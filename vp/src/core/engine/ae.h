#ifndef RISCV_VP_AE_H
#define RISCV_VP_AE_H

#include <stdint.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

#include <cmath>
#include <atomic>
#include <cstring>
#include <systemc>

#include "core/engine/type.h"

using namespace sc_core;
using namespace tlm;

class AE : public sc_core::sc_module {
   public:
	tlm_utils::simple_target_socket<AE> tsock;
	tlm_utils::simple_initiator_socket<AE> isock;

	sc_fifo<Fileds*> cmd_queue;

	std::atomic<uint32_t> *long_instr_complete;

	uint8_t* in_data = nullptr;
	uint8_t* p_data = nullptr;

	float* in_data_fp32 = nullptr;
	float* p_data_fp32 = nullptr;

	// Register
	uint32_t m_i[64];
	uint32_t L_i[64];

	SC_CTOR(AE) : cmd_queue(16), long_instr_complete(nullptr) {
		tsock.register_nb_transport_fw(this, &AE::nb_transport_fw);

		SC_THREAD(sentry);
	}

	void decode_execute(Fileds& fileds) {
		load_data(fileds);

		preprocess_data(fileds);

		compute(fileds, in_data_fp32, p_data_fp32, fileds["smx.n"], fileds["smx.m"], fileds["smx.dim"], fileds["act.n"],
		        fileds["act.m"]);

		store_data(fileds);
	}

	void store_data(Fileds& fileds) {
		uint32_t opcode = fileds["opcode"];

		if (opcode == 0b000010) {
			if (fileds["smx.opmask"] & (1 << 1)) {  // Store output
				store_out(fileds["smx.out.addr"], fileds["smx.n"], fileds["smx.m"], fileds["smx.out.stride"]);

				// delete in_data;
				// delete p_data;
				delete in_data_fp32;
				delete p_data_fp32;

				in_data = nullptr;
				p_data = nullptr;
				in_data_fp32 = nullptr;
				p_data_fp32 = nullptr;
			}

			if (fileds["smx.opmask"] & (1 << 2)) {  // Store psum
				store_p(fileds["smx.p.addr"], fileds["smx.n"], fileds["smx.dim"], fileds["smx.p.stride"]);

				// delete in_data;
				// delete p_data;
				delete in_data_fp32;
				delete p_data_fp32;

				in_data = nullptr;
				p_data = nullptr;
				in_data_fp32 = nullptr;
				p_data_fp32 = nullptr;
			}
		} else if (opcode == 0b000011) {
			store_out(fileds["act.out.addr"], fileds["act.n"], fileds["act.m"], fileds["act.out.stride"]);

			// delete in_data;
			// delete p_data;
			delete in_data_fp32;
			delete p_data_fp32;

			in_data = nullptr;
			p_data = nullptr;
			in_data_fp32 = nullptr;
			p_data_fp32 = nullptr;
		}
	}

	void compute(Fileds& fileds, float* in_data, float* p_data, int smx_n, int smx_m, int smx_dim, int act_n,
	             int act_m) {
		uint32_t opcode = fileds["opcode"];

		if (opcode == 0b000010) {
			online_softmax(in_data, p_data, smx_n, smx_m, smx_dim);
		} else if (opcode == 0b000011) {
			act_reduction(in_data, act_n, act_m);
		}
	}

	void act_reduction(float* in_data, int N, int M) {
		for (int i = 0; i < N; i++) {
			for (int j = 1; j < M; j++) {
				in_data[i * M + j] = in_data[i * M + j] / L_i[i];
			}
		}
	}

	void online_softmax(float* in_data, float* p_data, int N, int M, int DIM) {
		// 1. 计算每行最大值
		float* m_ij = new float[N];
		for (int i = 0; i < N; i++) {
			m_ij[i] = in_data[i * M];
			for (int j = 1; j < M; j++) {
				m_ij[i] = std::max(m_ij[i], in_data[i * M + j]);
			}
		}

		// 2. 输入归一化
		for (int i = 0; i < N; i++) {
			for (int j = 0; j < M; j++) {
				in_data[i * M + j] = in_data[i * M + j] - m_ij[i];
			}
		}

		// 3. 指数变换
		for (int i = 0; i < N * M; i++) {
			in_data[i] = std::exp(in_data[i]);
		}

		// 4. 行式求和
		float* L_ij = new float[N];
		for (int i = 0; i < N; i++) {
			L_ij[i] = 0.0f;
			for (int j = 0; j < M; j++) {
				L_ij[i] += in_data[i * M + j];
			}
		}

		// 5. 补偿因子计算
		float* alpha = new float[N];
		for (int i = 0; i < N; i++) {
			alpha[i] = std::exp(m_i[i] - m_ij[i]);
		}

		// 6. 部分和更新
		for (int i = 0; i < N; i++) {
			for (int j = 0; j < DIM; j++) {
				p_data[i * DIM + j] *= alpha[i];
			}
		}

		// 7. 分母更新
		for (int i = 0; i < N; i++) {
			L_i[i] = L_i[i] * alpha[i] + L_ij[i];  // L_i
		}

		// 8. 最大值更新
		for (int i = 0; i < N; i++) {
			m_i[i] = m_ij[i];  // m_i
		}

		// 清理临时数组
		delete[] m_ij;
		delete[] L_ij;
		delete[] alpha;
	}

	void store_out(uint32_t& dst, uint32_t N, uint32_t M, uint32_t stride) {
		uint8_t* data = (uint8_t*)in_data_fp32;

		tlm::tlm_generic_payload trans;
		trans.set_command(tlm::TLM_WRITE_COMMAND);
		trans.set_address(dst);
		trans.set_data_ptr(data);
		trans.set_data_length(N * M * 4);  // TODO: 需要修改
		trans.set_response_status(tlm::TLM_OK_RESPONSE);
		sc_core::sc_time local_delay = sc_core::SC_ZERO_TIME;
		isock->b_transport(trans, local_delay);

		dst += stride;
	}

