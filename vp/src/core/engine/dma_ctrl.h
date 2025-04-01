#ifndef RISCV_VP_DMA_CTRL_H
#define RISCV_VP_DMA_CTRL_H

#include <stdint.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

#include <systemc>
#include <vector>
#include <string>

using namespace sc_core;
using namespace tlm;

class DMACTRL : public sc_module {
   public:
	tlm_utils::simple_target_socket<DMACTRL> local_tsock;
	tlm_utils::simple_initiator_socket<DMACTRL> local_isock;

	// reserve data from Router
	std::vector<uint8_t> router_data_buffer;

	sc_event send_hello_event;
	int send_counter;
	sc_time send_interval; 

	SC_CTOR(DMACTRL) {
		local_tsock.register_b_transport(this, &DMACTRL::router_b_transport);

		SC_THREAD(send_hello_world);
		
		send_counter = 0;
		send_interval = sc_time(10, SC_NS);  
	}

	void router_b_transport(tlm_generic_payload& trans, sc_time& delay) {
		uint8_t* data_ptr = trans.get_data_ptr();
		unsigned int data_len = trans.get_data_length();

		router_data_buffer.insert(router_data_buffer.end(), data_ptr, data_ptr + data_len);

		trans.set_response_status(TLM_OK_RESPONSE);

		router_data_buffer.push_back(0);

		printf("%s: %s\n", name(), data_ptr);

		router_data_buffer.clear();
	}

	int i = 1;

	void send_hello_world() {
		while (true) {
			wait(send_interval);

			if (strcmp(name(), "dma_ctrl[0,0]")) break;

			// create header
			uint8_t dst_id = i ++;
			uint8_t res_1  = 0;
			uint8_t res_2  = 0;
			uint8_t res_3  = 0;
			
			// create payload
			char message[12] = {'H', 'e', 'l', 'l', 'o', ' ', 'W', 'o', 'r', 'l', 'd', '!'};
			
			// create  transaction
			std::vector<uint8_t> buffer;
			buffer.push_back(dst_id);
			buffer.push_back(res_1);
			buffer.push_back(res_2);
			buffer.push_back(res_3);

			for (int i = 0; i < 12; i ++) {
				buffer.push_back(message[i]);
			}
			
			assert(buffer.size() % 4 == 0);

			tlm_generic_payload trans;
			sc_time delay = SC_ZERO_TIME;
			
			trans.set_command(TLM_WRITE_COMMAND);
			trans.set_data_ptr(buffer.data());
			trans.set_data_length(buffer.size());
			
			local_isock->b_transport(trans, delay);
			
			if (trans.get_response_status() == TLM_OK_RESPONSE) {
				std::cout << "Success send to router: " << message << std::endl; 
				if (i == 16)
					break;
			}
		}
	}
};

#endif