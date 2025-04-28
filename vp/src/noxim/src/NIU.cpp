/*
 * Noxim - the NoC Simulator
 *
 * (C) 2005-2018 by the University of Catania
 * For the complete list of authors refer to file ../doc/AUTHORS.txt
 * For the license applied to these sources refer to file ../doc/LICENSE.txt
 *
 * This file contains the implementation of the processing element
 */

#include "NIU.h"

void NIU::b_transport(tlm_generic_payload& trans, sc_time& delay) {
    if (niu_state != NIU_IDLE) {
        trans.set_response_status(TLM_INCOMPLETE_RESPONSE);
        return;
    }

    // Get command, address and length
    tlm::tlm_command cmd = trans.get_command();
    uint64_t addr = trans.get_address();
    int len = trans.get_data_length();
    uint8_t* data = trans.get_data_ptr();
    assert(len == sizeof(DmaTrans));

    // Get Dma Transaction
    memcpy(&dma_trans, data, len);
    trans.set_response_status(TLM_OK_RESPONSE);

    return;
}

void NIU::state_machine() {
    switch (niu_state) {
        case NIU_IDLE:
            if (has_dma) {
                if (local_id == 0) {
                    niu_state = DMA_TRANS;
                    dma_trans.cmd = TLM_READ_COMMAND;
                    dma_trans.dst_id = 12;
                } else {
                    has_dma = false;
                }
            } else {
                if (req_rx.read() == 1 - current_level_rx) {
                    rx_trans = flit_rx.read();
                    current_level_rx = 1 - current_level_rx;	

                    if (rx_trans.flit_type == FLIT_TYPE_HEAD) {
                        niu_state = RX_TRANS;
                    }
                }
                ack_rx.write(current_level_rx);
            }
            break;

        case DMA_TRANS:
            if (dma_trans.cmd == TLM_READ_COMMAND) {
                niu_state = WAIT_ARREADY_MADE;
            } else if (dma_trans.cmd == TLM_WRITE_COMMAND) {

            }
            break;

        case RX_TRANS:
            if (rx_trans.axi_channel == AXI_CHANNEL_AR) {
                niu_state = WAIT_ARVALID_MADE;
            }
            break;

        case WAIT_ARREADY_MADE:
            ar_head.src_id = local_id;
            ar_head.dst_id = dma_trans.dst_id;
            ar_head.vc_id = 0;  
            ar_head.flit_type = FLIT_TYPE_HEAD;
            ar_head.sequence_no = 0;
            ar_head.sequence_length = 2;  // head flit + tail flit
            ar_head.timestamp = sc_time_stamp().to_double();
            ar_head.hop_no = 0;
            
            ar_head.arid = local_id;  
            ar_head.axi_channel = AXI_CHANNEL_AR;
            ar_head.arvalid = true;
            ar_head.arready = false;
            ar_head.araddr = dma_trans.addr;
            ar_head.arprot = 0;

            ar_tail.src_id = local_id;
            ar_tail.dst_id = dma_trans.dst_id;
            ar_tail.vc_id = 0;
            ar_tail.flit_type = FLIT_TYPE_TAIL;
            ar_tail.sequence_no = 1;
            ar_tail.sequence_length = 2;
            ar_tail.timestamp = sc_time_stamp().to_double();
            ar_tail.hop_no = 0;

            niu_state = WAIT_ARREADY_SEND_HEAD;
            break;

        case WAIT_ARREADY_SEND_HEAD:
            if (ack_tx.read() == current_level_tx) {
                flit_tx->write(ar_head);                     
                current_level_tx = 1 - current_level_tx;  
                req_tx.write(current_level_tx);

                niu_state = WAIT_ARREADY_SEND_TAIL;
            }
            break;

        case WAIT_ARREADY_SEND_TAIL:
            if (ack_tx.read() == current_level_tx) {
                flit_tx->write(ar_tail);     
                current_level_tx = 1 - current_level_tx;
                req_tx.write(current_level_tx);

                niu_state = WAIT_ARREADY;
            }
            break;

        case WAIT_ARREADY:
            if (req_rx.read() == 1 - current_level_rx) {
                Flit flit_tmp = flit_rx.read();
                current_level_rx = 1 - current_level_rx;

                if (flit_tmp.flit_type == FLIT_TYPE_HEAD) {
                    assert(flit_tmp.arid == dma_trans.dst_id);

                    if (flit_tmp.arready == 1) {
                        niu_state = WAIT_RVALID_MADE;
                    }
                }
            }
            ack_rx.write(current_level_rx);
            break;

        case WAIT_RVALID_MADE:
            r_head.src_id = local_id;
            r_head.dst_id = dma_trans.dst_id;
            r_head.vc_id = 0;
            r_head.flit_type = FLIT_TYPE_HEAD;
            r_head.sequence_no = 0;
            r_head.sequence_length = 2;
            r_head.timestamp = sc_time_stamp().to_double();
            r_head.hop_no = 0;

            r_head.axi_channel = AXI_CHANNEL_R;
            r_head.rid = local_id;
            r_head.rready = true;
                        
            r_tail.src_id = local_id;
            r_tail.dst_id = dma_trans.dst_id;
            r_tail.vc_id = 0;
            r_tail.flit_type = FLIT_TYPE_TAIL;
            r_tail.sequence_no = 1;
            r_tail.sequence_length = 2;
            r_tail.timestamp = sc_time_stamp().to_double();
            r_tail.hop_no = 0;

            niu_state = WAIT_RVALID_SEND_HEAD;
            break;

        case WAIT_RVALID_SEND_HEAD:
            if (ack_tx.read() == current_level_tx) {
                flit_tx->write(r_head);
                current_level_tx = 1 - current_level_tx;
                req_tx.write(current_level_tx);

                niu_state = WAIT_RVALID_SEND_TAIL;
            }
            break;

        case WAIT_RVALID_SEND_TAIL:
            if (ack_tx.read() == current_level_tx) {
                flit_tx->write(r_tail);
                current_level_tx = 1 - current_level_tx;
                req_tx.write(current_level_tx);

                niu_state = WAIT_RVALID; 
            }
            break;

        case WAIT_RVALID:
            if (req_rx.read() == 1 - current_level_rx) {
                Flit flit_tmp = flit_rx.read();
                current_level_rx = 1 - current_level_rx;  

                if (flit_tmp.flit_type == FLIT_TYPE_HEAD) {
                    if (flit_tmp.rvalid == 1) {
                        printf("rdata: %lx\n", flit_tmp.rdata);

                        has_dma = false;
                        niu_state = NIU_IDLE;
                    }
                }
            }
            ack_rx.write(current_level_rx);
            break;

        case WAIT_ARVALID_MADE:
            ar_head.src_id = local_id;
            ar_head.dst_id = rx_trans.src_id;
            ar_head.vc_id = 0;  
            ar_head.flit_type = FLIT_TYPE_HEAD;
            ar_head.sequence_no = 0;
            ar_head.sequence_length = 2;  // head flit + tail flit
            ar_head.timestamp = sc_time_stamp().to_double();
            ar_head.hop_no = 0;
            
            ar_head.arid = local_id;  
            ar_head.axi_channel = AXI_CHANNEL_AR;
            ar_head.arvalid = false;
            ar_head.arready = true;
            ar_head.araddr = 0;
            ar_head.arprot = 0;

            ar_tail.src_id = local_id;
            ar_tail.dst_id = rx_trans.dst_id;
            ar_tail.vc_id = 0;
            ar_tail.flit_type = FLIT_TYPE_TAIL;
            ar_tail.sequence_no = 1;
            ar_tail.sequence_length = 2;
            ar_tail.timestamp = sc_time_stamp().to_double();
            ar_tail.hop_no = 0;

            niu_state = WAIT_ARVALID_SEND_HEAD;
            break;

        case WAIT_ARVALID_SEND_HEAD:
            if (ack_tx.read() == current_level_tx) {
                flit_tx->write(ar_head);                     
                current_level_tx = 1 - current_level_tx;  
                req_tx.write(current_level_tx);

                niu_state = WAIT_ARVALID_SEND_TAIL;
            }
            break;

        case WAIT_ARVALID_SEND_TAIL:
            if (ack_tx.read() == current_level_tx) {
                flit_tx->write(ar_tail);     
                current_level_tx = 1 - current_level_tx;
                req_tx.write(current_level_tx);

                niu_state = WAIT_RREADY;
            }
            break;

        case WAIT_RREADY:
            if (req_rx.read() == 1 - current_level_rx) {
                Flit flit_tmp = flit_rx.read();
                current_level_rx = 1 - current_level_rx;

                if (flit_tmp.flit_type == FLIT_TYPE_HEAD) {
                    assert(flit_tmp.arid == rx_trans.arid);

                    if (flit_tmp.rready == 1) {
                        niu_state = WAIT_RREADY_MADE;
                    }
                }
            }
            ack_rx.write(current_level_rx);
            break;

        case WAIT_RREADY_MADE:
            r_head.src_id = local_id;
            r_head.dst_id = rx_trans.src_id;
            r_head.vc_id = 0;  
            r_head.flit_type = FLIT_TYPE_HEAD;
            r_head.sequence_no = 0;
            r_head.sequence_length = 2;  // head flit + tail flit
            r_head.timestamp = sc_time_stamp().to_double();
            r_head.hop_no = 0;
            
            r_head.axi_channel = AXI_CHANNEL_R;
            r_head.rid = local_id;
            r_head.rvalid = true;
            r_head.rready = false;
            r_head.rvalid = true;
            r_head.rdata = 0xdeadbeaf;
            r_head.rresp = 0;

            r_tail.src_id = local_id;
            r_tail.dst_id = rx_trans.dst_id;
            r_tail.vc_id = 0;
            r_tail.flit_type = FLIT_TYPE_TAIL;
            r_tail.sequence_no = 1;
            r_tail.sequence_length = 2;
            r_tail.timestamp = sc_time_stamp().to_double();
            r_tail.hop_no = 0;

            niu_state = WAIT_RREADY_SEND_HEAD;
            break;

        case WAIT_RREADY_SEND_HEAD:
            if (ack_tx.read() == current_level_tx) {
                flit_tx->write(r_head);                     
                current_level_tx = 1 - current_level_tx;  
                req_tx.write(current_level_tx);

                niu_state = WAIT_RREADY_SEND_TAIL;
            }
            break;

        case WAIT_RREADY_SEND_TAIL:
            if (ack_tx.read() == current_level_tx) {
                flit_tx->write(r_tail);     
                current_level_tx = 1 - current_level_tx;
                req_tx.write(current_level_tx);

                niu_state = NIU_IDLE;
            }
            break;

        default:
            assert(false && "Invalid state");
            break;
    }
}
