#include <cstdlib>
#include <ctime>


#include "core/common/clint.h"
#include "elf_loader.h"
#include "debug_memory.h"
#include "iss.h"
#include "mem.h"
#include "memory.h"
#include "mmu.h"
#include "syscall.h"
#include "platform/common/options.h"

#include "sharedmem.h"
#include "scheduler.h"
#include "dma_ctrl.h"
#include "spu.h"
#include "ae.h"
#include "dummy.h"

#include "gdb-mc/gdb_server.h"
#include "gdb-mc/gdb_runner.h"

#include <boost/io/ios_state.hpp>
#include <boost/program_options.hpp>
#include <iomanip>
#include <iostream>

using namespace rv64;
namespace po = boost::program_options;

#define MB * 1024 * 1024

struct Runner : public sc_core::sc_module {
	sc_in_clk clock;    
	sc_in<bool> reset;  

    std::atomic<int> *nr_done;
    int nr_cores;

    SC_CTOR(Runner) : nr_done(nullptr), nr_cores(0) {
		SC_METHOD(run);
		sensitive << reset;
		sensitive << clock.pos();
	}

	void run() {
        if (!reset.read()) {
            if (*nr_done == nr_cores)
                sc_stop();
        }
	}
};

struct TinyOptions : public Options {
public:
	typedef unsigned int addr_t;

	addr_t mem_size = 1024 * 1024 * 32;  // 32 MB ram, to place it before the CLINT and run the base examples (assume
	                                     // memory start at zero) without modifications
	addr_t mem_start_addr = 0x00000000;
	addr_t mem_end_addr = mem_start_addr + mem_size - 1;
	addr_t clint_start_addr = 0x02000000;
	addr_t clint_end_addr = 0x0200ffff;
	addr_t sys_start_addr = 0x02010000;
	addr_t sys_end_addr = 0x020103ff;
	addr_t shared_mem_size = 1024 * 1024 * 1;  
	addr_t shared_mem_start_addr = 0x03000000;
	addr_t shared_mem_end_addr = 0x03100000 - 1;

	bool quiet = false;
	bool use_E_base_isa = false;

	TinyOptions(void) {
		// clang-format off
		add_options()
			("quiet", po::bool_switch(&quiet), "do not output register values on exit")
			("memory-start", po::value<unsigned int>(&mem_start_addr), "set memory start address")
			("memory-size", po::value<unsigned int>(&mem_size), "set memory size")
			("use-E-base-isa", po::bool_switch(&use_E_base_isa), "use the E instead of the I integer base ISA");
        	// clang-format on
        }

	void parse(int argc, char **argv) override {
		Options::parse(argc, argv);
		mem_end_addr = mem_start_addr + mem_size - 1;
	}
};

