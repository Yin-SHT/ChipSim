#ifndef RISCV_VP_DMA_CTRL_H
#define RISCV_VP_DMA_CTRL_H

#include <stdint.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

#include <systemc>
#include <vector>
#include <string>

using namespace sc_core;
using namespace tlm;

class DMACTRL : public sc_module {
   public:
	tlm_utils::simple_target_socket<DMACTRL> local_tsock;
	tlm_utils::simple_initiator_socket<DMACTRL> local_isock;

	// reserve data from Router
	std::vector<uint8_t> router_data_buffer;
	sc_event router_data_ready;

	// 定时发送相关变量
	sc_event send_hello_event;
	int send_counter;
	sc_time send_interval;  // 发送间隔时间

	SC_CTOR(DMACTRL) {
		local_tsock.register_b_transport(this, &DMACTRL::router_b_transport);

		SC_THREAD(process_router_data);
		SC_THREAD(send_hello_world);
		
		// 初始化发送相关变量
		send_counter = 0;
		send_interval = sc_time(10, SC_NS);  // 每1000ns发送一次
	}

	void router_b_transport(tlm_generic_payload& trans, sc_time& delay) {
		uint8_t* data_ptr = trans.get_data_ptr();
		unsigned int data_len = trans.get_data_length();

		router_data_buffer.insert(router_data_buffer.end(), data_ptr, data_ptr + data_len);

		trans.set_response_status(TLM_OK_RESPONSE);

		router_data_ready.notify();

		printf("DMA: 从router接收到 %u 字节数据，当前缓冲区大小: %zu\n", data_len, router_data_buffer.size());
	}

	void process_router_data() {
		while (true) {
			wait(router_data_ready);

			if (!router_data_buffer.empty()) {
				printf("DMA: 处理router数据, 大小: %zu 字节\n", router_data_buffer.size());

				router_data_buffer.push_back(0);

                printf("%s: %s\n", name(), router_data_buffer.data());

				router_data_buffer.clear();
			}
		}
	}

	void send_hello_world() {
		while (true) {
			wait(send_interval);

			// 生成随机字节
			uint8_t random_byte = rand() % 16;
			
			// 构建消息
			std::string message = std::to_string(send_counter++) + "th Hello World";
			
			// 创建包含随机字节和消息的缓冲区
			std::vector<uint8_t> buffer;
			buffer.push_back(random_byte);  // 添加随机字节
			buffer.insert(buffer.end(), message.begin(), message.end());  // 添加消息内容
			
			tlm_generic_payload trans;
			sc_time delay = SC_ZERO_TIME;
			
			trans.set_command(TLM_WRITE_COMMAND);
			trans.set_data_ptr(buffer.data());
			trans.set_data_length(buffer.size());
			trans.set_response_status(TLM_INCOMPLETE_RESPONSE);
			
			local_isock->b_transport(trans, delay);
			
			if (trans.get_response_status() == TLM_OK_RESPONSE) {
				printf("DMA: 成功发送消息: [0x%02X] %s\n", random_byte, message.c_str());
			} else {
				printf("DMA: 发送消息失败\n");
			}
		}
	}
};

#endif