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

// #define TRACE

static char *NIUStateName[] = {
    "NIU_IDLE",

    "DMA_TRANS",
    "RX_TRANS",

    // RETRY
    "RETRY",

    // INVALID
    "INVALID_MADE",
    "INVALID_SEND_HEAD",
    "INVALID_SEND_TAIL",

    // AR、R
    "WAIT_ARREADY_MADE",
    "WAIT_ARREADY_SEND_HEAD",
    "WAIT_ARREADY_SEND_TAIL",

    "WAIT_ARREADY",

    "WAIT_RVALID_MADE",
    "WAIT_RVALID_SEND_HEAD",
    "WAIT_RVALID_SEND_TAIL",

    "WAIT_RVALID",

    "WAIT_ARVALID_MADE",
    "WAIT_ARVALID_SEND_HEAD",
    "WAIT_ARVALID_SEND_TAIL",

    "WAIT_RREADY",

    "WAIT_RREADY_MADE",
    "WAIT_RREADY_SEND_HEAD",
    "WAIT_RREADY_SEND_TAIL",

    // AW、W、B
    "WAIT_AW_W_READY_MADE",
    "WAIT_AW_W_READY_SEND_HEAD",
    "WAIT_AW_W_READY_SEND_TAIL",

    "WAIT_AW_W_READY",

    "WAIT_BVALID_MADE",
    "WAIT_BVALID_SEND_HEAD",
    "WAIT_BVALID_SEND_TAIL",

    "WAIT_BVALID",

    "WAIT_AW_W_VALID_MADE",
    "WAIT_AW_W_VALID_SEND_HEAD",
    "WAIT_AW_W_VALID_SEND_TAIL",

    "WAIT_BREADY",

    "WAIT_BREADY_MADE",
    "WAIT_BREADY_SEND_HEAD",
    "WAIT_BREADY_SEND_TAIL",
};

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

void NIU::make_filt(Flit &flit, int src_id, int dst_id, FlitType flit_type) {
    flit.src_id = src_id;
    flit.dst_id = dst_id;
    flit.vc_id = 0;  
    flit.flit_type = flit_type;
    flit.sequence_no = flit_type == FLIT_TYPE_HEAD ? 0 : 1;
    flit.sequence_length = 2;  // head flit + tail flit
    flit.timestamp = sc_time_stamp().to_double();
    flit.hop_no = 0;
}

bool NIU::send_filt(Flit flit) {
    if (ack_tx.read() == current_level_tx) {
        flit_tx->write(flit);                     
        current_level_tx = 1 - current_level_tx;  
        req_tx.write(current_level_tx);

        return true;
    }

    return false;
}

Flit NIU::read_flit() {
    Flit flit;
    flit.flit_type = FLIT_TYPE_NONE;

    if (req_rx.read() == 1 - current_level_rx) {
        flit = flit_rx.read();
        current_level_rx = 1 - current_level_rx;
    }
    ack_rx.write(current_level_rx);

    return flit;
}