int sc_main(int argc, char **argv) {
	TinyOptions opt;
	opt.parse(argc, argv);

	std::srand(std::time(nullptr));  // use current time as seed for random generator

	tlm::tlm_global_quantum::instance().set(sc_core::sc_time(opt.tlm_global_quantum, sc_core::SC_NS));

	ISS core("core", 0);
	MMU mmu(core);
	CombinedMemoryInterface core_mem_if("MemoryInterface0", core, mmu);
	SimpleMemory mem("SimpleMemory", opt.mem_size);
	ELFLoader loader(opt.input_program.c_str());
	SimpleBus<2, 4> bus("SimpleBus");
	SyscallHandler sys("SyscallHandler");
	CLINT<1> clint("CLINT");
	DebugMemoryInterface dbg_if("DebugMemoryInterface");

	MemoryDMI *dmi = MemoryDMI::create_start_size_mapping(mem.data, opt.mem_start_addr, mem.size);
	InstrMemoryProxy instr_mem(*dmi, core);

	std::shared_ptr<BusLock> bus_lock = std::make_shared<BusLock>();
	core_mem_if.bus_lock = bus_lock;
	mmu.mem = &core_mem_if;

	instr_memory_if *instr_mem_if = &core_mem_if;
	data_memory_if *data_mem_if = &core_mem_if;
	if (opt.use_instr_dmi)
		instr_mem_if = &instr_mem;
	if (opt.use_data_dmi) {
		core_mem_if.dmi_ranges.emplace_back(*dmi);
	}

	loader.load_executable_image(mem.data, mem.size, opt.mem_start_addr);
	core.init(instr_mem_if, data_mem_if, &clint, loader.get_entrypoint(), rv64_align_address(opt.mem_end_addr));
	sys.init(mem.data, opt.mem_start_addr, loader.get_heap_addr());
	sys.register_core(&core);

	if (opt.intercept_syscalls)
		core.sys = &sys;

	// setup port mapping
	bus.ports[0] = new PortMapping(opt.mem_start_addr, opt.mem_end_addr);
	bus.ports[1] = new PortMapping(opt.clint_start_addr, opt.clint_end_addr);
	bus.ports[2] = new PortMapping(opt.sys_start_addr, opt.sys_end_addr);
	bus.ports[3] = new PortMapping(opt.shared_mem_start_addr , opt.shared_mem_end_addr);

	// connect TLM sockets
	core_mem_if.isock.bind(bus.tsocks[0]);
	dbg_if.isock.bind(bus.tsocks[1]);
	bus.isocks[0].bind(mem.tsock);
	bus.isocks[1].bind(clint.tsock);
	bus.isocks[2].bind(sys.tsock);

	// connect interrupt signals/communication
	clint.target_harts[0] = &core;

	// switch for printing instructions
	core.trace = opt.trace_mode;

    // engine
    SharedMemory<4> *sharedmem = new SharedMemory<4>("SharedMemory", 1 MB);
    Scheduler *scheduler = new Scheduler("Scheduler");
    DMACTRL *dma_ctrl = new DMACTRL("DMACTRL");
    AE *ae = new AE("AE");
    SPU *spu = new SPU("SPU");
	Dummy *dummy = new Dummy("Dummy");

    bus.isocks[3].bind(sharedmem->tsocks[0]);
	core.isock.bind(scheduler->tsock);
    scheduler->dma_isock.bind(dma_ctrl->tsock);
    scheduler->ae_isock.bind(ae->tsock);
    scheduler->spu_isock.bind(spu->tsock);
    dma_ctrl->isock.bind(sharedmem->tsocks[1]);
    ae->isock.bind(sharedmem->tsocks[2]);
    spu->isock.bind(sharedmem->tsocks[3]);

	dma_ctrl->local_isock.bind(dummy->tsock);
	dummy->isock.bind(dma_ctrl->local_tsock);

    dma_ctrl->long_instr_complete = &(core.long_instr_complete);
    ae->long_instr_complete = &(core.long_instr_complete);
    spu->long_instr_complete = &(core.long_instr_complete);

	std::vector<debug_target_if *> threads;
	threads.push_back(&core);

	// if (opt.use_debug_runner) {
	// 	auto server = new GDBServer("GDBServer", threads, &dbg_if, opt.debug_port);
	// 	new GDBServerRunner("GDBRunner", server, &core);
	// } else {
	// 	new DirectCoreRunner(core);
	// }

    // Signals
    sc_clock clock("clock", 1000, SC_PS); // 1000 ps == 1 ns
    sc_signal <bool> reset;

    std::atomic<int> nr_done;
    nr_done = 0;

	core.clock(clock);
	core.reset(reset);
	core.nr_done = &nr_done;

	dma_ctrl->clock(clock);
	dma_ctrl->reset(reset);

    // Runner instance
    Runner runner("runnner");
    runner.clock(clock);
    runner.reset(reset);
    runner.nr_done = &nr_done;
    runner.nr_cores = 1;

    // Reset the chip and run the simulation
    reset.write(1);
    std::cout << "Reset for " << 1000 << " cycles... " << std::endl; 
    sc_start(100 * 1000, SC_PS);

    reset.write(0);
    std::cout << " done! " << std::endl;
    std::cout << " Now running until core done " << std::endl;

	if (opt.quiet)
		sc_core::sc_report_handler::set_verbosity_level(sc_core::SC_NONE);

	sc_core::sc_start();
	if (!opt.quiet) {
		core.show();
	}

	return 0;
}
