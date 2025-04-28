#ifndef RISCV_VP_DMA_CTRL_H
#define RISCV_VP_DMA_CTRL_H

#include <stdint.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

#include <atomic>
#include <systemc>
#include <vector>
#include <string>
#include "core/engine/type.h"
#include "noxim/src/DataStructs.h"

using namespace sc_core;
using namespace tlm;

// DMACTRL state enumeration
enum DMACTRLState {
	IDLE,
	SEND,
	SEND2,
	RECV
};

class DMACTRL : public sc_core::sc_module {
public:
	// I/O Ports
	sc_in_clk clock;
	sc_in<bool> reset;

	// TLM-2 sockets
	tlm_utils::simple_target_socket<DMACTRL> tsock;
	tlm_utils::simple_initiator_socket<DMACTRL> isock;
	tlm_utils::simple_target_socket<DMACTRL> local_tsock;
	tlm_utils::simple_initiator_socket<DMACTRL> local_isock;

	// FIFO to store commands
	sc_fifo<Fileds*> cmd_queue;

	// reserve data from Router
	std::vector<uint8_t> router_data_buffer;

	// Current state 
	DMACTRLState current_state;
	Fileds* current_cmd;
	
    // Indicates if there is a transaction from local_tsock
	bool has_received_local_trans;
	
	// Number of completed long instructions
	std::atomic<uint32_t> *long_instr_complete;

	SC_CTOR(DMACTRL) {
		// Register b_transport callback
		local_tsock.register_b_transport(this, &DMACTRL::local_transport);
		
		// Register state machine main method, sensitive to clock rising edge
		SC_METHOD(state_machine);
		sensitive << reset;
		sensitive << clock.pos();
	}

    bool has_send = false;

	// State machine main method
	void state_machine() {
		if (reset.read()) {
			current_state = IDLE;
			current_cmd = nullptr;
			has_received_local_trans = false;
			return;
		}
		
		// State machine logic
		switch (current_state) {
			case IDLE:
                break;
				if (has_received_local_trans) {
					current_state = RECV;
				} else if (cmd_queue.num_available() > 0) {
					current_cmd = cmd_queue.read();
					
					// Determine next state based on command content
					// Currently simple handling: all commands go to SEND state
					current_state = SEND;
				} else if (!strcmp(name(), "DMACTRL[4,5]") && !has_send) {
					current_state = SEND;
				}
				break;
				
			case SEND:
				handle_send_state();
				break;

			case SEND2:
				handle_send_state2();
				break;
				
			case RECV:
				handle_recv_state();
				break;
				
			default:
				// Unknown state, return to IDLE
				current_state = IDLE;
				break;
		}
	}

	// Handle transactions from local_tsock
	void local_transport(tlm_generic_payload& trans, sc_time& delay) {
		// Only process incoming transactions in IDLE state
		if (current_state == IDLE) {
			uint8_t* data_ptr = trans.get_data_ptr();
			unsigned int data_len = trans.get_data_length();

			// Store incoming data
			router_data_buffer.clear();
			router_data_buffer.insert(router_data_buffer.end(), data_ptr, data_ptr + data_len);

			// Mark that a transaction has been received
			has_received_local_trans = true;

			trans.set_response_status(TLM_OK_RESPONSE);
		} else {
			trans.set_response_status(TLM_INCOMPLETE_RESPONSE);
		}
	}

    uint8_t test_data[16] = {'H', 'e', 'l', 'l', 'o', ' ', 'W', 'o', 'r', 'l', 'd', '!'};

	// Handle SEND state: send transaction through local_isock
	void handle_send_state() {
        // assert(current_cmd != nullptr && "current_cmd is nullptr");

        uint8_t *data_ptr = new uint8_t[16];
        memcpy(data_ptr, test_data, 16);

        // Create a header
        Header header;
        header.dst_id = 0;
        header.hbm_id = -1;
        header.cmd = TLM_WRITE_COMMAND;
        header.addr = 0;
        header.data = data_ptr;
        header.len = 12;

        // Create a tlm_generic_payload
        tlm_generic_payload trans;
        sc_time delay = SC_ZERO_TIME;

        trans.set_data_ptr(reinterpret_cast<unsigned char*>(&header));
        trans.set_data_length(sizeof(Header));
        
        // Send transaction through local_isock
        local_isock->b_transport(trans, delay);
        
        // Check transaction status
        if (trans.get_response_status() == TLM_OK_RESPONSE) {
            // Free command memory
            delete current_cmd;
            current_cmd = nullptr;
            
            // Return to IDLE state
            std::cout << "\033[1;31m" << name() << ": Has Send WRITE transaction\033[0m" << std::endl;
            has_send = true;
            current_state = SEND2;
        }
	}

	// Handle SEND state: send transaction through local_isock
	void handle_send_state2() {
        // Create a header
        Header header;
        header.dst_id = 0;
        header.hbm_id = -1;
        header.cmd = TLM_READ_COMMAND;
        header.addr = 0;
        header.len = 12;

        // Create a tlm_generic_payload
        tlm_generic_payload trans;
        sc_time delay = SC_ZERO_TIME;

        trans.set_data_ptr(reinterpret_cast<unsigned char*>(&header));
        trans.set_data_length(sizeof(Header));
        
        // Send transaction through local_isock
        local_isock->b_transport(trans, delay);
        
        // Check transaction status
        if (trans.get_response_status() == TLM_OK_RESPONSE) {
            // Return to IDLE state
            std::cout << "\033[1;31m" << name() << ": Has Send READ transaction\033[0m" << std::endl;
            current_state = IDLE;
        }
	}

	// Handle RECV state: process data received from local_tsock
	void handle_recv_state() {
        // Process received data (can be extended based on actual requirements)
        std::cout << "\033[1;33m" << name() << ": Processing received data, size: " << router_data_buffer.size() << " bytes" << "\033[0m" << std::endl;
        for (uint8_t byte : router_data_buffer) {
            printf("\033[1;33m%c\033[0m", byte);
        }
        printf("\n");
        
        // Reset flag
        has_received_local_trans = false;
        router_data_buffer.clear();
        
        // Return to IDLE state
        current_state = IDLE;
	}

};

#endif