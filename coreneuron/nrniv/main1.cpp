/*
Copyright (c) 2016, Blue Brain Project
All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:
1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.
3. Neither the name of the copyright holder nor the names of its contributors
   may be used to endorse or promote products derived from this software
   without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
THE POSSIBILITY OF SUCH DAMAGE.
*/

/**
 * @file main1.cpp
 * @date 26 Oct 2014
 * @brief File containing main driver routine for CoreNeuron
 */

#include <vector>
#include <string.h>
#include "coreneuron/engine.h"
#include "coreneuron/utils/randoms/nrnran123.h"
#include "coreneuron/nrnconf.h"
#include "coreneuron/nrnoc/multicore.h"
#include "coreneuron/nrnoc/nrnoc_decl.h"
#include "coreneuron/nrnmpi/nrnmpi.h"
#include "coreneuron/nrniv/nrniv_decl.h"
#include "coreneuron/nrniv/nrnmutdec.h"
#include "coreneuron/nrniv/output_spikes.h"
#include "coreneuron/nrniv/nrn_checkpoint.h"
#include "coreneuron/utils/endianness.h"
#include "coreneuron/utils/memory_utils.h"
#include "coreneuron/nrniv/nrnoptarg.h"
#include "coreneuron/utils/sdprintf.h"
#include "coreneuron/nrniv/nrn_stats.h"
#include "coreneuron/utils/reports/nrnreport.h"
#include "coreneuron/nrniv/nrn_acc_manager.h"
#include "coreneuron/nrniv/profiler_interface.h"
#include "coreneuron/nrniv/partrans.h"
#include "coreneuron/nrniv/multisend.h"
#include "coreneuron/utils/file_utils.h"
#include "coreneuron/nrniv/nrn2core_direct.h"
#include "coreneuron/nrniv/cn_parameters.h"
#include "coreneuron/utils/CLI11/CLI.hpp"
#include <string.h>
#include <climits>

extern "C" {
const char* corenrn_version() {
    return coreneuron::bbcore_write_version;
}

#ifdef ISPC_INTEROP
// cf. utils/ispc_globals.c
extern double ispc_celsius;
#endif

/**
 * If "export OMP_NUM_THREADS=n" is not set then omp by default sets
 * the number of threads equal to the number of cores on this node.
 * If there are a number of mpi processes on this node as well, things
 * can go very slowly as there are so many more threads than cores.
 * Assume the NEURON users pc.nthread() is well chosen if
 * OMP_NUM_THREADS is not set.
 */
void set_openmp_threads(int nthread) {
#if defined(_OPENMP)
    if (!getenv("OMP_NUM_THREADS")) {
        omp_set_num_threads(nthread);
    }
#endif
}

/**
 * Convert char* containing arguments from neuron to char* argv[] for
 * coreneuron command line argument parser.
 */
char* prepare_args(int& argc, char**& argv, int use_mpi, const char* arg) {
    // first construct all arguments as string
    std::string args(arg);
    args.insert(0, " coreneuron ");
    args.append(" --skip-mpi-finalize ");
    if (use_mpi) {
        args.append(" -mpi ");
    }

    // we can't modify string with strtok, make copy
    char* first = strdup(args.c_str());
    const char* sep = " ";

    // first count the no of argument
    char* token = strtok(first, sep);
    argc = 0;
    while (token) {
        token = strtok(NULL, sep);
        argc++;
    }
    free(first);

    // now build char*argv
    argv = new char*[argc];
    first = strdup(args.c_str());
    token = strtok(first, sep);
    for (int i = 0; token; i++) {
        argv[i] = token;
        token = strtok(NULL, sep);
    }

    // return actual data to be freed
    return first;
}

int corenrn_embedded_run(int nthread, int have_gaps, int use_mpi, const char* arg) {
    corenrn_embedded = 1;
    corenrn_embedded_nthread = nthread;
    coreneuron::nrn_have_gaps = have_gaps;

    set_openmp_threads(nthread);
    int argc = 0;
    char** argv;
    char* new_arg = prepare_args(argc, argv, use_mpi, arg);
    mk_mech_init(argc, argv);
    run_solve_core(argc, argv);
    free(new_arg);
    delete[] argv;

    return corenrn_embedded;
}
}

