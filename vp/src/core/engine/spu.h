#ifndef RISCV_VP_SPU_H
#define RISCV_VP_SPU_H

#include <stdint.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

#include <cstring>
#include <systemc>

#include "core/engine/type.h"

using namespace sc_core;
using namespace tlm;

class SPU : public sc_core::sc_module {
   public:
	tlm_utils::simple_target_socket<SPU> tsock;
	tlm_utils::simple_initiator_socket<SPU> isock;

	sc_fifo<Fileds*> cmd_queue;

	uint8_t* hp_data = nullptr;
	uint8_t* lp_data = nullptr;
	uint8_t* w_data = nullptr;

	BF16* hp_data_bf16 = nullptr;
	FP8* lp_data_fp8 = nullptr;
	FP8* w_data_fp8 = nullptr;

	float* hp_data_fp32 = nullptr;
	float* lp_data_fp32 = nullptr;
	float* w_data_fp32 = nullptr;
	float* acc_data_fp32 = nullptr;

	SC_CTOR(SPU) : cmd_queue(16) {
		tsock.register_nb_transport_fw(this, &SPU::nb_transport_fw);

		SC_THREAD(sentry);
	}

	void decode_execute(Fileds& fileds) {
		load_data(fileds);

		preprocess_data(fileds);

		mat_mul_add(hp_data_fp32, fileds["mma.n"], fileds["mma.k"], w_data_fp32, fileds["mma.k"], fileds["mma.m"],
		            acc_data_fp32, fileds["mma.n"], fileds["mma.m"]);
		mat_mul_add(lp_data_fp32, fileds["mma.n"], fileds["mma.k"], w_data_fp32, fileds["mma.k"], fileds["mma.m"],
		            acc_data_fp32, fileds["mma.n"], fileds["mma.m"]);

		output_data(fileds);
	}

	void output_data(Fileds& fileds) {
		if (fileds["mma.opmask"] & (1 << 0)) {  // Store bitmask
			store_out(fileds["mma.out.addr"], fileds["mma.n"], fileds["mma.m"], fileds["mma.out.stride"]);

			delete hp_data;
			delete lp_data;
			delete w_data;

			delete hp_data_bf16;
			delete lp_data_fp8;
			delete w_data_fp8;

			delete hp_data_fp32;
			delete lp_data_fp32;
			delete w_data_fp32;
			delete acc_data_fp32;

			hp_data = nullptr;
			lp_data = nullptr;
			w_data = nullptr;

			hp_data_bf16 = nullptr;
			lp_data_fp8 = nullptr;
			w_data_fp8 = nullptr;

			hp_data_fp32 = nullptr;
			lp_data_fp32 = nullptr;
			w_data_fp32 = nullptr;
			acc_data_fp32 = nullptr;
		}
	}

