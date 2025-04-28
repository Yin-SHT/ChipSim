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

enum NIUState {
    NIU_IDLE,

    DMA_TRANS,
    RX_TRANS,

    WAIT_ARREADY_MADE,
    WAIT_ARREADY_SEND_HEAD,
    WAIT_ARREADY_SEND_TAIL,

    WAIT_ARREADY,

    WAIT_RVALID_MADE,
    WAIT_RVALID_SEND_HEAD,
    WAIT_RVALID_SEND_TAIL,

    WAIT_RVALID,

    WAIT_ARVALID_MADE,
    WAIT_ARVALID_SEND_HEAD,
    WAIT_ARVALID_SEND_TAIL,

    WAIT_RREADY,

    WAIT_RREADY_MADE,
    WAIT_RREADY_SEND_HEAD,
    WAIT_RREADY_SEND_TAIL,
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

	sc_in<int> free_slots_neighbor;

	// Registers
	int local_id;                     // Unique identification number
	bool current_level_rx;            // Current level for Alternating Bit Protocol (ABP)
	bool current_level_tx;            // Current level for Alternating Bit Protocol (ABP)

	tlm_utils::simple_target_socket<NIU> tsock; // target socket for DMA Ctrl
	tlm_utils::simple_initiator_socket<NIU> isock; // initiator socket for DMA Ctrl

    Flit ar_head;
    Flit ar_tail;

    Flit r_head;
    Flit r_tail;

    bool has_dma;
    DmaTrans dma_trans;
    Flit rx_trans;

    NIUState niu_state;

    void state_machine();
	void b_transport(tlm_generic_payload& trans, sc_time& delay);

	// Constructor
	SC_CTOR(NIU) {
        has_dma = true;
        niu_state = NIU_IDLE;

		SC_METHOD(state_machine);
		sensitive << reset;
		sensitive << clock.pos();
	}
};

#endif