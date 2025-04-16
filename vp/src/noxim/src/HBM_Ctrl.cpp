/*
 * Noxim - the NoC Simulator
 *
 * (C) 2005-2018 by the University of Catania
 * For the complete list of authors refer to file ../doc/AUTHORS.txt
 * For the license applied to these sources refer to file ../doc/LICENSE.txt
 *
 * This file contains the implementation of the router
 */

#include "HBM_Ctrl.h"

void HBM_CTRL::process() {
	txProcess();
	rxProcess();
	handleHBM();
}

void HBM_CTRL::rxProcess() {
	if (reset.read()) {
		TBufferFullStatus bfs;
		// Clear outputs and indexes of receiving protocol
		ack_rx.write(0);
		current_level_rx = 0;
		buffer_full_status_rx.write(bfs);
	} else {
		// This process simply sees a flow of incoming flits. All arbitration
		// and wormhole related issues are addressed in the txProcess()
		// assert(false);

        // To accept a new flit, the following conditions must match:
        // 1) there is an incoming request
        // 2) there is a free slot in the input buffer of direction i

        if (req_rx.read() == 1 - current_level_rx) {
            Flit received_flit = flit_rx.read();

            if (!flits_buffer.IsFull()) {
                // Store the incoming flit in the circular buffer
                flits_buffer.Push(received_flit);

                // Negate the old value for Alternating Bit Protocol (ABP)
                current_level_rx = 1 - current_level_rx;
            }
        }
        ack_rx.write(current_level_rx);
        // updates the mask of VCs to prevent incoming data on full buffers
        TBufferFullStatus bfs;
        for (int vc = 0; vc < GlobalParams::n_virtual_channels; vc++) bfs.mask[vc] = buffer.IsFull();
            buffer_full_status_rx.write(bfs);
	}
}

void HBM_CTRL::txProcess() {
	if (reset.read()) {
		// Clear outputs and indexes of transmitting protocol
		req_tx.write(0);
		current_level_tx = 0;
	} else {
		// 1st phase: Reservation
        if (!buffer.IsEmpty()) {
            Flit flit = buffer.Front();

            if (flit.flit_type == FLIT_TYPE_HEAD) {
                int o = 0;

                TReservation r;
                r.input = 0;
                r.vc = 0;

                int rt_status = reservation_table.checkReservation(r, o);

                if (rt_status == RT_AVAILABLE) {
                    LOG << " reserving direction " << o << " for flit " << flit << endl;
                    reservation_table.reserve(r, o);
                } else if (rt_status == RT_ALREADY_SAME) {
                    LOG << " RT_ALREADY_SAME reserved direction " << o << " for flit " << flit << endl;
                } else if (rt_status == RT_OUTVC_BUSY) {
                    LOG << " RT_OUTVC_BUSY reservation direction " << o << " for flit " << flit << endl;
                } else if (rt_status == RT_ALREADY_OTHER_OUT) {
                    LOG << "RT_ALREADY_OTHER_OUT: another output previously reserved for the same flit "
                        << endl;
                } else
                    assert(false);  // no meaningful status here
            }
        }

		// 2nd phase: Forwarding
        vector<pair<int, int> > reservations = reservation_table.getReservations(0);

        if (reservations.size() != 0) {
            int rnd_idx = rand() % reservations.size();

            int o = reservations[rnd_idx].first;
            int vc = reservations[rnd_idx].second;

            if (!buffer.IsEmpty()) {
                Flit flit = buffer.Front();

                if ((current_level_tx == ack_tx.read()) && (buffer_full_status_tx.read().mask[vc] == false)) {

                    flit_tx.write(flit);
                    current_level_tx = 1 - current_level_tx;
                    req_tx.write(current_level_tx);
                    buffer.Pop();

                    if (flit.flit_type == FLIT_TYPE_TAIL) {
                        TReservation r;
                        r.input = 0;
                        r.vc = vc;
                        reservation_table.release(r, o);
                    }
                } 
            }
        }  // if not reserved

		if ((int)(sc_time_stamp().to_double() / GlobalParams::clock_period_ps) % 2 == 0)
			reservation_table.updateIndex();
	}
}

void HBM_CTRL::configure(const int _id, const unsigned int _max_buffer_size) {
	local_id = _id;

	reservation_table.setSize(1);

	buffer.SetMaxBufferSize(_max_buffer_size);
	buffer.setLabel(string(name()) + "->buffer[" + i_to_string(0) + "]");
}