	void store_out(uint32_t& dst, uint32_t N, uint32_t M, uint32_t stride) {
		uint8_t* data = (uint8_t*)acc_data_fp32;

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

	void mat_mul_add(float* A_mat, uint32_t A_row, uint32_t A_col, float* B_mat, uint32_t B_row, uint32_t B_col,
	                 float* C_mat, uint32_t C_row, uint32_t C_col) {
		if (A_col != B_row) {
			throw std::invalid_argument("Matrix dimensions for multiplication are incompatible: A_col != B_row");
		}
		if (A_row != C_row || B_col != C_col) {
			throw std::invalid_argument(
			    "Matrix dimensions for addition are incompatible: result dimensions must match C");
		}

		for (uint32_t i = 0; i < A_row; ++i) {
			for (uint32_t j = 0; j < B_col; ++j) {
				float sum = 0.0f;
				for (uint32_t k = 0; k < A_col; ++k) {
					sum += A_mat[i * A_col + k] * B_mat[k * B_col + j];
				}
				C_mat[i * C_col + j] += sum;
			}
		}
	}

	void preprocess_data(Fileds& fileds) {
		uint32_t opmask = fileds["mma.opmask"];

		if (opmask & (1 << 8)) {  // Load HP
			hp_data_bf16 = convert_BF16((uint16_t*)hp_data, fileds["mma.n"] * fileds["mma.k"]);
			hp_data_fp32 = BF16_to_FP32(hp_data_bf16, fileds["mma.n"] * fileds["mma.k"]);
		}

		if (opmask & (1 << 7)) {  // Load LP
			lp_data_fp8 = convert_FP8(lp_data, fileds["mma.n"] * fileds["mma.k"]);
			lp_data_fp32 = FP8_to_FP32(lp_data_fp8, fileds["mma.n"] * fileds["mma.k"]);
		}

		if (opmask & (1 << 6)) {  // Load W
			w_data_fp8 = convert_FP8(w_data, fileds["mma.k"] * fileds["mma.m"]);
			w_data_fp32 = FP8_to_FP32(w_data_fp8, fileds["mma.k"] * fileds["mma.m"]);
		}

		acc_data_fp32 = acc_data_fp32 ? acc_data_fp32 : new float[fileds["mma.n"] * fileds["mma.m"]];
		for (uint32_t i = 0; i < fileds["mma.n"] * fileds["mma.m"]; ++i) {
			acc_data_fp32[i] = 0.0f;
		}
	}

	float* BF16_to_FP32(BF16* data, uint32_t length) {
		float* fp32_data = new float[length];
		for (uint32_t i = 0; i < length; ++i) {
			fp32_data[i] = data[i].toFP32();
		}
		return fp32_data;
	}

	float* FP8_to_FP32(FP8* data, uint32_t length) {
		float* fp32_data = new float[length];
		for (uint32_t i = 0; i < length; ++i) {
			fp32_data[i] = data[i].toFP32();
		}
		return fp32_data;
	}

	BF16* convert_BF16(uint16_t* data, uint32_t length) {
		BF16* bf16_data = new BF16[length];
		for (uint32_t i = 0; i < length; ++i) {
			bf16_data[i] = data[i];
		}
		return bf16_data;
	}

	FP8* convert_FP8(uint8_t* data, uint32_t length) {
		FP8* fp8_data = new FP8[length];
		for (uint32_t i = 0; i < length; ++i) {
			fp8_data[i] = data[i];
		}
		return fp8_data;
	}

	void load_data(Fileds& fileds) {
		uint32_t opmask = fileds["mma.opmask"];

		if (opmask & (1 << 8)) {  // Load HP
			hp_data = load_hp(fileds["mma.hp.addr"], fileds["mma.n"], fileds["mma.k"], fileds.hp_dtype_size,
			                  fileds["mma.hp.stride"]);
		}

		if (opmask & (1 << 7)) {  // Load LP
			lp_data = load_lp(fileds["mma.lp.addr"], fileds["mma.n"], fileds["mma.k"], fileds.lp_dtype_size,
			                  fileds["mma.lp.stride"]);
		}

		if (opmask & (1 << 6)) {  // Load Weight
			w_data = load_w(fileds["mma.w.addr"], fileds["mma.k"], fileds["mma.m"], fileds.w_dtype_size,
			                fileds["mma.w.stride"]);
		}
	}

	uint8_t* load_hp(uint32_t& src, uint32_t N, uint32_t K, uint32_t dtype_size, uint32_t stride) {
		int length = (N * K) * dtype_size;
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

	uint8_t* load_lp(uint32_t& src, uint32_t N, uint32_t K, uint32_t dtype_size, uint32_t stride) {
		int length = (N * K) * dtype_size;
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

	uint8_t* load_w(uint32_t& src, uint32_t K, uint32_t M, uint32_t dtype_size, uint32_t stride) {
		int length = (K * M) * dtype_size;
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

				// core.long_instr_complete++;
			}

			wait(10, sc_core::SC_NS);  // Simulate interval between instructions
		}
	}

	void update_resource_table(std::map<std::string, int>& resource_table) {
		if (cmd_queue.num_available() == 0) {
			resource_table["SPU"] = QueueState::EMPTY;
		} else if (cmd_queue.num_available() == 16) {
			resource_table["SPU"] = QueueState::FULL;
		} else {
			resource_table["SPU"] = QueueState::PARTIAL;
		}
	}

	bool is_queue_full() {
		return cmd_queue.num_available() >= 16;
	}

	tlm_sync_enum nb_transport_fw(tlm_generic_payload& trans, tlm_phase& phase, sc_time& delay) {
		if (phase == BEGIN_REQ) {
			Fileds* fileds = reinterpret_cast<Fileds*>(trans.get_data_ptr());

			if (is_queue_full()) {
				printf("SPU: FIFO full, cannot accept Command at %s\n", sc_time_stamp().to_string().c_str());
				return TLM_ACCEPTED;
			}

			cmd_queue.write(fileds);
			printf("SPU: Command added to FIFO at %s\n", sc_time_stamp().to_string().c_str());

			phase = END_RESP;
			return TLM_COMPLETED;
		}
		return TLM_ACCEPTED;
	}
};

#endif