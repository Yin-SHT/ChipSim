#ifndef RISCV_VP_DUMMY_H
#define RISCV_VP_DUMMY_H

#include <stdint.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

#include <systemc>
#include <vector>
#include <string>

using namespace sc_core;
using namespace tlm;

class Dummy : public sc_core::sc_module {
   public:
	tlm_utils::simple_target_socket<Dummy> tsock;
	tlm_utils::simple_initiator_socket<Dummy> isock;

	SC_CTOR(Dummy) { }
};

#endif