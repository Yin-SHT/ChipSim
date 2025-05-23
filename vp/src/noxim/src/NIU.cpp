/*
 * Noxim - the NoC Simulator
 *
 * (C) 2005-2018 by the University of Catania
 * For the complete list of authors refer to file ../doc/AUTHORS.txt
 * For the license applied to these sources refer to file ../doc/LICENSE.txt
 *
 * This file contains the implementation of the network interface unit
 */

#include "NIU.h"

Flit NIU::make_flit(int src_id, int dst_id, int vc_id, FlitType flit_type, int sequence_no, int sequence_length, Header &header) {
    Flit flit;
    flit.src_id = src_id;
    flit.dst_id = dst_id;
    flit.vc_id = vc_id;
    flit.flit_type = flit_type;
    flit.sequence_no = sequence_no;
    flit.sequence_length = sequence_length;
    flit.cmd = header.cmd;
    flit.addr = header.addr;
    flit.len = header.len;
    for (int i = 0; i < FLIT_SIZE; i++) {
        flit.data[i] = 0;
    }
    flit.valid_len = 0;
    flit.is_broadcast = header.is_broadcast;
    flit.is_reduction = header.is_reduction;
    return flit;
}

void NIU::b_transport(tlm_generic_payload& trans, sc_time& delay) {
    if (tx_state != TxState::Tx_WAIT) {
        trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        return;
    }

    uint8_t* data_ptr = trans.get_data_ptr();
    memcpy(&dma_trans, data_ptr, sizeof(Header));
    memcpy(&dma_buffer, dma_trans.data, dma_trans.len);
}

void NIU::broadcast_process() {
    switch (broadcast_state) {
        case Broadcast_IDLE: {
            if (reset.read()) {
                ack_broadcast.write(0);
                current_level_broadcast = 0;
            } else {
                broadcast_state = Broadcast_WAIT;
            }
            break;
        }
        
        case Broadcast_WAIT: {
            if (req_broadcast.read() == 1 - current_level_broadcast) {
                Flit flit_tmp = flit_broadcast.read();
                current_level_broadcast = 1 - current_level_broadcast;  

                // defensive programming
                assert(flit_tmp.flit_type == FLIT_TYPE_HEAD);
                assert(flit_tmp.valid_len <= FLIT_SIZE);
                broadcast_head = flit_tmp;
                broadcast_buffer.clear();
                broadcast_state = Broadcast_ASSEMBLE;
            }
            ack_broadcast.write(current_level_broadcast);
            break;
        }
        
        case Broadcast_ASSEMBLE: {
            if (req_broadcast.read() == 1 - current_level_broadcast) {
                Flit flit_tmp = flit_broadcast.read();
                current_level_broadcast = 1 - current_level_broadcast;  

                // defensive programming
                assert(flit_tmp.flit_type == FLIT_TYPE_BODY || flit_tmp.flit_type == FLIT_TYPE_TAIL);
                assert(flit_tmp.valid_len <= FLIT_SIZE);
                assert(flit_tmp.src_id == broadcast_head.src_id);
                broadcast_buffer.insert(broadcast_buffer.end(), flit_tmp.data, flit_tmp.data + flit_tmp.valid_len);

                if (flit_tmp.flit_type == FLIT_TYPE_TAIL) {
                    broadcast_state = Broadcast_SEND;
                }
            }
            ack_broadcast.write(current_level_broadcast);
            break;
        }
        
        case Broadcast_SEND: {
            tlm_generic_payload trans;
            sc_time delay = SC_ZERO_TIME;

            assert(broadcast_head.cmd == tlm::TLM_WRITE_COMMAND);
            assert(broadcast_head.len == broadcast_buffer.size());
            
            trans.set_command(broadcast_head.cmd);
            trans.set_address(broadcast_head.addr);
            trans.set_data_length(broadcast_head.len);
            trans.set_data_ptr(broadcast_buffer.data());
            
            // isock->b_transport(trans, delay);
            printf("%d: ", local_id);
            for (int i = 0; i < broadcast_buffer.size(); i++) {
                printf("%c", broadcast_buffer[i]);
            }
            printf("\n");
            
            // if (trans.get_response_status() == tlm::TLM_OK_RESPONSE) {
            //     broadcast_state = broadcast_WAIT;
            // }
            broadcast_state = Broadcast_WAIT;
            break;
        }
    }

}