void NIU::state_machine() {
    last_state = niu_state;

    switch (niu_state) {
        case NIU_IDLE:
            if (has_dma) {
                // niu_state = DMA_TRANS;

                // TEST
                if (local_id != 0 && local_id <= 15) {
                    dma_trans.cmd = TLM_READ_COMMAND;
                    dma_trans.dst_id = 0;

                    niu_state = DMA_TRANS;
                } else {
                    niu_state = NIU_IDLE;
                    has_dma = false;
                }
            } else {
                if (req_rx.read() == 1 - current_level_rx) {
                    rx_trans = flit_rx.read();
                    current_level_rx = 1 - current_level_rx;	

                    if (rx_trans.flit_type == FLIT_TYPE_HEAD) {
                        assert(rx_trans.axi_channel == AXI_CHANNEL_AR || rx_trans.axi_channel == AXI_CHANNEL_AW);
                        assert(rx_trans.arvalid == 1 || (rx_trans.awvalid == 1 && rx_trans.wvalid == 1));

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

        //--------------------------------
        //
        //  Retry
        //
        //--------------------------------
        case RETRY:
            niu_state = after_retry;
            break;

        //--------------------------------
        //
        //  INVALID
        //
        //--------------------------------
        case INVALID_MADE:
            make_filt(invalid_head, local_id, invalid_flit.src_id, FLIT_TYPE_HEAD);
            make_filt(invalid_tail, local_id, invalid_flit.src_id, FLIT_TYPE_TAIL);

            // Set AXI channel signals
            invalid_head.axi_channel = invalid_flit.axi_channel;
            
            switch (invalid_flit.axi_channel) {
                case AXI_CHANNEL_AR:
                    assert(invalid_flit.arvalid == 1);

                    invalid_head.arid = local_id;
                    invalid_head.arvalid = 0;
                    invalid_head.arready = 0; // !!!
                    invalid_head.araddr = 0;
                    invalid_head.arprot = 0;

                    break;

                case AXI_CHANNEL_AW:
                    assert(invalid_flit.awvalid == 1);

                    invalid_head.awid = local_id;
                    invalid_head.awvalid = 0;
                    invalid_head.awready = 0; // !!!
                    invalid_head.awaddr = 0;
                    invalid_head.awprot = 0;

                    invalid_head.wvalid = 0;
                    invalid_head.wready = 0; // !!!
                    invalid_head.wdata = 0;
                    invalid_head.wstrb = 0;

                    break;

                default:
                    assert(false && "Invalid AXI channel");
            }

            niu_state = INVALID_SEND_HEAD;
            break;

        case INVALID_SEND_HEAD:
            if (!send_filt(invalid_head)) break;

            niu_state = INVALID_SEND_TAIL;
            break;

        case INVALID_SEND_TAIL:
            if (!send_filt(invalid_tail)) break;

            niu_state = after_invalid;
            break;

        //-------------------------------------
        //
        //              AXI Read
        //
        //-------------------------------------

        
        //-------------------------------------
        //              MASTER
        //-------------------------------------
        case WAIT_ARREADY_MADE:
            make_filt(ar_head, local_id, dma_trans.dst_id, FLIT_TYPE_HEAD);
            make_filt(ar_tail, local_id, dma_trans.dst_id, FLIT_TYPE_TAIL);
            
            // Set AXI channel singals
            ar_head.axi_channel = AXI_CHANNEL_AR;

            ar_head.arid = local_id;  
            ar_head.arvalid = 1;
            ar_head.arready = 0;
            ar_head.araddr = dma_trans.addr;
            ar_head.arprot = 0;

            niu_state = WAIT_ARREADY_SEND_HEAD;
            break;

        case WAIT_ARREADY_SEND_HEAD:
            if (!send_filt(ar_head)) break;

            niu_state = WAIT_ARREADY_SEND_TAIL;
            break;

        case WAIT_ARREADY_SEND_TAIL:
            if (!send_filt(ar_tail)) break;

            niu_state = WAIT_ARREADY;
            break;

        case WAIT_ARREADY:
            flit = read_flit();
            next_state = WAIT_ARREADY;

            if (flit.flit_type == FLIT_TYPE_HEAD) {
                if (flit.src_id == dma_trans.dst_id) {
                    // node to be communicated

                    if (flit.arready == 1) {
                        next_state = WAIT_RVALID_MADE;
                    } else if (flit.arready == 0) {
                        after_retry = WAIT_ARREADY_MADE;
                        next_state = RETRY;
                    }
                } else if (flit.src_id != dma_trans.dst_id) {
                    // other node

                    invalid_flit = flit;
                    after_invalid = WAIT_ARREADY;
                    next_state = INVALID_MADE;
                }
            }

            niu_state = next_state;
            break;

        case WAIT_RVALID_MADE:
            make_filt(r_head, local_id, dma_trans.dst_id, FLIT_TYPE_HEAD);
            make_filt(r_tail, local_id, dma_trans.dst_id, FLIT_TYPE_TAIL);
            
            // Set AXI channel signals
            r_head.axi_channel = AXI_CHANNEL_R;

            r_head.rid = local_id;
            r_head.rvalid = 0;
            r_head.rready = 1;
            r_head.rdata = 0;
            r_head.rresp = 0;

            niu_state = WAIT_RVALID_SEND_HEAD;
            break;

        case WAIT_RVALID_SEND_HEAD:
            if (!send_filt(r_head)) break;

            niu_state = WAIT_RVALID_SEND_TAIL;
            break;

        case WAIT_RVALID_SEND_TAIL:
            if (!send_filt(r_tail)) break;

            niu_state = WAIT_RVALID; 
            break;

        case WAIT_RVALID:
            flit = read_flit();
            next_state = WAIT_RVALID;

            if (flit.flit_type == FLIT_TYPE_HEAD) {
                if (flit.src_id == dma_trans.dst_id) {
                    assert(flit.rvalid == 1);
                    
                    // Do something here
                    printf("NIU[%d,%d] read: %lx\n", local_id % GlobalParams::mesh_dim_x, local_id / GlobalParams::mesh_dim_x, flit.rdata);
                    has_dma = false;

                    next_state = NIU_IDLE;
                } else if (flit.src_id != dma_trans.dst_id) {
                    invalid_flit = flit;
                    after_invalid = WAIT_RVALID;
                    next_state = INVALID_MADE;
                }
            }

            niu_state = next_state;
            break;

        //-------------------------------------
        //              SLAVE
        //-------------------------------------
        case WAIT_ARVALID_MADE:
            make_filt(ar_head, local_id, rx_trans.src_id, FLIT_TYPE_HEAD);
            make_filt(ar_tail, local_id, rx_trans.src_id, FLIT_TYPE_TAIL);
            
            // Set AXI channel signals
            ar_head.axi_channel = AXI_CHANNEL_AR;

            ar_head.arid = local_id;  
            ar_head.arvalid = 0;
            ar_head.arready = 1;
            ar_head.araddr = 0;
            ar_head.arprot = 0;

            niu_state = WAIT_ARVALID_SEND_HEAD;
            break;

        case WAIT_ARVALID_SEND_HEAD:
            if (!send_filt(ar_head)) break;

            niu_state = WAIT_ARVALID_SEND_TAIL;
            break;

        case WAIT_ARVALID_SEND_TAIL:
            if (!send_filt(ar_tail)) break;

            niu_state = WAIT_RREADY;
            break;

        case WAIT_RREADY:
            flit = read_flit();
            next_state = WAIT_RREADY;

            if (flit.flit_type == FLIT_TYPE_HEAD) {
                if (flit.src_id == rx_trans.src_id) {
                    assert(flit.rready == 1);

                    next_state = WAIT_RREADY_MADE;
                } else if (flit.src_id != rx_trans.src_id) {
                    invalid_flit = flit;
                    after_invalid = WAIT_RREADY;
                    next_state = INVALID_MADE;
                }
            }

            niu_state = next_state;
            break;

        case WAIT_RREADY_MADE:
            make_filt(r_head, local_id, rx_trans.src_id, FLIT_TYPE_HEAD);
            make_filt(r_tail, local_id, rx_trans.src_id, FLIT_TYPE_TAIL);

            r_head.axi_channel = AXI_CHANNEL_R;
            r_head.rid = local_id;

            r_head.rvalid = 1;
            r_head.rready = 0;
            r_head.rdata = 0xdeadbeafdeadbeaf;
            r_head.rresp = 0;

            niu_state = WAIT_RREADY_SEND_HEAD;
            break;

        case WAIT_RREADY_SEND_HEAD:
            if (!send_filt(r_head)) break;

            niu_state = WAIT_RREADY_SEND_TAIL;
            break;

        case WAIT_RREADY_SEND_TAIL:
            if (!send_filt(r_tail)) break;

            niu_state = NIU_IDLE;
            break;
        
        //-------------------------------------
        //
        //              AXI Write
        //
        //-------------------------------------
        case WAIT_AW_W_READY_MADE:
            make_filt(aw_w_head, local_id, dma_trans.dst_id, FLIT_TYPE_HEAD);
            make_filt(aw_w_tail, local_id, dma_trans.dst_id, FLIT_TYPE_TAIL);
            
            aw_w_head.awid = local_id;  
            aw_w_head.axi_channel = AXI_CHANNEL_AW;
            
            aw_w_head.awvalid = 1;
            aw_w_head.awready = 0;
            aw_w_head.awaddr = dma_trans.addr;
            aw_w_head.awprot = 0;

            aw_w_head.wvalid = 1;
            aw_w_head.wready = 0;
            aw_w_head.wdata = 0xdeadbeafdeadbeaf;
            aw_w_head.wstrb = 0;

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
            make_filt(b_head, local_id, dma_trans.dst_id, FLIT_TYPE_HEAD);
            make_filt(b_tail, local_id, dma_trans.dst_id, FLIT_TYPE_TAIL);

            b_head.axi_channel = AXI_CHANNEL_B;
            b_head.bid = local_id;

            b_head.bvalid = 0;
            b_head.bready = 1;
            b_head.bresp = 0;
                       
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
                        printf("NIU[%d,%d] write: %lx\n", local_id % GlobalParams::mesh_dim_x, local_id / GlobalParams::mesh_dim_x, dma_data);

                        has_dma = false;
                        niu_state = NIU_IDLE;
                    }
                }
            }
            ack_rx.write(current_level_rx);
            break;

        case WAIT_AW_W_VALID_MADE:
            make_filt(aw_w_head, local_id, rx_trans.src_id, FLIT_TYPE_HEAD);
            make_filt(aw_w_tail, local_id, rx_trans.src_id, FLIT_TYPE_TAIL);
            
            aw_w_head.awid = local_id;  
            aw_w_head.axi_channel = AXI_CHANNEL_AW;

            aw_w_head.awvalid = 0;
            aw_w_head.awready = 1;
            aw_w_head.awaddr = 0;
            aw_w_head.awprot = 0;

            aw_w_head.wvalid = 0;
            aw_w_head.wready = 1;
            aw_w_head.wdata = 0;
            aw_w_head.wstrb = 0;

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
            make_filt(b_head, local_id, rx_trans.src_id, FLIT_TYPE_HEAD);
            make_filt(b_tail, local_id, rx_trans.src_id, FLIT_TYPE_TAIL);
            
            b_head.axi_channel = AXI_CHANNEL_B;
            b_head.bid = local_id;

            b_head.bvalid = 0;
            b_head.bready = 1;
            b_head.bresp = 0;

            niu_state = WAIT_BREADY_SEND_HEAD;
            break;

        case WAIT_BREADY_SEND_HEAD:
            if (ack_tx.read() == current_level_tx) {
                flit_tx->write(b_head);                     
                current_level_tx = 1 - current_level_tx;  
                req_tx.write(current_level_tx);

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

#ifdef TRACE
    if (last_state != niu_state) {
        if (local_id == 0) {
            // red output
            printf("\033[31mNIU[%d,%d]: %s --> %s\033[0m\n", local_id % GlobalParams::mesh_dim_x, local_id / GlobalParams::mesh_dim_x, NIUStateName[last_state], NIUStateName[niu_state]);
        } 
        
        if (local_id == 1) {
            // yellow output
            printf("\033[34mNIU[%d,%d]: %s --> %s\033[0m\n", local_id % GlobalParams::mesh_dim_x, local_id / GlobalParams::mesh_dim_x, NIUStateName[last_state], NIUStateName[niu_state]);
        }

        if (local_id == 2) {
            // green output
            printf("\033[32mNIU[%d,%d]: %s --> %s\033[0m\n", local_id % GlobalParams::mesh_dim_x, local_id / GlobalParams::mesh_dim_x, NIUStateName[last_state], NIUStateName[niu_state]);
        }
    }
#endif
}