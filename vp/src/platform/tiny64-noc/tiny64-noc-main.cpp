/*
 * Noxim - the NoC Simulator
 *
 * (C) 2005-2018 by the University of Catania
 * For the complete list of authors refer to file ../doc/AUTHORS.txt
 * For the license applied to these sources refer to file ../doc/LICENSE.txt
 *
 * This file contains the implementation of the top-level of Noxim
 */

#include "ConfigurationManager.h"
#include "NoC.h"
#include "GlobalStats.h"
#include "DataStructs.h"
#include "GlobalParams.h"

#include "dma_ctrl.h"

#include <csignal>
#include <cstdlib>
#include <ctime>

#include "platform/common/bus.h"
#include "platform/common/memory.h"
#include "core/common/clint.h"
#include "core/rv64/elf_loader.h"
#include "elf_loader.h"
#include "debug_memory.h"
#include "iss.h"
#include "mem.h"
#include "memory.h"
#include "mmu.h"
#include "syscall.h"

using namespace std;
using namespace rv64;

// need to be globally visible to allow "-volume" simulation stop
unsigned int drained_volume;
NoC *n;

void signalHandler( int signum )
{
    cout << "\b\b  " << endl;
    cout << endl;
    cout << "Current Statistics:" << endl;
    cout << "(" << sc_time_stamp().to_double() / GlobalParams::clock_period_ps << " sim cycles executed)" << endl;
    GlobalStats gs(n);
    gs.showStats(std::cout, GlobalParams::detailed);
}

void config_core(ISS &core, int i, int j) {
	MMU mmu(core);
	CombinedMemoryInterface core_mem_if("MemoryInterface0", core, mmu);
	SimpleMemory mem("SimpleMemory", GlobalParams::mem_size);
	ELFLoader loader(GlobalParams::elf.c_str());
	SimpleBus<2, 3> bus("SimpleBus");
	SyscallHandler sys("SyscallHandler");
	CLINT<1> clint((std::string("CLINT") + to_string(i) + "_" + to_string(j)).c_str());
	DebugMemoryInterface dbg_if("DebugMemoryInterface");

	MemoryDMI dmi = MemoryDMI::create_start_size_mapping(mem.data, GlobalParams::mem_start_addr, mem.size);
	InstrMemoryProxy instr_mem(dmi, core);

	std::shared_ptr<BusLock> bus_lock = std::make_shared<BusLock>();
	core_mem_if.bus_lock = bus_lock;
	mmu.mem = &core_mem_if;

	instr_memory_if *instr_mem_if = &core_mem_if;
	data_memory_if *data_mem_if = &core_mem_if;
	if (GlobalParams::use_instr_dmi)
		instr_mem_if = &instr_mem;
	if (GlobalParams::use_data_dmi) {
		core_mem_if.dmi_ranges.emplace_back(dmi);
	}

	loader.load_executable_image(mem.data, mem.size, GlobalParams::mem_start_addr);
	core.init(instr_mem_if, data_mem_if, &clint, loader.get_entrypoint(), rv64_align_address(GlobalParams::mem_end_addr));
	sys.init(mem.data, GlobalParams::mem_start_addr, loader.get_heap_addr());
	sys.register_core(&core);

	if (GlobalParams::intercept_syscalls)
		core.sys = &sys;

	// setup port mapping
	bus.ports[0] = new PortMapping(GlobalParams::mem_start_addr, GlobalParams::mem_end_addr);
	bus.ports[1] = new PortMapping(GlobalParams::clint_start_addr, GlobalParams::clint_end_addr);
	bus.ports[2] = new PortMapping(GlobalParams::sys_start_addr, GlobalParams::sys_end_addr);

	// connect TLM sockets
	core_mem_if.isock.bind(bus.tsocks[0]);
	dbg_if.isock.bind(bus.tsocks[1]);
	bus.isocks[0].bind(mem.tsock);
	bus.isocks[1].bind(clint.tsock);
	bus.isocks[2].bind(sys.tsock);

	// connect interrupt signals/communication
	clint.target_harts[0] = &core;

	// switch for printing instructions
	core.trace = GlobalParams::pe_trace_mode;

    // engine
    std::string dma_name = std::string("dma_ctrl[") + to_string(i) + "," + to_string(j) + std::string("]");
    DMACTRL *dma_ctrl = new DMACTRL(dma_name.c_str());

    core.dma_ctrl = dma_ctrl;
}

