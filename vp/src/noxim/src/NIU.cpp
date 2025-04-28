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
                    dma_trans.cmd = TLM_WRITE_COMMAND;
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
                niu_state = WAIT_AW_W_READY_MADE;
            } 
            break;

        case RX_TRANS:
            if (rx_trans.axi_channel == AXI_CHANNEL_AR) {
                niu_state = WAIT_ARVALID_MADE;
            } else if (rx_trans.axi_channel == AXI_CHANNEL_AW) {
                niu_state = WAIT_AW_W_VALID_MADE;
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
        
        case WAIT_AW_W_READY_MADE:
            aw_w_head.src_id = local_id;
            aw_w_head.dst_id = dma_trans.dst_id;
            aw_w_head.vc_id = 0;  
            aw_w_head.flit_type = FLIT_TYPE_HEAD;
            aw_w_head.sequence_no = 0;
            aw_w_head.sequence_length = 2;  // head flit + tail flit
            aw_w_head.timestamp = sc_time_stamp().to_double();
            aw_w_head.hop_no = 0;
            
            aw_w_head.awid = local_id;  
            aw_w_head.axi_channel = AXI_CHANNEL_AW;
            
            aw_w_head.awvalid = true;
            aw_w_head.awready = false;
            aw_w_head.awaddr = dma_trans.addr;
            aw_w_head.awprot = 0;

            aw_w_head.wvalid = true;
            aw_w_head.wready = false;
            aw_w_head.wdata = 0xdeadbeaf;
            aw_w_head.wstrb = 0;

            aw_w_tail.src_id = local_id;
            aw_w_tail.dst_id = dma_trans.dst_id;
            aw_w_tail.vc_id = 0;
            aw_w_tail.flit_type = FLIT_TYPE_TAIL;
            aw_w_tail.sequence_no = 1;
            aw_w_tail.sequence_length = 2;
            aw_w_tail.timestamp = sc_time_stamp().to_double();
            aw_w_tail.hop_no = 0;

            niu_state = WAIT_AW_W_READY_SEND_HEAD;
            break;

        case WAIT_AW_W_READY_SEND_HEAD:
            if (ack_tx.read() == current_level_tx) {
                flit_tx->write(aw_w_head);                     
                current_level_tx = 1 - current_level_tx;  
                req_tx.write(current_level_tx);

                niu_state = WAIT_AW_W_READY_SEND_TAIL;
            }
            break;

        case WAIT_AW_W_READY_SEND_TAIL:
            if (ack_tx.read() == current_level_tx) {
                flit_tx->write(aw_w_tail);     
                current_level_tx = 1 - current_level_tx;
                req_tx.write(current_level_tx);

                niu_state = WAIT_AW_W_READY;
            }
            break;

        case WAIT_AW_W_READY:
            if (req_rx.read() == 1 - current_level_rx) {
                Flit flit_tmp = flit_rx.read();
                current_level_rx = 1 - current_level_rx;

                if (flit_tmp.flit_type == FLIT_TYPE_HEAD) {
                    assert(flit_tmp.awid == dma_trans.dst_id);

                    if (flit_tmp.awready == 1 && flit_tmp.wready == 1) {
                        niu_state = WAIT_BVALID_MADE;
                    }
                }
            }
            ack_rx.write(current_level_rx);
            break;

        case WAIT_BVALID_MADE:
            b_head.src_id = local_id;
            b_head.dst_id = dma_trans.dst_id;
            b_head.vc_id = 0;
            b_head.flit_type = FLIT_TYPE_HEAD;
            b_head.sequence_no = 0;
            b_head.sequence_length = 2;
            b_head.timestamp = sc_time_stamp().to_double();
            b_head.hop_no = 0;

            b_head.axi_channel = AXI_CHANNEL_B;
            b_head.bid = local_id;
            b_head.bvalid = false;
            b_head.bready = true;
            b_head.bresp = 0;
                       
            b_tail.src_id = local_id;
            b_tail.dst_id = dma_trans.dst_id;
            b_tail.vc_id = 0;
            b_tail.flit_type = FLIT_TYPE_TAIL;
            b_tail.sequence_no = 1;
            b_tail.sequence_length = 2;
            b_tail.timestamp = sc_time_stamp().to_double();
            b_tail.hop_no = 0;

            niu_state = WAIT_BVALID_SEND_HEAD;
            break;

        case WAIT_BVALID_SEND_HEAD:
            if (ack_tx.read() == current_level_tx) {
                flit_tx->write(b_head);
                current_level_tx = 1 - current_level_tx;
                req_tx.write(current_level_tx);

                niu_state = WAIT_BVALID_SEND_TAIL;
            }
            break;

        case WAIT_BVALID_SEND_TAIL:
            if (ack_tx.read() == current_level_tx) {
                flit_tx->write(b_tail);
                current_level_tx = 1 - current_level_tx;
                req_tx.write(current_level_tx);

                niu_state = WAIT_BVALID; 
            }
            break;

        case WAIT_BVALID:
            if (req_rx.read() == 1 - current_level_rx) {
                Flit flit_tmp = flit_rx.read();
                current_level_rx = 1 - current_level_rx;  

                if (flit_tmp.flit_type == FLIT_TYPE_HEAD) {
                    assert(flit_tmp.bid == dma_trans.dst_id);

                    if (flit_tmp.bvalid == 1) {
                        has_dma = false;
                        niu_state = NIU_IDLE;
                    }
                }
            }
            ack_rx.write(current_level_rx);
            break;

        case WAIT_AW_W_VALID_MADE:
            aw_w_head.src_id = local_id;
            aw_w_head.dst_id = rx_trans.src_id;
            aw_w_head.vc_id = 0;  
            aw_w_head.flit_type = FLIT_TYPE_HEAD;
            aw_w_head.sequence_no = 0;
            aw_w_head.sequence_length = 2;  // head flit + tail flit
            aw_w_head.timestamp = sc_time_stamp().to_double();
            aw_w_head.hop_no = 0;
            
            aw_w_head.awid = local_id;  
            aw_w_head.axi_channel = AXI_CHANNEL_AW;

            aw_w_head.awvalid = false;
            aw_w_head.awready = true;
            aw_w_head.awaddr = 0;
            aw_w_head.awprot = 0;

            aw_w_head.wvalid = false;
            aw_w_head.wready = true;
            aw_w_head.wdata = 0;
            aw_w_head.wstrb = 0;

            aw_w_tail.src_id = local_id;
            aw_w_tail.dst_id = rx_trans.dst_id;
            aw_w_tail.vc_id = 0;
            aw_w_tail.flit_type = FLIT_TYPE_TAIL;
            aw_w_tail.sequence_no = 1;
            aw_w_tail.sequence_length = 2;
            aw_w_tail.timestamp = sc_time_stamp().to_double();
            aw_w_tail.hop_no = 0;

            niu_state = WAIT_AW_W_VALID_SEND_HEAD;
            break;

        case WAIT_AW_W_VALID_SEND_HEAD:
            if (ack_tx.read() == current_level_tx) {
                flit_tx->write(aw_w_head);                     
                current_level_tx = 1 - current_level_tx;  
                req_tx.write(current_level_tx);

                niu_state = WAIT_AW_W_VALID_SEND_TAIL;
            }
            break;

        case WAIT_AW_W_VALID_SEND_TAIL:
            if (ack_tx.read() == current_level_tx) {
                flit_tx->write(aw_w_tail);     
                current_level_tx = 1 - current_level_tx;
                req_tx.write(current_level_tx);

                niu_state = WAIT_BREADY;
            }
            break;

        case WAIT_BREADY:
            if (req_rx.read() == 1 - current_level_rx) {
                Flit flit_tmp = flit_rx.read();
                current_level_rx = 1 - current_level_rx;

                if (flit_tmp.flit_type == FLIT_TYPE_HEAD) {
                    assert(flit_tmp.bid == rx_trans.bid);

                    if (flit_tmp.bready == 1) {
                        niu_state = WAIT_BREADY_MADE;
                    }
                }
            }
            ack_rx.write(current_level_rx);
            break;

        case WAIT_BREADY_MADE:
            b_head.src_id = local_id;
            b_head.dst_id = rx_trans.src_id;
            b_head.vc_id = 0;  
            b_head.flit_type = FLIT_TYPE_HEAD;
            b_head.sequence_no = 0;
            b_head.sequence_length = 2;  // head flit + tail flit
            b_head.timestamp = sc_time_stamp().to_double();
            b_head.hop_no = 0;
            
            b_head.axi_channel = AXI_CHANNEL_B;
            b_head.bid = local_id;
            b_head.bvalid = true;
            b_head.bready = false;
            b_head.bresp = 0;

            b_tail.src_id = local_id;
            b_tail.dst_id = rx_trans.dst_id;
            b_tail.vc_id = 0;
            b_tail.flit_type = FLIT_TYPE_TAIL;
            b_tail.sequence_no = 1;
            b_tail.sequence_length = 2;
            b_tail.timestamp = sc_time_stamp().to_double();
            b_tail.hop_no = 0;

            niu_state = WAIT_BREADY_SEND_HEAD;
            break;

        case WAIT_BREADY_SEND_HEAD:
            if (ack_tx.read() == current_level_tx) {
                flit_tx->write(b_head);                     
                current_level_tx = 1 - current_level_tx;  
                req_tx.write(current_level_tx);

                printf("\033[31m0x%lx\033[0m\n", rx_trans.wdata);

                niu_state = WAIT_RREADY_SEND_TAIL;
            }
            break;

        case WAIT_BREADY_SEND_TAIL:
            if (ack_tx.read() == current_level_tx) {
                flit_tx->write(b_tail);     
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
