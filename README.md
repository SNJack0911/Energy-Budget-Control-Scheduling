
# Session 1: Getting started
First, clone this repository somewhere on your filesystem.

```sh
git clone https://gitlab.com/mpoquet-courses/sched-with-batsim.git
```

**Unless otherwise stated, all provided commands should be run from the root directory of the cloned repository.**

## Software environment
This section describes how to get into an environment where all the software is available for you to use.
3 methods to get into such an environment are listed below, in order of preference.

You can use the following commands to check if all the required softwares are available or not.

```sh
batsim --version
meson --version
ninja --version
pkg-config --version
gdb --version
cgdb --version
```

### Method 1: Nix
Nix a generic purpose package manager that tries as hard as possible to make builds reproducible.
Nix is strongly recommended for any scientific usage, since it not only produces reproducible binaries, but also allows good traceability of an experiment.

Steps to get into a Batsim environment.

1. Install Nix. Up-to-date information is there: https://nixos.org/download/  
  - If you have no installation privileges, try `nix-portable` instead: https://github.com/DavHau/nix-portable
2. Enable Nix flakes. This is done by modifying a configuration file (or creating it first if needed).
   Up-to-date information is there, probably on the "Other Distros, without Home-Manager" section: https://nixos.wiki/wiki/Flakes