int sc_main(int arg_num, char *arg_vet[])
{
    signal(SIGQUIT, signalHandler);  

    // TEMP
    drained_volume = 0;

    // Handle command-line arguments
    cout << "\t--------------------------------------------" << endl; 
    cout << "\t\tNoxim - the NoC Simulator" << endl;
    cout << "\t\t(C) University of Catania" << endl;
    cout << "\t--------------------------------------------" << endl; 

    cout << "Catania V., Mineo A., Monteleone S., Palesi M., and Patti D. (2016) Cycle-Accurate Network on Chip Simulation with Noxim. ACM Trans. Model. Comput. Simul. 27, 1, Article 4 (August 2016), 25 pages. DOI: https://doi.org/10.1145/2953878" << endl;
    cout << endl;
    cout << endl;

    configure(arg_num, arg_vet);

	std::srand(std::time(nullptr));  // use current time as seed for random generator

    // Signals
    sc_clock clock("clock", GlobalParams::clock_period_ps, SC_PS);
    sc_signal <bool> reset;

	// tlm::tlm_global_quantum::instance().set(sc_core::sc_time(GlobalParams::tlm_global_quantum, sc_core::SC_NS));

    // NoC instance
    n = new NoC("NoC");

    n->clock(clock);
    n->reset(reset);

    for (int j = 0; j < GlobalParams::mesh_dim_y; j++) {
        for (int i = 0; i < GlobalParams::mesh_dim_x; i++) {
            int core_id = j * GlobalParams::mesh_dim_x + i;

            std::string core_name = std::string("core[") + to_string(i) + "," + to_string(j) + std::string("]");
            n->t[i][j]->pe->core = new ISS(core_name.c_str(), core_id);
            n->t[i][j]->pe->core->clock(clock);
            n->t[i][j]->pe->core->reset(reset);
            config_core(*(n->t[i][j]->pe->core), i, j);

            n->t[i][j]->pe->local_isock.bind(n->t[i][j]->pe->core->dma_ctrl->local_tsock);
            n->t[i][j]->pe->core->dma_ctrl->local_isock.bind(n->t[i][j]->pe->local_tsock);
        }
    }

    // Trace signals
    sc_trace_file *tf = NULL;
    if (GlobalParams::noc_trace_mode) {
        tf = sc_create_vcd_trace_file(GlobalParams::trace_filename.c_str());
        sc_trace(tf, reset, "reset");
        sc_trace(tf, clock, "clock");

        for (int i = 0; i < GlobalParams::mesh_dim_x; i++) {
            for (int j = 0; j < GlobalParams::mesh_dim_y; j++) {
                char label[64];

                sprintf(label, "req(%02d)(%02d).east", i, j);
                sc_trace(tf, n->req[i][j].east, label);
                sprintf(label, "req(%02d)(%02d).west", i, j);
                sc_trace(tf, n->req[i][j].west, label);
                sprintf(label, "req(%02d)(%02d).south", i, j);
                sc_trace(tf, n->req[i][j].south, label);
                sprintf(label, "req(%02d)(%02d).north", i, j);
                sc_trace(tf, n->req[i][j].north, label);

                sprintf(label, "ack(%02d)(%02d).east", i, j);
                sc_trace(tf, n->ack[i][j].east, label);
                sprintf(label, "ack(%02d)(%02d).west", i, j);
                sc_trace(tf, n->ack[i][j].west, label);
                sprintf(label, "ack(%02d)(%02d).south", i, j);
                sc_trace(tf, n->ack[i][j].south, label);
                sprintf(label, "ack(%02d)(%02d).north", i, j);
                sc_trace(tf, n->ack[i][j].north, label);
            }
        }
    }

    // Reset the chip and run the simulation
    reset.write(1);
    cout << "Reset for " << (int)(GlobalParams::reset_time) << " cycles... " << std::endl; 
    srand(GlobalParams::rnd_generator_seed);

    // fix clock periods different from 1ns
    //sc_start(GlobalParams::reset_time, SC_NS);
    sc_start(GlobalParams::reset_time * GlobalParams::clock_period_ps, SC_PS);

    reset.write(0);
    cout << " done! " << std::endl;
    cout << " Now running for " << GlobalParams:: simulation_time << " cycles..." << std::endl;
    // fix clock periods different from 1ns
    //sc_start(GlobalParams::simulation_time, SC_NS);
    sc_start(GlobalParams::simulation_time * GlobalParams::clock_period_ps, SC_PS);


    // Close the simulation
    if (GlobalParams::noc_trace_mode) sc_close_vcd_trace_file(tf);
    cout << "Noxim simulation completed.";
    cout << " (" << sc_time_stamp().to_double() / GlobalParams::clock_period_ps << " cycles executed)" << endl;
    cout << endl;

    // Show statistics
    GlobalStats gs(n);
    gs.showStats(std::cout, GlobalParams::detailed);


    if ((GlobalParams::max_volume_to_be_drained > 0) &&
	(sc_time_stamp().to_double() / GlobalParams::clock_period_ps - GlobalParams::reset_time >=
	 GlobalParams::simulation_time)) {
	cout << endl
         << "WARNING! the number of flits specified with -volume option" << endl
	     << "has not been reached. ( " << drained_volume << " instead of " << GlobalParams::max_volume_to_be_drained << " )" << endl
         << "You might want to try an higher value of simulation cycles" << endl
	     << "using -sim option." << endl;

#ifdef TESTING
	cout << endl
         << " Sum of local drained flits: " << gs.drained_total << endl
	     << endl
         << " Effective drained volume: " << drained_volume;
#endif

    }

#ifdef DEADLOCK_AVOIDANCE
	cout << "***** WARNING: DEADLOCK_AVOIDANCE ENABLED!" << endl;
#endif
    return 0;
}
