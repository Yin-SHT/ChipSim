/*
 * Noxim - the NoC Simulator
 *
 * (C) 2005-2018 by the University of Catania
 * For the complete list of authors refer to file ../doc/AUTHORS.txt
 * For the license applied to these sources refer to file ../doc/LICENSE.txt
 *
 * This file contains the implementation of the processing element
 */

#include <math.h>
#include <algorithm>  // 添加 algorithm 头文件

#include "ProcessingElement.h"

int ProcessingElement::randInt(int min, int max) {
	return min + (int)((double)(max - min + 1) * rand() / (RAND_MAX + 1.0));
}

void ProcessingElement::rxProcess() {
	if (reset.read()) {
		ack_rx.write(0);
		current_level_rx = 0;
		packet_buffer.clear();
		current_packet_size = 0;
		receiving_packet = false;
	} else {
		if (req_rx.read() == 1 - current_level_rx) {
			Flit flit_tmp = flit_rx.read();
			
			// 处理接收到的 flit
			if (flit_tmp.flit_type == FLIT_TYPE_HEAD) {
				// 开始新的数据包
				packet_buffer.clear();
				current_packet_size = flit_tmp.sequence_length;
				receiving_packet = true;
				printf("PE[%d]: 开始接收新数据包，大小: %d\n", local_id, current_packet_size);
			}
			
			if (receiving_packet) {
				// 将 payload 添加到缓冲区
				uint8_t* payload_ptr = flit_tmp.payload.data_array;
				packet_buffer.insert(packet_buffer.end(), payload_ptr, payload_ptr + FLIT_SIZE);
				
				// 如果是 tail flit，发送整个数据包
				if (flit_tmp.flit_type == FLIT_TYPE_TAIL) {
					printf("PE[%d]: 接收到 tail flit, 准备发送数据包, 大小: %zu 字节\n", 
						local_id, packet_buffer.size());
					
					// 创建 TLM 传输
					tlm_generic_payload trans;
					sc_time delay = SC_ZERO_TIME;
					
					// 设置传输参数
					trans.set_command(TLM_WRITE_COMMAND);
					trans.set_data_ptr(packet_buffer.data());
					trans.set_data_length(packet_buffer.size());
					trans.set_response_status(TLM_INCOMPLETE_RESPONSE);
					
					// 发送数据到 DMA 控制器
					local_isock->b_transport(trans, delay);
					
					if (trans.get_response_status() == TLM_OK_RESPONSE) {
						printf("PE[%d]: 成功发送数据包到 DMA 控制器\n", local_id);
					} else {
						printf("PE[%d]: 发送数据包到 DMA 控制器失败\n", local_id);
					}
					
					// 重置接收状态
					receiving_packet = false;
					packet_buffer.clear();
					current_packet_size = 0;
				}
			}
			
			current_level_rx = 1 - current_level_rx;  // Negate the old value for Alternating Bit Protocol (ABP)
		} 
		ack_rx.write(current_level_rx);
	}
}

void ProcessingElement::dma_b_transport(tlm_generic_payload& trans, sc_time& delay) {
	uint8_t* data_ptr = trans.get_data_ptr();
	unsigned int data_len = trans.get_data_length();

    // extract dst_id
	int dst_id = data_ptr[0];

    // create packet
	Packet packet;
	packet.make(local_id, dst_id, 0, sc_time_stamp().to_double() / GlobalParams::clock_period_ps, 
					   (data_len - FLIT_SIZE) / FLIT_SIZE, data_ptr + FLIT_SIZE, data_len - FLIT_SIZE); // TODO: 需要修改

    // push packet to packet_queue
    packet_queue.push(packet);
    
    printf("PE[%d]: 创建新数据包，目标节点: %d, 大小: %d flits\n", 
            local_id, dst_id, packet.size);
	
	trans.set_response_status(TLM_OK_RESPONSE);
}

void ProcessingElement::txProcess() {
	if (reset.read()) {
		req_tx.write(0);
		current_level_tx = 0;
		transmittedAtPreviousCycle = false;
	} else {
		if (ack_tx.read() == current_level_tx) {
			if (!packet_queue.empty()) {
				Flit flit = nextFlit();                   // Generate a new flit
				flit_tx->write(flit);                     // Send the generated flit
				current_level_tx = 1 - current_level_tx;  // Negate the old value for Alternating Bit Protocol (ABP)
				req_tx.write(current_level_tx);
			}
		}
	}
}