3. Optional but recommended: Run this command to use [our binary cache](https://app.cachix.org/cache/capack) instead of compiling software on your local machine:
   `nix develop --extra-substituters 'https://capack.cachix.org' --extra-trusted-public-keys 'capack.cachix.org-1:38D+QFk3JXvMYJuhSaZ+3Nm/Qh+bZJdCrdu4pkIh5BU='`
4. Run this command to enter a new shell from which you should be able to run all softwares: `nix develop`

### Method 2: Docker
Additionally, a Docker container is provided.

1. Install Docker if needed, and configure it if needed.
2. Give read and write permissions to all users on the repository you have git cloned previously.
3. Run the following command: `docker run -it --read-only --volume .:/outside oarteam/batsim-getting-started:ARCH`
  - Replace ARCH by `x86_64-linux` if your CPU architecture is [x86_64](https://en.wikipedia.org/wiki/X86-64)
  - Replace ARCH by `aarch64-linux` if your CPU architecture is [AArch64](https://en.wikipedia.org/wiki/AArch64)

### Method 3: Build it yourself
Feeling extra adventurous? You can build everything yourself. This is not really documented but here some guidelines.

- You should be able to get these from the package manager of any decent Linux distro: a C++ compilation toolchain, [Meson](https://mesonbuild.com/), [Ninja](https://ninja-build.org/), [pkg-config](https://en.wikipedia.org/wiki/Pkg-config), [Boost](https://en.wikipedia.org/wiki/Boost_(C%2B%2B_libraries)) and [nlohmann_json](https://github.com/nlohmann/json).
- Either get SimGrid from your Linux distro package manager if available, or build it from [source](https://framagit.org/simgrid/simgrid) then install it.
  SimGrid is usually built using [CMake](https://en.wikipedia.org/wiki/CMake).
  As I write these lines, Batsim should work with SimGrid 3.36.0.
- Build and install [intervalset](https://framagit.org/batsim/intervalset) from source.
- Build and install [batprotocol-cpp](https://framagit.org/batsim/batprotocol) from source.
- Build and install [batsim](https://framagit.org/batsim/batsim) from source. Use the `batprotocol` branch. Batsim may require more dependencies, install these when Meson yells at you because it cannot find them.

## Run your first simulation
Yay, most programs and libraries should now be available!

A simple scheduling code is provided in the repository (`exec1by1.cpp`), as well as a description on how to build it (`meson.build`).
Running the following commands shoud compile the algorithm into a shared library (`.so` [ELF](https://en.wikipedia.org/wiki/Executable_and_Linkable_Format) file).

```sh
# setup (generate and configure) a build directory named 'build'
meson setup build

# call ninja, which is in charge of executing the compilation workflow generated my meson
ninja -C buid
```

Your build directory should now contain a `.so` file for the `exec1by1` scheduling algorithm.  
You can list them by running `ls -l build/*.so`

You can now run Batsim, which usually requires three main inputs.
- A SimGrid platform file, which describes the resources used in the simulation.
  More information in [SimGrid documentation](https://simgrid.org/doc/latest/Platform.html)
- A Batsim workload file, which describes what user requests are done at which time (_jobs_), as well as how to simulate each job (_profiles_).
  More information in [Batsim documentation](https://batsim.readthedocs.io/en/latest/input-workload.html)
- A Batsim _external decision component (edc)_.
  You have just built one, as _EDC_ is just Batsim jargon to name a scheduler.

The following command should run Batsim with the scheduler you just built, a tiny platform of 1 machine, and a tiny workload of 2 jobs.

```sh
batsim -l ./build/libexec1by1.so 0 '' -p assets/1machine.xml -w assets/2jobs.json
```

## A glimpse at the simulation result
By default, Batsim writes output files into an `out` directory in your current directory.
Where to write output files can be customized with Batsim's `-e` command-line option.  
You can run Batsim with the `--help` option for details, and to list the available options.

Batsim should have generated two output files.
- `schedule.csv` contains various metrics about the resulting schedule of the simulation.
  This file is similar to what Batsim prints at the end of the simulation.
- `jobs.csv` contains various information about the jobs of the simulation.
  There should be one line per job.
  Some fields identify the job (`job_id` and `workload_name`).
  Some fields conveniently provide you user-given information on the job request (`requested_number_of_resources`, `requested_time`).
  Some fields give you information about the job execution (`starting_time`, `finish_time`, `allocated_resources`).

Have a look at the `jobs.csv` file.
Using a tool that can nicely print CSV files is recommended, such as [bat](https://github.com/sharkdp/bat) for terminal use, [R](https://en.wikipedia.org/wiki/R_(programming_language)) or [Python](https://en.wikipedia.org/wiki/Python_(programming_language)) for data analysis use, or alternatively any interactive spreadsheet software.


## Run your first simulation in a debugger
The provided setup should enable you to smootly run the simulation from a debugger.
This is very convenient to debug your code, or just to see what it does step by step.

The debugger should find the sources of your scheduler easily, as you compiled it yourself.
This is another story for the software ecosystem that calls you code, or for the code that your scheduler calls.
The `${GDB_DIR_ARGS}` environment contains the additional arguments that should be given to `gdb` so that it finds the sources of `batsim`, `batprotocol-cpp` and `intervalset`.

You can use `gdb` directly if you want, or any tool that decorates it with some user interface.
Here is an example using `cgdb`, which is a terminal user interface on top of `gdb`, and which should be available in the provided environment.

```sh
cgdb ${GDB_DIR_ARGS} --args batsim -l ./build/libexec1by1.so 0 '' -p assets/1machine.xml -w assets/2jobs.json
```

Before running the program (by calling the `run` command), you can configure on which events the debugger should stop.
This is typically with breakpoints, conditional breakpoints or watchpoints.
- `break batsim_edc_take_decisions` will make the debugger stop whenever your code is called to decide something.

## Analyze and visualize simulation results
In most real scenarios, you need to compute some analysis on Batsim outputs.
Here you don't... but you'll soon need to do it, so let us do something simple first 😜.

Create a program in your favorite data analysis programming language (R, Julia, Python...) to do the following.
- Read the `jobs.csv` file output from Batsim.
- Recompute the waiting time of each job from its `starting_time` and its `finish_time`.
- Compute aggregation of the waiting time of all the jobs (minimum, median, arithmetic mean, maximum), and print it.

Create a program in your favorite data visualization programming language (R, Julia, gnuplot, Python...) to do the following.
- Read the `jobs.csv` file output from Batsim.
- Plot the Gantt chart of the jobs.
  - Here is an old example in R + tidyverse: https://adfaure.github.io/blog/gantt-charts/
  - Here are many examples in Python: https://github.com/oar-team/evalys/tree/master


# Session 2: Write your own scheduler
## FCFS
Implement FCFS into a new Batsim scheduler with the following steps.

1. Copy `src/exec1by1.cpp` into `src/fcfs.cpp`.
2. Modify `meson.build` to also compile a `fcfs` shared library, using `src/fcfs.cpp` instead of `src/exec1by1.cpp`.
3. Run `ninja -C build` and you should see your new scheduler.
4. Modify `src/fcfs.cpp` so it runs a FCFS algorithm that can run jobs in parallel.

To test that your FCFS algorithm work as expected, create a SimGrid platform that contains several hosts, create a workload with at least 5 jobs such that you can see a difference in the decisions made by FCFS and the provided sequencer (`exec1by1`).

## EASY Backfilling
Similarly, implement [EASY Backfilling](http://www.cs.umd.edu/~hollings/cs818z/s99/papers/feitelson.pdf) into a new Batsim scheduler with similar steps as for FCFS.

To test your algorithm, create a workload with at least 5 jobs that all have a `walltime` field.
Make sure that your workload have some _holes_ if executed by FCFS, such that the holes are backfilled if executed by EASY.

## Performance comparison of EASY and FCFS
Execute both FCFS and EASY on the `assets/more_jobs.json` workload, using the first 32 hosts of the `assets/cluster512.xml` SimGrid platform, thanks to the `--mmax` Batsim command-line option. Note the _real_ time it takes Batsim to simulate this instance in both cases.

Write a script in your favorite data analysis programming language that open both Batsim `jobs.csv` output files and that computes the following metrics.
- Makespan. Compute it as the maximum job completion time minus the minimum job submission time.
- Mean waiting time. Compute the waiting time of each job as the difference between starting time and submission time, then average it over all jobs.
- Mean turnaround time. The turnaround time is the time a job spent in the system (completion time minus submission time). Compute it for each job then average it over all jobs.

Which algorithm performs the best on these metrics? Is this done on behalf of other criteria? Typically, does one algorithm favors one kind of jobs more than the other?


# Session 3: Walltime impact on Backfilling
The goal of this session is to better understand how backfilling algorithms are impacted by the walltimes provided by the users.

## Custom short workload
First, write a workload of at least 10 jobs with delay profiles such that
- all submission times are different, first job is submitted at time 0, last job is submitted near the schedule makespan / 2
- all jobs use delay profiles with different length
- jobs ask between 1 and 32 resources
- FCFS and EASY should behave differently on it

Generate several variants of your workload, such that
- the walltime of each job is the profile duration + 10 s
- the walltime of each job is the profile duration + 100 s
- the walltime of each job is the profile duration + 1000 s
- the walltime of each job is the profile duration + 10000 s

Execute EASY on each workload, and execute FCFS on one workload. Compare the resulting schedules visually, using Gantt charts.

What intuition the Gantt charts give you on the impact of walltime values on the performance of EASY backfilling?

## Statistics time
Now, instead of looking at Gantt charts, let us run bigger workloads and compute statistics on the resulting schedules :).

Write a small script in your favorite prototyping language that opens a Batsim workload that contains jobs that use delay profiles, and that generates a new workload with the walltime of each job set to the duration of its profile + a given static delay.

Use your script on the `assets/more_jobs.json` input workload with 10, 100, 1000 and 10000 s static delays to generate 4 different workloads.

Execute EASY on each workload, and execute FCFS on one workload.

Compute, for each simulation instance, the mean waiting time of all the jobs. What pattern do you see here?

Visualize the distribution of the waiting time of all the jobs of each simulation instance.
What do you see if you split this visualization by categories of job execution times? For example short jobs (<= 10 s), medium jobs (between 10 s and 100 s), and long jobs (> 100 s)?

# Projects
Projects are to be carried out in groups. Members of each project should do the following.
- Read and understand scheduling algorithms described in a research paper.
- Implement in Batsim (batprotocol branch) algorithms from the paper.
- Do a simple comparison of the implemented algorithms (or variants of the algorithms), using your own input files (workloads and platforms).
  The comparaison should be similar to the one used in the experimental section of the paper, but with much less input workloads and much smaller workloads/platforms.
  You can typically see it as a test rather than a scientific evaluation with this question: Do the conclusions of the article hold on your small set of inputs or not?

Each **student** is responsible for implementing 1 or more algorithms.
Each **group** is responsible for comparing the different algorithms on the same inputs, using the same metrics computed from the simulation outputs.

The project will be evaluated from two outputs from each group.
- A public git repository that contains your implementations of the algorithms, your code to compare the algorithms on some workloads/platforms, and a small documentation on how to compile and run everything.
- A group defense where you'll quickly present the algorithms, how the algorithms have been implemented, who has implemented what, how you have compared the algorithms, and what are the results of your comparison.

## Topic 1: Job Reordering in EASY Backfilling
- Base article: Tuning EASY-Backfilling Queues. J Lelong, V Reis, D Trystram.
  https://hal.science/hal-01522459/document
- Expected algorithms:
  - EASY with FCFS reordering (default EASY policy)
  - EASY with LCFS reordering
  - EASY with LPF reordering
  - EASY with SPJ reordering
  - EASY with LQF reordering
  - EASY with SQJ reordering

## Topic 2: Conservative Backfilling
- Base article: Utilization and Predictability in Scheduling the IBM SP2 with Backfilling. D Feitelson, A Weil.
  http://www.cs.umd.edu/~hollings/cs818z/s99/papers/feitelson.pdf
- Expected algorithms:
  - Conservative Backfilling, `do nothing` option
  - Conservative Backfilling, `initiate a new round of backfilling` option
  - Conservative Backfilling, `retain the original schedule, but compress it` option

## Topic 3: Backfilling under an Energy Budget
- Base article: Towards Energy Budget Control in HPC. PF Dutot, Y Georgiou, D Glesser, L Lefèvre, M Poquet, I Raïs.
  https://hal.science/hal-01533417v1/file/towards_energy_budget_control_in_hpc.pdf
- Expected algorithms:
  - `energyBud_IDLE`
  - `PC_IDLE`
  - `reducePC_IDLE`

## Topic 4: Contiguity and Locality Constraints in Backfilling
- Base article : Fernando Mendonça's PhD manuscript, Chapters 4 and 5.
  https://theses.hal.science/tel-01681424v2/file/MENDONCA_2017_archivage.pdf
- Expected algorithms:
  - `Basic Backfilling (Algorithm 2)`
  - `Backfilling with best effort contiguity (Algorithm 3)`
  - `Backfilling with best effort locality (Algorithm 4)`
  - `Backfilling with forced contiguity`
  - `Backfilling with forced locality`

# Acknowledgment
This tutorial is heavily inspired from Mael Madon's work on https://gitlab.irit.fr/sepia-pub/mael/RM4ES-practicals.
