#ifndef __NIU_H__
#define __NIU_H__

#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

#include <systemc>

using namespace sc_core;
using namespace tlm;

enum NIUState {
	IDLE,
};

class NIU : public sc_core::sc_module {
   public:
	// I/O Ports
	sc_in_clk clock;
	sc_in<bool> reset;
};

#endif