Flit ProcessingElement::nextFlit() {
	Flit flit;
	Packet packet = packet_queue.front();

	flit.src_id = packet.src_id;
	flit.dst_id = packet.dst_id;
	flit.vc_id = packet.vc_id;
	flit.timestamp = packet.timestamp;
	flit.sequence_no = packet.size - packet.flit_left;
	flit.sequence_length = packet.size;
	flit.hop_no = 0;

	if (packet.payload != nullptr) {
		int start_idx = (packet.size - packet.flit_left) * FLIT_SIZE;
		int end_idx = start_idx + FLIT_SIZE > packet.size * FLIT_SIZE ? packet.size * FLIT_SIZE : start_idx + FLIT_SIZE;
		
		size_t copy_size = (end_idx - start_idx) < FLIT_SIZE ? 
						  (end_idx - start_idx) : FLIT_SIZE;

		memset(&flit.payload, 0, FLIT_SIZE);  // 清空 payload
		memcpy(&flit.payload, packet.payload + start_idx, copy_size);
	}

	flit.hub_relay_node = NOT_VALID;

	if (packet.size == packet.flit_left)
		flit.flit_type = FLIT_TYPE_HEAD;
	else if (packet.flit_left == 1)
		flit.flit_type = FLIT_TYPE_TAIL;
	else
		flit.flit_type = FLIT_TYPE_BODY;

	packet_queue.front().flit_left--;
	if (packet_queue.front().flit_left == 0) {
		if (packet_queue.front().payload != nullptr) {
			delete[] packet_queue.front().payload;
		}
		packet_queue.pop();
	}

	return flit;
}

bool ProcessingElement::canShot(Packet &packet) {
	// assert(false);
	if (never_transmit)
		return false;

	// if(local_id!=16) return false;
	/* DEADLOCK TEST
	double current_time = sc_time_stamp().to_double() / GlobalParams::clock_period_ps;

	if (current_time >= 4100)
	{
	    //if (current_time==3500)
	         //cout << name() << " IN CODA " << packet_queue.size() << endl;
	    return false;
	}
	//*/

#ifdef DEADLOCK_AVOIDANCE
	if (local_id % 2 == 0)
		return false;
#endif
	bool shot;
	double threshold;

	double now = sc_time_stamp().to_double() / GlobalParams::clock_period_ps;

	if (GlobalParams::traffic_distribution != TRAFFIC_TABLE_BASED) {
		if (!transmittedAtPreviousCycle)
			threshold = GlobalParams::packet_injection_rate;
		else
			threshold = GlobalParams::probability_of_retransmission;

		shot = (((double)rand()) / RAND_MAX < threshold);
		if (shot) {
			if (GlobalParams::traffic_distribution == TRAFFIC_RANDOM)
				packet = trafficRandom();
			else if (GlobalParams::traffic_distribution == TRAFFIC_TRANSPOSE1)
				packet = trafficTranspose1();
			else if (GlobalParams::traffic_distribution == TRAFFIC_TRANSPOSE2)
				packet = trafficTranspose2();
			else if (GlobalParams::traffic_distribution == TRAFFIC_BIT_REVERSAL)
				packet = trafficBitReversal();
			else if (GlobalParams::traffic_distribution == TRAFFIC_SHUFFLE)
				packet = trafficShuffle();
			else if (GlobalParams::traffic_distribution == TRAFFIC_BUTTERFLY)
				packet = trafficButterfly();
			else if (GlobalParams::traffic_distribution == TRAFFIC_LOCAL)
				packet = trafficLocal();
			else if (GlobalParams::traffic_distribution == TRAFFIC_ULOCAL)
				packet = trafficULocal();
			else {
				cout << "Invalid traffic distribution: " << GlobalParams::traffic_distribution << endl;
				exit(-1);
			}
		}
	} else {  // Table based communication traffic
		if (never_transmit)
			return false;

		bool use_pir = (transmittedAtPreviousCycle == false);
		vector<pair<int, double> > dst_prob;
		double threshold = traffic_table->getCumulativePirPor(local_id, (int)now, use_pir, dst_prob);

		double prob = (double)rand() / RAND_MAX;
		shot = (prob < threshold);
		if (shot) {
			for (unsigned int i = 0; i < dst_prob.size(); i++) {
				if (prob < dst_prob[i].second) {
					int vc = randInt(0, GlobalParams::n_virtual_channels - 1);
					packet.make(local_id, dst_prob[i].first, vc, now, getRandomSize());
					break;
				}
			}
		}
	}

	return shot;
}

