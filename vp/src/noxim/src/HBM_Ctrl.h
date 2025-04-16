/*
 * Noxim - the NoC Simulator
 *
 * (C) 2005-2018 by the University of Catania
 * For the complete list of authors refer to file ../doc/AUTHORS.txt
 * For the license applied to these sources refer to file ../doc/LICENSE.txt
 *
 * This file contains the declaration of the router
 */

#ifndef __HBM_CTRL_H__
#define __HBM_CTRL_H__

#include <systemc.h>
#include "DataStructs.h"
#include "Buffer.h"
#include "Stats.h"
#include "GlobalRoutingTable.h"
#include "LocalRoutingTable.h"
#include "ReservationTable.h"
#include "Utils.h"
#include <tlm.h>
#include <tlm_utils/simple_initiator_socket.h>

using namespace std;

SC_MODULE(HBM_CTRL)
{
    // I/O Ports
    sc_in_clk clock;		                // The input clock for the router
    sc_in <bool> reset;                     // The reset signal for the router

    // number of ports: 1 (east/west) directions 
    sc_in <Flit> flit_rx;	                // The input channels 
    sc_in <bool> req_rx;	                // The requests associated with the input channels
    sc_out <bool> ack_rx;	                // The outgoing ack signals associated with the input channels
    sc_out <TBufferFullStatus> buffer_full_status_rx;

    sc_out <Flit> flit_tx;                  // The output channels
    sc_out <bool> req_tx;	                // The requests associated with the output channels
    sc_in <bool> ack_tx;	                // The outgoing ack signals associated with the output channels
    sc_in <TBufferFullStatus> buffer_full_status_tx;

    // TLM socket for HBM communication
    tlm_utils::simple_initiator_socket<HBM_CTRL> hbm_socket;

    // Registers

    int local_id;		                    // Unique ID
    Buffer flits_buffer;
    Buffer buffer;
    bool current_level_rx;	                // Current level for Alternating Bit Protocol (ABP)
    bool current_level_tx;	                // Current level for Alternating Bit Protocol (ABP)
    ReservationTable reservation_table;		// Switch reservation table
    
    // Functions

    void process();
    void rxProcess();		// The receiving process
    void txProcess();		// The transmitting process
    void configure(const int _id, const unsigned int _max_buffer_size);
    void handleHBM(); 

    // Constructor

    SC_CTOR(HBM_CTRL) {
        SC_METHOD(process);
        sensitive << reset;
        sensitive << clock.pos();
    }
};

#endif