#if 0
#include <fenv.h>
#define NRN_FEEXCEPT (FE_DIVBYZERO | FE_INVALID | FE_OVERFLOW)
int nrn_feenableexcept() {
  int result = -1;
  result = feenableexcept(NRN_FEEXCEPT);
  return result;
}
#endif
namespace coreneuron {
void call_prcellstate_for_prcellgid(int prcellgid, int compute_gpu, int is_init);

void nrn_init_and_load_data(int argc,
                            char* argv[],
                            bool is_mapping_needed = false,
                            bool nrnmpi_under_nrncontrol = true,
                            bool run_setup_cleanup = true) {
#if defined(NRN_FEEXCEPT)
    nrn_feenableexcept();
#endif

    stop_profile();

// mpi initialisation
#if NRNMPI
    nrnmpi_init(nrnmpi_under_nrncontrol ? 1 : 0, &argc, &argv);
#endif

    // memory footprint after mpi initialisation
    report_mem_usage("After MPI_Init");

    // initialise default coreneuron parameters
    initnrn();

    // create mutex for nrn123, protect instance_count_
    nrnran123_mutconstruct();

    // set global variables
    // precedence is: set by user, globals.dat, 34.0
    celsius = cn_par.celsius;

#if _OPENACC
    if (!cn_par.gpu && cn_par.cell_interleave_permute == 2) {
        fprintf(
            stderr,
            "compiled with _OPENACC does not allow the combination of --cell-permute=2 and missing --gpu\n");
        exit(1);
    }
#endif

// if multi-threading enabled, make sure mpi library supports it
#if NRNMPI
    if (cn_par.threading) {
        nrnmpi_check_threading_support();
    }
#endif

    // full path of files.dat file
    std::string filesdat(cn_par.datpath + "/" + cn_par.filesdat);

    // read the global variable names and set their values from globals.dat
    set_globals(cn_par.datpath.c_str(), (cn_par.seed==0),
                cn_par.seed);

    // set global variables for start time, timestep and temperature
    std::string restore_path = cn_par.reportpath;
    t = restore_time(restore_path.c_str());

    if (cn_par.dt != -1000.) {  // command line arg highest precedence
        dt = cn_par.dt;
    } else if (dt == -1000.) {  // not on command line and no dt in globals.dat
        dt = 0.025;             // lowest precedence
    }

    cn_par.dt = dt;

    rev_dt = (int)(1. / dt);

    if (cn_par.celsius != -1000.) {  // command line arg highest precedence
        celsius = cn_par.celsius;
    } else if (celsius == -1000.) {  // not on command line and no celsius in globals.dat
        celsius = 34.0;              // lowest precedence
    }

    cn_par.celsius = celsius;

#ifdef ISPC_INTEROP
    ispc_celsius = celsius;
#endif
    // create net_cvode instance
    mk_netcvode();

    // One part done before call to nrn_setup. Other part after.

    if (!cn_par.patternstim.empty()) {
        nrn_set_extra_thread0_vdata();
    }

    report_mem_usage("Before nrn_setup");

    // set if need to interleave cells
    use_interleave_permute = cn_par.cell_interleave_permute;
    cellorder_nwarp = cn_par.nwarp;
    use_solve_interleave = cn_par.cell_interleave_permute;

#if LAYOUT == 1
    // permuting not allowed for AoS
    use_interleave_permute = 0;
    use_solve_interleave = 0;
#endif

    if (cn_par.gpu && use_interleave_permute == 0) {
        if (nrnmpi_myid == 0) {
            printf(
                " WARNING : GPU execution requires --cell-permute type 1 or 2. Setting it to 1.\n");
        }
        use_interleave_permute = 1;
        use_solve_interleave = 1;
    }

    // pass by flag so existing tests do not need a changed nrn_setup prototype.
    nrn_setup_multiple = cn_par.multiple;
    nrn_setup_extracon = cn_par.extracon;
    // multisend options
    use_multisend_ = cn_par.multisend ? 1 : 0;
    n_multisend_interval = cn_par.multisend;
    use_phase2_ = (cn_par.ms_phases == 2) ? 1 : 0;

    // reading *.dat files and setting up the data structures, setting mindelay
    nrn_setup(filesdat.c_str(), is_mapping_needed, nrn_need_byteswap, run_setup_cleanup);

    // Allgather spike compression and  bin queuing.
    nrn_use_bin_queue_ = cn_par.binqueue;
    int spkcompress = cn_par.spkcompress;
    nrnmpi_spike_compress(spkcompress, (spkcompress ? true : false), use_multisend_);

    report_mem_usage("After nrn_setup ");

    // Invoke PatternStim
    if (!cn_par.patternstim.empty()) {
        nrn_mkPatternStim(cn_par.patternstim.c_str());
    }

    /// Setting the timeout
    nrn_set_timeout(200.);

    // show all configuration parameters for current run
    //nrnopt_show();
    if (nrnmpi_myid == 0) {
        std::cout << " Start time (t) = " << t << std::endl << std::endl;
    }

    // allocate buffer for mpi communication
    mk_spikevec_buffer(cn_par.spikebuf);

    report_mem_usage("After mk_spikevec_buffer");

    if (cn_par.gpu) {
        setup_nrnthreads_on_device(nrn_threads, nrn_nthread);
    }

    if (nrn_have_gaps) {
        nrn_partrans::gap_update_indices();
    }

    // call prcellstate for prcellgid
    call_prcellstate_for_prcellgid(cn_par.prcellgid, cn_par.gpu, 1);
}

void call_prcellstate_for_prcellgid(int prcellgid, int compute_gpu, int is_init) {
    char prcellname[1024];
#ifdef ENABLE_CUDA
    const char* prprefix = "cu";
#else
    const char* prprefix = "acc";
#endif

    if (prcellgid >= 0) {
        if (compute_gpu) {
            if (is_init)
                sprintf(prcellname, "%s_gpu_init", prprefix);
            else
                sprintf(prcellname, "%s_gpu_t%f", prprefix, t);
        } else {
            if (is_init)
                strcpy(prcellname, "cpu_init");
            else
                sprintf(prcellname, "cpu_t%f", t);
        }
        update_nrnthreads_on_host(nrn_threads, nrn_nthread);
        prcellstate(prcellgid, prcellname);
    }
}

/* perform forwardskip and call prcellstate for prcellgid */
void handle_forward_skip(double forwardskip, int prcellgid) {
    double savedt = dt;
    double savet = t;

    dt = forwardskip * 0.1;
    t = -1e9;

    for (int step = 0; step < 10; ++step) {
        nrn_fixed_step_minimal();
    }

    if (prcellgid >= 0) {
        prcellstate(prcellgid, "fs");
    }

    dt = savedt;
    t = savet;
    dt2thread(-1.);
}

const char* nrn_version(int) {
    return "version id unimplemented";
}
}  // namespace coreneuron

