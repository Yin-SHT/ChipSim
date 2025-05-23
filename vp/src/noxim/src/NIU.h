/*
 * Noxim - the NoC Simulator
 *
 * (C) 2005-2018 by the University of Catania
 * For the complete list of authors refer to file ../doc/AUTHORS.txt
 * For the license applied to these sources refer to file ../doc/LICENSE.txt
 *
 * This file contains the declaration of the processing element
 */

#ifndef __NIU_H__
#define __NIU_H__

#include <systemc.h>
#include <tlm.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

#include <queue>

#include "DataStructs.h"
#include "GlobalTrafficTable.h"
#include "Utils.h"

using namespace std;
using namespace tlm;

enum RxState {
    Rx_IDLE,
    Rx_WAIT,
    Rx_ASSEMBLE,
    Rx_SEND,
};

enum TxState {
    Tx_IDLE,
    Tx_WAIT,
    Tx_DECOMPOSE,
    Tx_SEND,
};

enum BroadcastState {
    Broadcast_IDLE,
    Broadcast_WAIT,
    Broadcast_ASSEMBLE,
    Broadcast_SEND,
};

SC_MODULE(NIU) {
	// I/O Ports
	sc_in_clk clock;    // The input clock for the PE
	sc_in<bool> reset;  // The reset signal for the PE

	sc_in<Flit> flit_rx;  // The input channel
	sc_in<bool> req_rx;   // The request associated with the input channel
	sc_out<bool> ack_rx;  // The outgoing ack signal associated with the input channel
	sc_out<TBufferFullStatus> buffer_full_status_rx;

	sc_out<Flit> flit_tx;  // The output channel
	sc_out<bool> req_tx;   // The request associated with the output channel
	sc_in<bool> ack_tx;    // The outgoing ack signal associated with the output channel
	sc_in<TBufferFullStatus> buffer_full_status_tx;

	sc_in<Flit> flit_broadcast;  // The input channel
	sc_in<bool> req_broadcast;   // The request associated with the input channel
	sc_out<bool> ack_broadcast;  // The outgoing ack signal associated with the input channel

	sc_in<int> free_slots_neighbor;

	// Registers
	int local_id;                     // Unique identification number
	bool current_level_rx;            // Current level for Alternating Bit Protocol (ABP)
	bool current_level_tx;            // Current level for Alternating Bit Protocol (ABP)
	bool current_level_broadcast;     // Current level for Alternating Bit Protocol (ABP)

    // Used for flit from Router
    Flit head_flit;                   // Flit to be processed
    vector<uint8_t> router_buffer;    // Buffer to store the data from router

    Flit broadcast_head;              // Broadcast flit to be processed
    vector<uint8_t> broadcast_buffer;    // Buffer to store the data from router

    // Used for transaction from DMA Ctrl
    bool has_dma;   
    Header dma_trans;
    vector<uint8_t> dma_buffer;
    queue<Flit> flit_queue;

	tlm_utils::simple_target_socket<NIU> tsock; // target socket for DMA Ctrl
	tlm_utils::simple_initiator_socket<NIU> isock; // initiator socket for DMA Ctrl

    RxState rx_state;
    TxState tx_state;
    BroadcastState broadcast_state;

    Flit make_flit(int src_id, int dst_id, int vc_id, FlitType flit_type, int sequence_no, int sequence_length, Header &header);

    void rx_process();
    void tx_process();
    void broadcast_process();
	void b_transport(tlm_generic_payload& trans, sc_time& delay);

	// Constructor
	SC_CTOR(NIU) {

        rx_state = RxState::Rx_IDLE;
        tx_state = TxState::Tx_IDLE;

        has_dma = true;

		SC_METHOD(rx_process);
		sensitive << reset;
		sensitive << clock.pos();

		SC_METHOD(tx_process);
		sensitive << reset;
		sensitive << clock.pos();

		SC_METHOD(broadcast_process);
		sensitive << reset;
		sensitive << clock.pos();
	}
};

#endif