void NIU::rx_process() {
    switch (rx_state) {
        case Rx_IDLE: {
            if (reset.read()) {
                ack_rx.write(0);
                current_level_rx = 0;
            } else {
                rx_state = Rx_WAIT;
            }
            break;
        }
        
        case Rx_WAIT: {
            if (req_rx.read() == 1 - current_level_rx) {
                Flit flit_tmp = flit_rx.read();
                current_level_rx = 1 - current_level_rx;  

                // defensive programming
                assert(flit_tmp.flit_type == FLIT_TYPE_HEAD);
                assert(flit_tmp.valid_len <= FLIT_SIZE);
                head_flit = flit_tmp;
                router_buffer.clear();
                rx_state = Rx_ASSEMBLE;
            }
            ack_rx.write(current_level_rx);
            break;
        }
        
        case Rx_ASSEMBLE: {
            if (req_rx.read() == 1 - current_level_rx) {
                Flit flit_tmp = flit_rx.read();
                current_level_rx = 1 - current_level_rx;  

                // defensive programming
                assert(flit_tmp.flit_type == FLIT_TYPE_BODY || flit_tmp.flit_type == FLIT_TYPE_TAIL);
                assert(flit_tmp.valid_len <= FLIT_SIZE);
                assert(flit_tmp.src_id == head_flit.src_id);
                router_buffer.insert(router_buffer.end(), flit_tmp.data, flit_tmp.data + flit_tmp.valid_len);

                if (flit_tmp.flit_type == FLIT_TYPE_TAIL) {
                    rx_state = Rx_SEND;
                }
            }
            ack_rx.write(current_level_rx);
            break;
        }
        
        case Rx_SEND: {
            tlm_generic_payload trans;
            sc_time delay = SC_ZERO_TIME;

            assert(head_flit.cmd == tlm::TLM_WRITE_COMMAND);
            assert(head_flit.len == router_buffer.size());
            
            trans.set_command(head_flit.cmd);
            trans.set_address(head_flit.addr);
            trans.set_data_length(head_flit.len);
            trans.set_data_ptr(router_buffer.data());
            
            // isock->b_transport(trans, delay);
            printf("%d: ", local_id);
            for (int i = 0; i < router_buffer.size(); i++) {
                printf("%c", router_buffer[i]);
            }
            printf("\n");
            
            // if (trans.get_response_status() == tlm::TLM_OK_RESPONSE) {
            //     rx_state = Rx_WAIT;
            // }
            rx_state = Rx_WAIT;
            break;
        }
    }
}

void NIU::tx_process() {
    switch (tx_state) {
        case Tx_IDLE: {
            if (reset.read()) {
                req_tx.write(0);
                current_level_tx = 0;
            } else {
                tx_state = Tx_WAIT;
            }
            break;
        }
        
        case Tx_WAIT: {
            if (has_dma) {
                tx_state = Tx_DECOMPOSE;
            } 
            break;
        }
        
        case Tx_DECOMPOSE: {
            if (local_id == 16) {
                dma_trans.dst_id = 31;
                dma_trans.hbm_id = 0;
                dma_trans.cmd = tlm::TLM_WRITE_COMMAND;
                dma_trans.addr = 0x1000;
                dma_trans.len = FLIT_SIZE;
                dma_trans.data = new uint8_t[dma_trans.len];
                for (int i = 0; i < dma_trans.len; i++) {
                    dma_trans.data[i] = 'a' + i;
                }
                dma_trans.is_broadcast = true;
                dma_trans.is_reduction = false;
            } else {
                has_dma = false;
                break;
            }

            int dst_id = dma_trans.dst_id;
            int sequence_length = (dma_trans.len + FLIT_SIZE - 1) / FLIT_SIZE + 2;  // HEAD + BODY + TAIL
            Flit head_flit = make_flit(local_id, dst_id, 0, FLIT_TYPE_HEAD, 0, sequence_length, dma_trans);
            flit_queue.push(head_flit);

            int remaining_len = dma_trans.len;
            int offset = 0;
            int seq_no = 1;
            while (remaining_len > 0) {
                Flit body_flit = make_flit(local_id, dst_id, 0, FLIT_TYPE_BODY, seq_no++, sequence_length, dma_trans);
                
                int copy_len = min(remaining_len, FLIT_SIZE);
                memcpy(body_flit.data, dma_trans.data + offset, copy_len);
                body_flit.valid_len = copy_len;
                
                flit_queue.push(body_flit);
                
                remaining_len -= copy_len;
                offset += copy_len;
            }

            Flit tail_flit = make_flit(local_id, dst_id, 0, FLIT_TYPE_TAIL, seq_no, sequence_length, dma_trans);
            flit_queue.push(tail_flit);

            tx_state = Tx_SEND;
            break;    
        }

        case Tx_SEND: {
            if (ack_tx.read() == current_level_tx) {
                if (!flit_queue.empty()) {
                    Flit flit = flit_queue.front();
                    flit_queue.pop();
                    flit_tx.write(flit);
                    current_level_tx = 1 - current_level_tx;
                    req_tx.write(current_level_tx);
                } else {
                    has_dma = false;
                    tx_state = Tx_WAIT;
                }
            }
            break;
        }
    }
}
