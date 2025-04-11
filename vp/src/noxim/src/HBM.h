/**
 * @file hbm.h
 * @brief High Bandwidth Memory (HBM) module implementation using SystemC TLM2.0
 *
 * This HBM module features:
 * - 16 channels that can be accessed in parallel
 * - Configurable memory interleaving
 * - 64-bit read/write data granularity
 * - Support for concurrent access with synchronization mechanisms
 */

#ifndef HBM_H
#define HBM_H

#include <systemc>
#include <tlm.h>
#include <tlm_utils/simple_target_socket.h>
#include <tlm_utils/peq_with_cb_and_phase.h>
#include <vector>
#include <map>
#include <mutex>
#include <memory>
#include <atomic>

class HBM : public sc_core::sc_module {
public:
    // TLM-2.0 socket, one for each channel (16 channels total)
    std::vector<tlm_utils::simple_target_socket<HBM>> targ_socket;
    
    // Constructor
    SC_HAS_PROCESS(HBM);
    HBM(sc_core::sc_module_name name, 
        uint64_t memory_size = 8 * 1024 * 1024 * 1024ULL,  // 8GB default
        uint32_t interleave_size = 256,                    // 256 bytes default
        uint32_t num_channels = 16)                        // 16 channels default
    : sc_core::sc_module(name),
      m_memory_size(memory_size),
      m_interleave_size(interleave_size),
      m_num_channels(num_channels)
    {
        // Initialize the channels and target sockets
        targ_socket.resize(m_num_channels);
        
        // Register callbacks for each socket
        for (uint32_t i = 0; i < m_num_channels; i++) {
            targ_socket[i].register_b_transport(this, &HBM::b_transport);
        }
        
        // Allocate memory for each channel
        uint64_t channel_size = m_memory_size / m_num_channels;
        m_channel_data.resize(m_num_channels);
        
        for (uint32_t i = 0; i < m_num_channels; i++) {
            m_channel_data[i] = new uint8_t[channel_size];
            memset(m_channel_data[i], 0, channel_size);
        }
        
        // Initialize access locks for each interleave block
        uint64_t num_interleave_blocks = (m_memory_size / m_interleave_size) + 1;
        for (uint64_t i = 0; i < num_interleave_blocks; i++) {
            m_block_read_count[i] = 0;
            m_block_write_lock[i] = false;
        }
        
        // Statistics
        m_read_count = 0;
        m_write_count = 0;
        m_read_conflicts = 0;
        m_write_conflicts = 0;
    }
    
    // Destructor
    ~HBM() {
        for (auto& data : m_channel_data) {
            delete[] data;
        }
    }
    
    // Print statistics
    void print_stats() {
        std::cout << "HBM Statistics:" << std::endl;
        std::cout << "  Total Reads: " << m_read_count << std::endl;
        std::cout << "  Total Writes: " << m_write_count << std::endl;
        std::cout << "  Read Conflicts: " << m_read_conflicts << std::endl;
        std::cout << "  Write Conflicts: " << m_write_conflicts << std::endl;
    }
    
private:
    // Memory size and parameters
    uint64_t m_memory_size;      // Total memory size in bytes
    uint32_t m_interleave_size;  // Interleave block size in bytes
    uint32_t m_num_channels;     // Number of HBM channels
    
    // Memory storage (one for each channel)
    std::vector<uint8_t*> m_channel_data;
    
    // Synchronization for memory blocks
    std::map<uint64_t, std::atomic<int>> m_block_read_count;  // Number of current readers per block
    std::map<uint64_t, std::atomic<bool>> m_block_write_lock; // Write lock per block
    std::mutex m_channel_mutex;  // Mutex for channel access coordination
    
    // Statistics
    std::atomic<uint64_t> m_read_count;
    std::atomic<uint64_t> m_write_count;
    std::atomic<uint64_t> m_read_conflicts;
    std::atomic<uint64_t> m_write_conflicts;
    
    // Calculate which channel and offset within the channel for a given address
    void map_address(const uint64_t addr, uint32_t& channel, uint64_t& offset) {
        // Determine interleave block index
        uint64_t block_index = addr / m_interleave_size;
        
        // Determine channel using round-robin assignment of blocks
        channel = block_index % m_num_channels;
        
        // Calculate offset within the channel
        uint64_t channel_block_index = block_index / m_num_channels;
        uint64_t block_offset = addr % m_interleave_size;
        offset = (channel_block_index * m_interleave_size) + block_offset;
    }
    
    // Get the block index for a given address
    uint64_t get_block_index(const uint64_t addr) {
        return addr / m_interleave_size;
    }
    
    // TLM-2.0 blocking transport method
    void b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay) {
        tlm::tlm_command cmd = trans.get_command();
        uint64_t addr = trans.get_address();
        unsigned char* data_ptr = trans.get_data_ptr();
        unsigned int data_length = trans.get_data_length();
        
        // Map address to channel and offset
        uint32_t channel;
        uint64_t offset;
        map_address(addr, channel, offset);
        
        // Get block index for synchronization
        uint64_t block_index = get_block_index(addr);
        
        // Check if address is valid
        if (offset + data_length > (m_memory_size / m_num_channels)) {
            trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        
        // Process read/write command
        if (cmd == tlm::TLM_READ_COMMAND) {
            // Lock for reading
            acquire_read_lock(block_index);
            
            // Perform read from the mapped channel
            memcpy(data_ptr, &m_channel_data[channel][offset], data_length);
            
            // Release read lock
            release_read_lock(block_index);
            
            m_read_count++;
            
            // Set response status
            trans.set_response_status(tlm::TLM_OK_RESPONSE);
            
            // Add memory read latency
            delay += sc_core::sc_time(30, sc_core::SC_NS);
        }
        else if (cmd == tlm::TLM_WRITE_COMMAND) {
            // Lock for writing
            bool acquired = acquire_write_lock(block_index);
            
            if (acquired) {
                // Perform write to the mapped channel
                memcpy(&m_channel_data[channel][offset], data_ptr, data_length);
                
                // Release write lock
                release_write_lock(block_index);
                
                m_write_count++;
                
                // Set response status
                trans.set_response_status(tlm::TLM_OK_RESPONSE);
                
                // Add memory write latency
                delay += sc_core::sc_time(30, sc_core::SC_NS);
            }
            else {
                // Write conflict, transaction needs to be retried
                m_write_conflicts++;
                trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
                
                // Schedule retry after some delay
                sc_core::sc_time retry_delay = delay + sc_core::sc_time(10, sc_core::SC_NS);
            }
        }
        else {
            trans.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
        }
    }
    
    // Acquire read lock for a memory block
    void acquire_read_lock(uint64_t block_index) {
        // Wait until there's no writer
        while (m_block_write_lock[block_index].load()) {
            m_read_conflicts++;
            sc_core::wait(sc_core::SC_ZERO_TIME);
        }
        
        // Increment read count
        m_block_read_count[block_index]++;
    }
    
    // Release read lock for a memory block
    void release_read_lock(uint64_t block_index) {
        m_block_read_count[block_index]--;
    }
    
    // Acquire write lock for a memory block
    bool acquire_write_lock(uint64_t block_index) {
        // Check if any readers or another writer
        if (m_block_read_count[block_index].load() > 0 || 
            m_block_write_lock[block_index].exchange(true)) {
            return false;
        }
        return true;
    }
    
    // Release write lock for a memory block
    void release_write_lock(uint64_t block_index) {
        m_block_write_lock[block_index] = false;
    }
};

#endif // HBM_H