	void store_p(uint32_t& dst, uint32_t N, uint32_t M, uint32_t stride) {
		uint8_t* data = (uint8_t*)p_data_fp32;

		tlm::tlm_generic_payload trans;
		trans.set_command(tlm::TLM_WRITE_COMMAND);
		trans.set_address(dst);
		trans.set_data_ptr(data);
		trans.set_data_length(N * M * 4);  // TODO: 需要修改
		trans.set_response_status(tlm::TLM_OK_RESPONSE);
		sc_core::sc_time local_delay = sc_core::SC_ZERO_TIME;
		isock->b_transport(trans, local_delay);

		dst += stride;
	}

	void preprocess_data(Fileds& fileds) {
		uint32_t opcode = fileds["opcode"];

		if (opcode == 0b000010) {
			uint32_t opmask = fileds["smx.opmask"];

			if (opmask & (1 << 4)) {  // input
				in_data_fp32 = (float*)in_data;
			}

			if (opmask & (1 << 3)) {  // psum
				p_data_fp32 = (float*)p_data;
			}
		} else if (opcode == 0b000011) {
			uint32_t opmask = fileds["act.opmask"];

			if (opmask & (1 << 1)) {  // input
				in_data_fp32 = (float*)in_data;
			}
		}
	}

	void load_data(Fileds& fileds) {
		uint32_t opcode = fileds["opcode"];

		if (opcode == 0b000010) {
			uint32_t opmask = fileds["smx.opmask"];

			if (opmask & (1 << 4)) {  // Load Input (FP32 temp)
				in_data = load_in(fileds["smx.in.addr"], fileds["smx.n"], fileds["smx.m"], 4, fileds["smx.in.stride"]);
			}

			if (opmask & (1 << 3)) {  // Load Psum (FP32 temp)
				p_data = load_p(fileds["smx.p.addr"], fileds["smx.n"], fileds["smx.dim"], 4, fileds["smx.p.stride"]);
			}

			if (fileds["smx.init"]) {
				for (int i = 0; i < 64; i++) {
					m_i[i] = 0.0f;
					L_i[i] = 0.0f;
				}
			}
		} else if (opcode == 0b000011) {
			uint32_t opmask = fileds["act.opmask"];

			if (opmask & (1 << 1)) {  // Load Input (FP32 temp)
				in_data = load_in(fileds["act.in.addr"], fileds["act.n"], fileds["act.m"], 4, fileds["act.in.stride"]);
			}
		}
	}

	uint8_t* load_in(uint32_t& src, uint32_t N, uint32_t M, uint32_t dtype_size, uint32_t stride) {
		int length = (N * M) * dtype_size;
		uint8_t* data = new uint8_t[length];

		tlm::tlm_generic_payload trans;
		trans.set_command(tlm::TLM_READ_COMMAND);
		trans.set_address(src);
		trans.set_data_ptr(data);
		trans.set_data_length(length);
		trans.set_response_status(tlm::TLM_OK_RESPONSE);
		sc_core::sc_time local_delay = sc_core::SC_ZERO_TIME;
		isock->b_transport(trans, local_delay);

		src += stride;

		return data;
	}

	uint8_t* load_p(uint32_t& src, uint32_t N, uint32_t DIM, uint32_t dtype_size, uint32_t stride) {
		int length = (N * DIM) * dtype_size;
		uint8_t* data = new uint8_t[length];

		tlm::tlm_generic_payload trans;
		trans.set_command(tlm::TLM_READ_COMMAND);
		trans.set_address(src);
		trans.set_data_ptr(data);
		trans.set_data_length(length);
		trans.set_response_status(tlm::TLM_OK_RESPONSE);
		sc_core::sc_time local_delay = sc_core::SC_ZERO_TIME;
		isock->b_transport(trans, local_delay);

		src += stride;

		return data;
	}

	void sentry() {
		while (true) {
			if (cmd_queue.num_available() > 0) {
				Fileds* fileds = cmd_queue.read();

				decode_execute(*fileds);
				delete fileds;

				(*long_instr_complete) ++;
			}

			wait(10, sc_core::SC_NS);  // Simulate interval between instructions
		}
	}

	// Update Queue status to Scheduler
	void update_resource_table(std::map<std::string, int>& resource_table) {
		if (cmd_queue.num_available() == 0) {
			resource_table["AE"] = QueueState::EMPTY;
		} else if (cmd_queue.num_available() == 16) {
			resource_table["AE"] = QueueState::FULL;
		} else {
			resource_table["AE"] = QueueState::PARTIAL;
		}
	}

	bool is_queue_full() {
		return cmd_queue.num_available() >= 16;
	}

	tlm_sync_enum nb_transport_fw(tlm_generic_payload& trans, tlm_phase& phase, sc_time& delay) {
		if (phase == BEGIN_REQ) {
			Fileds* fileds = reinterpret_cast<Fileds*>(trans.get_data_ptr());

			if (is_queue_full()) {
				printf("AE: FIFO full, cannot accept Command at %s\n", sc_time_stamp().to_string().c_str());
				return TLM_ACCEPTED;
			}

			cmd_queue.write(fileds);
			printf("AE: Command added to FIFO at %s\n", sc_time_stamp().to_string().c_str());

			phase = END_RESP;
			return TLM_COMPLETED;
		}
		return TLM_ACCEPTED;
	}
};

#endif