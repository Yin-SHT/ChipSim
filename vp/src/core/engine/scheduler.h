#ifndef RISCV_SCHEDULER_H
#define RISCV_SCHEDULER_H

#include <stdint.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

#include <iostream>
#include <memory>
#include <systemc>
#include <tlm>

#include "core/engine/ae.h"
#include "core/engine/dma_ctrl.h"
#include "core/engine/spu.h"
#include "core/engine/type.h"

using namespace sc_core;
using namespace tlm;

struct Scheduler : sc_module {
	tlm_utils::simple_target_socket<Scheduler> tsock;
	tlm_utils::simple_initiator_socket<Scheduler> spu_isock;
	tlm_utils::simple_initiator_socket<Scheduler> ae_isock;
	tlm_utils::simple_initiator_socket<Scheduler> dma_isock;

	SPU* spu_ref;
	AE* ae_ref;
	DMACTRL* dma_ref;

	tlm::tlm_generic_payload trans;
	sc_time delay;

	sc_fifo<Fileds*> cmd_queue;
	std::map<std::string, int> resource_table;

    SC_CTOR(Scheduler) : spu_ref(nullptr), ae_ref(nullptr), dma_ref(nullptr) {
		tsock.register_b_transport(this, &Scheduler::b_transport);

		SC_THREAD(schedule);
	}

	void set_targets(SPU *spu, AE *ae, DMACTRL* dma) {
		spu_ref = spu;
		ae_ref = ae;
		dma_ref = dma;
	}

	void update_resource_table() {
		spu_ref->update_resource_table(resource_table);
		ae_ref->update_resource_table(resource_table);
	}

	void handle_fence() {
		while (true) {
			bool all_free = true;

			for (const auto& pair : resource_table) {
				if (pair.second != QueueState::EMPTY) {
					all_free = false;
					break;
				}
			}

			if (all_free) {
				// core.long_instr_complete++;
				break;
			}

			wait(10, SC_NS);  // wait engine consume instruction
			update_resource_table();
		}
	}

	void try_send_to_engine(Fileds& fileds) {
		tlm_phase phase = BEGIN_REQ;

		trans.set_data_ptr(reinterpret_cast<unsigned char*>(&fileds));
		trans.set_write();

		uint32_t opcode = fileds["opcode"];
		tlm_sync_enum result;

		while (true) {
			if (opcode == Engine::MMA) {
				result = spu_isock->nb_transport_fw(trans, phase, delay);
				if (result != TLM_COMPLETED) {
					while (true) {
						wait(10, SC_NS);  // wait engine consume instruction
						update_resource_table();

						if (resource_table["SPU"] == QueueState::EMPTY ||
						    resource_table["SPU"] == QueueState::PARTIAL) {
							break;
						}
					}
				}
			} else if (opcode == Engine::SMX || opcode == Engine::ACT) {
				result = ae_isock->nb_transport_fw(trans, phase, delay);
				if (result != TLM_COMPLETED) {
					while (true) {
						wait(10, SC_NS);  // wait engine consume instruction
						update_resource_table();

						if (resource_table["AE"] == QueueState::EMPTY || resource_table["AE"] == QueueState::PARTIAL) {
							break;
						}
					}
				}
			} else {
				printf("%s: Unsupported Engine Opcode: %d\n", this->name(), opcode);
				throw std::invalid_argument("Unsupported Engine Opcode");
			}

			if (result == TLM_COMPLETED) {
				break;
			}
		}
	}

	void dispatch(Fileds& fileds) {
		uint32_t opcode = fileds["opcode"];

		if (opcode == Engine::FENCE) {
			printf("%s: FENCE\n", this->name());
			handle_fence();
		} else {
			printf("%s: Dispatch\n", this->name());
			try_send_to_engine(fileds);
		}
	}

	void schedule() {
		while (true) {
			Fileds* fileds = cmd_queue.read();
			dispatch(*fileds);

			wait(10, sc_core::SC_NS);  // Simulate interval between instructions
		}
	}

	void b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay) {
		Fileds* fileds = (Fileds*)trans.get_data_ptr();
		cmd_queue.write(fileds);
	}
};

#endif