Packet ProcessingElement::trafficLocal() {
	Packet p;
	p.src_id = local_id;
	double rnd = rand() / (double)RAND_MAX;

	vector<int> dst_set;

	int max_id = (GlobalParams::mesh_dim_x * GlobalParams::mesh_dim_y);

	for (int i = 0; i < max_id; i++) {
		if (rnd <= GlobalParams::locality) {
			if (local_id != i && sameRadioHub(local_id, i))
				dst_set.push_back(i);
		} else if (!sameRadioHub(local_id, i))
			dst_set.push_back(i);
	}

	int i_rnd = rand() % dst_set.size();

	p.dst_id = dst_set[i_rnd];
	p.timestamp = sc_time_stamp().to_double() / GlobalParams::clock_period_ps;
	p.size = p.flit_left = getRandomSize();
	p.vc_id = randInt(0, GlobalParams::n_virtual_channels - 1);

	return p;
}

int ProcessingElement::findRandomDestination(int id, int hops) {
	assert(GlobalParams::topology == TOPOLOGY_MESH);

	int inc_y = rand() % 2 ? -1 : 1;
	int inc_x = rand() % 2 ? -1 : 1;

	Coord current = id2Coord(id);

	for (int h = 0; h < hops; h++) {
		if (current.x == 0)
			if (inc_x < 0)
				inc_x = 0;

		if (current.x == GlobalParams::mesh_dim_x - 1)
			if (inc_x > 0)
				inc_x = 0;

		if (current.y == 0)
			if (inc_y < 0)
				inc_y = 0;

		if (current.y == GlobalParams::mesh_dim_y - 1)
			if (inc_y > 0)
				inc_y = 0;

		if (rand() % 2)
			current.x += inc_x;
		else
			current.y += inc_y;
	}
	return coord2Id(current);
}

int roulette() {
	int slices = GlobalParams::mesh_dim_x + GlobalParams::mesh_dim_y - 2;

	double r = rand() / (double)RAND_MAX;

	for (int i = 1; i <= slices; i++) {
		if (r < (1 - 1 / double(2 << i))) {
			return i;
		}
	}
	assert(false);
	return 1;
}

Packet ProcessingElement::trafficULocal() {
	Packet p;
	p.src_id = local_id;

	int target_hops = roulette();

	p.dst_id = findRandomDestination(local_id, target_hops);

	p.timestamp = sc_time_stamp().to_double() / GlobalParams::clock_period_ps;
	p.size = p.flit_left = getRandomSize();
	p.vc_id = randInt(0, GlobalParams::n_virtual_channels - 1);

	return p;
}

Packet ProcessingElement::trafficRandom() {
	Packet p;
	p.src_id = local_id;
	double rnd = rand() / (double)RAND_MAX;
	double range_start = 0.0;
	int max_id;

	if (GlobalParams::topology == TOPOLOGY_MESH)
		max_id = (GlobalParams::mesh_dim_x * GlobalParams::mesh_dim_y) - 1;  // Mesh
	else                                                                     // other delta topologies
		max_id = GlobalParams::n_delta_tiles - 1;

	// Random destination distribution
	do {
		p.dst_id = randInt(0, max_id);

		// check for hotspot destination
		for (size_t i = 0; i < GlobalParams::hotspots.size(); i++) {
			if (rnd >= range_start && rnd < range_start + GlobalParams::hotspots[i].second) {
				if (local_id != GlobalParams::hotspots[i].first) {
					p.dst_id = GlobalParams::hotspots[i].first;
				}
				break;
			} else
				range_start += GlobalParams::hotspots[i].second;  // try next
		}
#ifdef DEADLOCK_AVOIDANCE
		assert((GlobalParams::topology == TOPOLOGY_MESH));
		if (p.dst_id % 2 != 0) {
			p.dst_id = (p.dst_id + 1) % 256;
		}
#endif

	} while (p.dst_id == p.src_id);

	p.timestamp = sc_time_stamp().to_double() / GlobalParams::clock_period_ps;
	p.size = p.flit_left = getRandomSize();
	p.vc_id = randInt(0, GlobalParams::n_virtual_channels - 1);

	return p;
}
// TODO: for testing only
Packet ProcessingElement::trafficTest() {
	Packet p;
	p.src_id = local_id;
	p.dst_id = 10;

	p.timestamp = sc_time_stamp().to_double() / GlobalParams::clock_period_ps;
	p.size = p.flit_left = getRandomSize();
	p.vc_id = randInt(0, GlobalParams::n_virtual_channels - 1);

	return p;
}