void HBM_CTRL::handleHBM() {
    // Define HBM controller state enumeration
    enum HBMState {
        HBM_IDLE,
        HBM_READ
    };
    
    // Use static variable to maintain state
    static HBMState state = HBM_IDLE;
    static tlm::tlm_command current_cmd;
    static uint64_t current_addr;
    static int remaining_len;
    static int current_sequence_no = 0;
    static int total_sequence_length = 0;
    static int src_id, dst_id, vc_id;
    static double timestamp;
    static int hop_no;
    
    // Create TLM transaction and delay objects
    tlm::tlm_generic_payload trans;
    sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
    
    // Check if flits_buffer is empty
    if (flits_buffer.IsEmpty()) {
        return; // No flits to process
    }
    
    // Get the first flit from the flits_buffer
    Flit flit = flits_buffer.Front();
    
    // Process flits_buffer based on current state
    switch (state) {
        case HBM_IDLE:
            {
                if (flit.flit_type == FLIT_TYPE_HEAD && flit.cmd == tlm::TLM_READ_COMMAND) {
                    state = HBM_READ;

                    // Save transaction information
                    current_cmd = flit.cmd;
                    current_addr = flit.addr;
                    remaining_len = flit.len;
                    total_sequence_length = 1 + ((remaining_len + FLIT_SIZE - 1) / FLIT_SIZE) + 1;
                    src_id = flit.src_id;
                    dst_id = flit.dst_id;
                    vc_id = flit.vc_id;
                    timestamp = flit.timestamp;
                    hop_no = flit.hop_no;
                    current_sequence_no = 0;
                } else if (flit.flit_type == FLIT_TYPE_BODY && flit.cmd == tlm::TLM_WRITE_COMMAND) {
                    // Set transaction parameters
                    trans.set_command(flit.cmd);
                    trans.set_address(flit.addr);
                    trans.set_data_length(flit.valid_len);
                    
                    // Allocate and prepare write data
                    uint8_t* data_ptr = new uint8_t[flit.valid_len];
                    memcpy(data_ptr, flit.data, flit.valid_len);
                    trans.set_data_ptr(data_ptr);
                    
                    // Send write transaction to HBM
                    hbm_socket->b_transport(trans, delay);
                    
                    // Free allocated memory
                    delete[] data_ptr;
                    
                    if (trans.get_response_status() == tlm::TLM_OK_RESPONSE) {
                        // Remove processed flit
                        flits_buffer.Pop();
                    }
                } else if (flit.flit_type == FLIT_TYPE_HEAD && flit.cmd == tlm::TLM_WRITE_COMMAND) {
                    // Remove head flit
                    flits_buffer.Pop();
                } else if (flit.flit_type == FLIT_TYPE_TAIL) {
                    // Remove tail flit
                    flits_buffer.Pop();
                } else {
                    assert(false && "Invalid flit type");
                }
            }
            break;
            
        case HBM_READ:
            {
                // Defensive programming
                assert(current_cmd == tlm::TLM_READ_COMMAND);
                
                // Check if there is enough buffer space for response
                if (buffer.IsFull()) {
                    break;
                }
                
                // Determine the type of flit to create
                FlitType current_flit_type;
                if (current_sequence_no == 0) {
                    current_flit_type = FLIT_TYPE_HEAD;
                } else if (current_sequence_no == total_sequence_length - 1) {
                    current_flit_type = FLIT_TYPE_TAIL;
                } else {
                    current_flit_type = FLIT_TYPE_BODY;
                }
                
                // Create response flit
                Flit response_flit;
                response_flit.src_id = dst_id;
                response_flit.dst_id = src_id;
                response_flit.vc_id = vc_id;
                response_flit.timestamp = timestamp;
                response_flit.hop_no = hop_no;
                response_flit.sequence_no = current_sequence_no;
                response_flit.sequence_length = total_sequence_length;
                response_flit.flit_type = current_flit_type;
                
                // Only BODY type flit contains actual data read from HBM
                if (current_flit_type == FLIT_TYPE_BODY) {
                    // Calculate the number of bytes to read this time
                    int bytes_to_read = min(remaining_len, FLIT_SIZE);
                    response_flit.valid_len = bytes_to_read;
                    
                    // Set transaction parameters
                    trans.set_command(tlm::TLM_READ_COMMAND);
                    trans.set_address(current_addr);
                    trans.set_data_length(bytes_to_read);
                    
                    // Allocate buffer for read data
                    uint8_t* data_ptr = new uint8_t[bytes_to_read];
                    trans.set_data_ptr(data_ptr);
                    
                    // Send read transaction to HBM
                    hbm_socket->b_transport(trans, delay);
                    
                    // Check if transaction is successful
                    if (trans.get_response_status() == tlm::TLM_OK_RESPONSE) {
                        // Copy read data to response flit
                        memcpy(response_flit.data, data_ptr, bytes_to_read);
                        
                        // Update address and remaining length
                        current_addr += bytes_to_read;
                        remaining_len -= bytes_to_read;

                        // Push response flit to send buffer
                        buffer.Push(response_flit);
                        current_sequence_no++;
                    }
                    
                    // Release allocated memory
                    delete[] data_ptr;
                } else {
                    // For HEAD and TAIL type flits, no actual data
                    // You can choose to clear the data area
                    memset(response_flit.data, 0, FLIT_SIZE);
                    response_flit.valid_len = 0;

                    // Push response flit to send buffer
                    buffer.Push(response_flit);
                    current_sequence_no++;
                }
                
                // If all data has been read and TAIL flit has been sent, return to IDLE state
                if (current_sequence_no >= total_sequence_length) {
                    assert(!flits_buffer.IsEmpty());
                    assert(flits_buffer.Front().flit_type == FLIT_TYPE_HEAD);
                    assert(flits_buffer.Front().cmd == tlm::TLM_READ_COMMAND);

                    state = HBM_IDLE;
                    flits_buffer.Pop();
                }
            }
            break;
    }
}