/// The following high-level functions are marked as "extern C"
/// for compat with C, namely Neuron mod files.
/// They split the previous solve_core so that intermediate init of external mechanisms can occur.
/// See mech/corenrnmech.cpp for the new all-in-one solve_core (not compiled into the coreneuron
/// lib since with nrnivmodl-core we have 'future' external mechanisms)

using namespace coreneuron;

extern "C" int mk_mech_init(int argc, char** argv) {
    // read command line parameters and parameter config files
    //nrnopt_parse(argc, (const char**)argv);

    CLI::App app{"CoreNeuron - Your friendly neuron simulator."};

    app.get_formatter()->column_width(50);
    app.set_help_all_flag("-H, --help-all", "Print this help including subcommands and exit.");

    app.set_config("--config", "config.ini", "Read parameters from ini file", false)
        ->check(CLI::ExistingFile);
    app.add_flag("--mpi", cn_par.mpi_en, "Enable MPI. In order to initialize MPI environment this argument must be specified." );
    app.add_flag("--gpu", cn_par.gpu, "Activate GPU computation.");
    app.add_option("--dt", cn_par.dt, "Fixed time step. The default value is set by defaults.dat or is 0.025.", true)
        ->check(CLI::Range(-1000.,1e9));
    app.add_option("-e, --tstop", cn_par.tstop, "Stop Time in ms.")
        ->check(CLI::Range(0., 1e9));
    app.add_flag("--show", cn_par.print_arg, "Print arguments.");

    auto sub_gpu = app.add_subcommand("gpu", "Commands relative to GPU.");
    sub_gpu -> add_option("-W, --nwarp", cn_par.nwarp, "Number of warps to balance.", true)
        ->check(CLI::Range(0, 1000000));
    sub_gpu -> add_option("-R, --cell-permute", cn_par.cell_interleave_permute, "Cell permutation: 0 No permutation; 1 optimise node adjacency; 2 optimize parent adjacency.", true)
        ->check(CLI::Range(0, 3));

    auto sub_input = app.add_subcommand("input", "Input dataset options.");
    sub_input -> add_option("-d, --datpath", cn_par.datpath, "Path containing CoreNeuron data files.")
        ->check(CLI::ExistingPath);
    sub_input -> add_option("-f, --filesdat", cn_par.filesdat, "Name for the distribution file.", true)
        ->check(CLI::ExistingFile);
    sub_input -> add_option("-p, --pattern", cn_par.patternstim, "Apply patternstim using the specified spike file.")
        ->check(CLI::ExistingFile);
    sub_input -> add_option("-s, --seed", cn_par.seed, "Initialization seed for random number generator.")
        ->check(CLI::Range(0, 100000000));
    sub_input -> add_option("-v, --voltage", cn_par.voltage, "Initial voltage used for nrn_finitialize(1, v_init). If 1000, then nrn_finitialize(0,...).")
        ->check(CLI::Range(-1e9, 1e9));
    sub_input -> add_option("--read-config", cn_par.rconfigpath, "Read configuration file filename.")
        ->check(CLI::ExistingPath);
    sub_input -> add_option("--report-conf", cn_par.reportpath, "Reports configuration file.")
        ->check(CLI::ExistingPath);
    sub_input -> add_option("--restore", cn_par.restorepath, "Restore simulation from provided checkpoint directory.")
        ->check(CLI::ExistingPath);

    auto sub_parallel = app.add_subcommand("parallel", "Parallel processing options.");
    sub_parallel -> add_flag("-c, --threading", cn_par.threading, "Parallel threads. The default is serial threads.");
    sub_parallel -> add_flag("--skip-mpi-finalize", cn_par.skip_mpi_finalize, "Do not call mpi finalize.");

    auto sub_spike = app.add_subcommand("spike", "Spike exchange options.");
    sub_spike -> add_option("--ms-phases", cn_par.ms_phases, "Number of multisend phases, 1 or 2.", true)
        ->check(CLI::Range(1, 2));
    sub_spike -> add_option("--ms-subintervals", cn_par.ms_subint, "Number of multisend subintervals, 1 or 2.", true)
        ->check(CLI::Range(1, 2));
    sub_spike -> add_flag("--multisend", cn_par.multisend, "Use Multisend spike exchange instead of Allgather.");
    sub_spike -> add_option("--spkcompress", cn_par.spkcompress, "Spike compression. Up to ARG are exchanged during MPI_Allgather.", true)
        ->check(CLI::Range(0, 100000));
    sub_spike->add_flag("--binqueue", cn_par.binqueue, "Use bin queue." );

    auto sub_config = app.add_subcommand("config", "Config options.");
    sub_config -> add_option("-b, --spikebuf", cn_par.spikebuf, "Spike buffer size.", true)
        ->check(CLI::Range(0, 2000000000));
    sub_config -> add_option("-g, --prcellgid", cn_par.prcellgid, "Output prcellstate information for the gid NUMBER.")
        ->check(CLI::Range(0, 2000000000));
    sub_config -> add_option("-k, --forwardskip", cn_par.forwardskip, "Forwardskip to TIME")
        ->check(CLI::Range(0., 1e9));
    sub_config -> add_option("-l, --celsius", cn_par.celsius, "Temperature in degC. The default value is set in defaults.dat or else is 34.0.", true)
        ->check(CLI::Range(-1000., 1000.));
    sub_config -> add_option("-x, --extracon", cn_par.extracon, "Number of extra random connections in each thread to other duplicate models.")
        ->check(CLI::Range(0, 10000000));
    sub_config -> add_option("-z, --multiple", cn_par.multiple, "Model duplication factor. Model size is normal size * multiple")
        ->check(CLI::Range(1, 10000000));
    sub_config -> add_option("--mindelay", cn_par.mindelay, "Maximum integration interval (likely reduced by minimum NetCon delay).", true)
        ->check(CLI::Range(0., 1e9));
    sub_config -> add_option("--report-buffer-size", cn_par.report_buff_size, "Size in MB of the report buffer.")
        ->check(CLI::Range(1, 128));

    auto sub_output = app.add_subcommand("output", "Output configuration.");
    sub_output -> add_option("-i, --dt_io", cn_par.dt_io, "Dt of I/O.", true)
        ->check(CLI::Range(-1000., 1e9));
    sub_output -> add_option("-o, --outpath", cn_par.outpath, "Path to place output data files.", true)
        ->check(CLI::ExistingPath);
    sub_output -> add_option("--checkpoint", cn_par.checkpointpath, "Enable checkpoint and specify directory to store related files.")
        ->check(CLI::ExistingDirectory);

    CLI11_PARSE(app, argc, argv);

    if (cn_par.print_arg) {
        std::cout << std::fixed << std::setprecision(2);
        std::cout << cn_par << std::endl;
    }

    std::ofstream out("last_config.ini");
    out << app.config_to_str(true, true);
    out.close();

    // reads mechanism information from bbcore_mech.dat

    mk_mech((cn_par.datpath).c_str());
}