Packet ProcessingElement::trafficTranspose1() {
	assert(GlobalParams::topology == TOPOLOGY_MESH);
	Packet p;
	p.src_id = local_id;
	Coord src, dst;

	// Transpose 1 destination distribution
	src.x = id2Coord(p.src_id).x;
	src.y = id2Coord(p.src_id).y;
	dst.x = GlobalParams::mesh_dim_x - 1 - src.y;
	dst.y = GlobalParams::mesh_dim_y - 1 - src.x;
	fixRanges(src, dst);
	p.dst_id = coord2Id(dst);

	p.vc_id = randInt(0, GlobalParams::n_virtual_channels - 1);
	p.timestamp = sc_time_stamp().to_double() / GlobalParams::clock_period_ps;
	p.size = p.flit_left = getRandomSize();

	return p;
}

Packet ProcessingElement::trafficTranspose2() {
	assert(GlobalParams::topology == TOPOLOGY_MESH);
	Packet p;
	p.src_id = local_id;
	Coord src, dst;

	// Transpose 2 destination distribution
	src.x = id2Coord(p.src_id).x;
	src.y = id2Coord(p.src_id).y;
	dst.x = src.y;
	dst.y = src.x;
	fixRanges(src, dst);
	p.dst_id = coord2Id(dst);

	p.vc_id = randInt(0, GlobalParams::n_virtual_channels - 1);
	p.timestamp = sc_time_stamp().to_double() / GlobalParams::clock_period_ps;
	p.size = p.flit_left = getRandomSize();

	return p;
}

void ProcessingElement::setBit(int &x, int w, int v) {
	int mask = 1 << w;

	if (v == 1)
		x = x | mask;
	else if (v == 0)
		x = x & ~mask;
	else
		assert(false);
}

int ProcessingElement::getBit(int x, int w) {
	return (x >> w) & 1;
}

inline double ProcessingElement::log2ceil(double x) {
	return ceil(log(x) / log(2.0));
}

Packet ProcessingElement::trafficBitReversal() {
	int nbits = (int)log2ceil((double)(GlobalParams::mesh_dim_x * GlobalParams::mesh_dim_y));
	int dnode = 0;
	for (int i = 0; i < nbits; i++) setBit(dnode, i, getBit(local_id, nbits - i - 1));

	Packet p;
	p.src_id = local_id;
	p.dst_id = dnode;

	p.vc_id = randInt(0, GlobalParams::n_virtual_channels - 1);
	p.timestamp = sc_time_stamp().to_double() / GlobalParams::clock_period_ps;
	p.size = p.flit_left = getRandomSize();

	return p;
}

Packet ProcessingElement::trafficShuffle() {
	int nbits = (int)log2ceil((double)(GlobalParams::mesh_dim_x * GlobalParams::mesh_dim_y));
	int dnode = 0;
	for (int i = 0; i < nbits - 1; i++) setBit(dnode, i + 1, getBit(local_id, i));
	setBit(dnode, 0, getBit(local_id, nbits - 1));

	Packet p;
	p.src_id = local_id;
	p.dst_id = dnode;

	p.vc_id = randInt(0, GlobalParams::n_virtual_channels - 1);
	p.timestamp = sc_time_stamp().to_double() / GlobalParams::clock_period_ps;
	p.size = p.flit_left = getRandomSize();

	return p;
}

Packet ProcessingElement::trafficButterfly() {
	int nbits = (int)log2ceil((double)(GlobalParams::mesh_dim_x * GlobalParams::mesh_dim_y));
	int dnode = 0;
	for (int i = 1; i < nbits - 1; i++) setBit(dnode, i, getBit(local_id, i));
	setBit(dnode, 0, getBit(local_id, nbits - 1));
	setBit(dnode, nbits - 1, getBit(local_id, 0));

	Packet p;
	p.src_id = local_id;
	p.dst_id = dnode;

	p.vc_id = randInt(0, GlobalParams::n_virtual_channels - 1);
	p.timestamp = sc_time_stamp().to_double() / GlobalParams::clock_period_ps;
	p.size = p.flit_left = getRandomSize();

	return p;
}

void ProcessingElement::fixRanges(const Coord src, Coord &dst) {
	// Fix ranges
	if (dst.x < 0)
		dst.x = 0;
	if (dst.y < 0)
		dst.y = 0;
	if (dst.x >= GlobalParams::mesh_dim_x)
		dst.x = GlobalParams::mesh_dim_x - 1;
	if (dst.y >= GlobalParams::mesh_dim_y)
		dst.y = GlobalParams::mesh_dim_y - 1;
}

int ProcessingElement::getRandomSize() {
	return randInt(GlobalParams::min_packet_size, GlobalParams::max_packet_size);
}

unsigned int ProcessingElement::getQueueSize() const {
	return packet_queue.size();
}
