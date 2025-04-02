#ifndef RISCV_SHAREDMEMORY_H
#define RISCV_SHAREDMEMORY_H

#include <stdint.h>
#include <tlm_utils/simple_target_socket.h>

#include <systemc>

template <unsigned int NR_OF_INITIATORS>
struct SharedMemory : public sc_core::sc_module {
	std::array<tlm_utils::simple_target_socket<SharedMemory>, NR_OF_INITIATORS> tsocks;

	uint8_t *data;
	uint32_t size;

	SharedMemory(sc_core::sc_module_name, uint32_t size) : data(new uint8_t[size]()), size(size) {
		for (auto &s : tsocks) {
			s.register_b_transport(this, &SharedMemory::transport);
		}
	}

	~SharedMemory(void) {
		delete[] data;
	}

	void write_data(unsigned addr, const uint8_t *src, unsigned num_bytes) {
		assert(addr + num_bytes <= size);

		memcpy(data + addr, src, num_bytes);
	}

	void read_data(unsigned addr, uint8_t *dst, unsigned num_bytes) {
		assert(addr + num_bytes <= size);

		memcpy(dst, data + addr, num_bytes);
	}

	void transport(tlm::tlm_generic_payload &trans, sc_core::sc_time &delay) {
		tlm::tlm_command cmd = trans.get_command();
		unsigned addr = trans.get_address();
		auto *ptr = trans.get_data_ptr();
		auto len = trans.get_data_length();

		assert(addr < size);

		if (cmd == tlm::TLM_WRITE_COMMAND) {
			write_data(addr, ptr, len);
		} else if (cmd == tlm::TLM_READ_COMMAND) {
			read_data(addr, ptr, len);
		} else {
			sc_assert(false && "unsupported tlm command");
		}

		delay += sc_core::sc_time(10, sc_core::SC_NS);
	}
};

#endif