extern "C" int run_solve_core(int argc, char** argv) {
#if NRNMPI
    nrnmpi_init(1, &argc, &argv);
#endif
    std::vector<ReportConfiguration> configs;
    bool reports_needs_finalize = false;

    report_mem_usage("After mk_mech ang global initialization");

    if (cn_par.reportpath.size()) {
        if (cn_par.multiple > 1) {
            if (nrnmpi_myid == 0)
                printf("\n WARNING! : Can't enable reports with model duplications feature! \n");
        } else {
            configs = create_report_configurations(cn_par.reportpath.c_str(),
                                                   cn_par.outpath.c_str());
            reports_needs_finalize = configs.size();
        }
    }

    // initializationa and loading functions moved to separate
    nrn_init_and_load_data(argc, argv, configs.size() > 0);
    std::string checkpoint_path = cn_par.checkpointpath;
    if (strlen(checkpoint_path.c_str())) {
        nrn_checkpoint_arg_exists = true;
    }
    std::string output_dir = cn_par.outpath;

    if (nrnmpi_myid == 0) {
        mkdir_p(output_dir.c_str());
    }
#if NRNMPI
    nrnmpi_barrier();
#endif
    bool compute_gpu = cn_par.gpu;
    bool skip_mpi_finalize = cn_par.skip_mpi_finalize;

// clang-format off
    #pragma acc data copyin(celsius, secondorder) if (compute_gpu)
    // clang-format on
    {
        double v = cn_par.voltage;

        // TODO : if some ranks are empty then restore will go in deadlock
        // phase (as some ranks won't have restored anything and hence return
        // false in checkpoint_initialize
        if (!checkpoint_initialize()) {
            nrn_finitialize(v != 1000., v);
        }

        report_mem_usage("After nrn_finitialize");
        double dt = cn_par.dt;
        double delay = cn_par.mindelay;
        double tstop = cn_par.tstop;

        if (tstop < t && nrnmpi_myid == 0) {
            printf("Error: Stop time (%lf) < Start time (%lf), restoring from checkpoint? \n",
                   tstop, t);
            abort();
        }

        // register all reports into reportinglib
        double min_report_dt = INT_MAX;
        int report_buffer_size = cn_par.report_buff_size;
        for (size_t i = 0; i < configs.size(); i++) {
            register_report(dt, tstop, delay, configs[i]);
            if (configs[i].report_dt < min_report_dt) {
                min_report_dt = configs[i].report_dt;
            }
        }
        // Set the buffer size if is not the default value. Otherwise use report.conf on
        // register_report
        if (cn_par.report_buff_size!=4) {
            set_report_buffer_size(report_buffer_size);
        }
        setup_report_engine(min_report_dt, delay);
        configs.clear();

        // call prcellstate for prcellgid
        call_prcellstate_for_prcellgid(cn_par.prcellgid, compute_gpu, 0);

        // handle forwardskip
        if (cn_par.forwardskip > 0.0) {
            handle_forward_skip(cn_par.forwardskip, cn_par.prcellgid);
        }

        start_profile();

        /// Solver execution
        BBS_netpar_solve(cn_par.tstop);
        // Report global cell statistics
        report_cell_stats();

        // prcellstate after end of solver
        call_prcellstate_for_prcellgid(cn_par.prcellgid, compute_gpu, 0);
    }

    // write spike information to outpath
    output_spikes(output_dir.c_str());

    write_checkpoint(nrn_threads, nrn_nthread, checkpoint_path.c_str(), nrn_need_byteswap);

    stop_profile();

    // must be done after checkpoint (to avoid deleting events)
    if (reports_needs_finalize) {
        finalize_report();
    }

    // Cleaning the memory
    nrn_cleanup();

    // tau needs to resume profile
    start_profile();

// mpi finalize
#if NRNMPI
    if (!skip_mpi_finalize) {
        nrnmpi_finalize();
    }
#endif

    finalize_data_on_device();

    return 0;
}
