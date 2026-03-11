# Implementation Plan: Topic 3 — Backfilling under an Energy Budget

**Paper:** Towards Energy Budget Control in HPC. Dutot et al., CCGrid 2017.
**Target algorithms:** `PC_IDLE`, `energyBud_IDLE`, `reducePC_IDLE`

---

## 1. Understanding the Batsim EDC Template

Before planning the algorithms, we must understand how `exec1by1.cpp` works, since all three algorithms follow the same EDC skeleton.

### The 3 mandatory functions

```
batsim_edc_init()        → called once at start: allocate global state, parse init data
batsim_edc_deinit()      → called once at end: free all memory
batsim_edc_take_decisions() → called on every event: process events, then make scheduling decisions
```

### The event loop pattern (from exec1by1.cpp)

Every call to `batsim_edc_take_decisions` follows this fixed structure:

```
1. deserialize_message()         → parse what Batsim sent us
2. mb->clear(parsed->now())      → reset decisions buffer, record current time
3. for each event in events:     → react to what happened
     BatsimHelloEvent             → reply with edc_hello (handshake)
     SimulationBeginsEvent        → read platform info (nb_hosts)
     JobSubmittedEvent            → add job to queue
     JobCompletedEvent            → free resources
     (others ignored with default: break)
4. [scheduling logic here]       → decide what to launch
5. mb->finish_message()          → finalize decisions
6. serialize_message()           → send decisions back to Batsim
```

### Key Batsim API calls used

| Call | What it does |
|---|---|
| `mb->add_edc_hello(name, version)` | Protocol handshake reply |
| `mb->add_execute_job(job_id, hosts_str)` | Launch a job on given hosts |
| `mb->add_reject_job(job_id)` | Reject a job (too big for platform) |
| `mb->add_call_me_later(time)` | Ask Batsim to call us again at a future time (for monitoring) |
| `IntervalSet(ClosedInterval(a, b))` | Create a set of host IDs from a to b |
| `hosts.to_string_hyphen()` | Serialize host set for execute_job |

### What exec1by1 does NOT do (but we need)

- It does NOT implement EASY Backfilling (no queue reordering, no reservations)
- It does NOT read walltime from jobs
- It does NOT track free hosts as an interval set (just counts them)
- It does NOT parse init data (ignores `data` and `size`)
- It does NOT use `add_call_me_later` for periodic monitoring

---

## 2. What We Need to Add Per Algorithm

### New fields in SchedJob struct

`exec1by1` only stores `job_id` and `nb_hosts`. We need to extend this:

```
SchedJob:
  job_id          string     — unique job identifier
  nb_hosts        uint32     — number of processors requested
  walltime        double     — user-provided walltime estimate (wall_j from paper)
  submission_time double     — r_j: when job was submitted
```

`walltime` is critical — it is used to estimate `E_job = nb_hosts × (P̃_comp − P̃_idle) × walltime` in energyBud and reducePC.

### New global state (across all algorithms)

```
// Platform state
uint32_t platform_nb_hosts       — total hosts (from SimulationBeginsEvent)
IntervalSet free_hosts           — currently available host IDs
uint32_t n_comp                  — number of hosts currently computing
uint32_t n_idle                  — number of hosts currently idle (= platform_nb_hosts - n_comp)

// Job queue
std::list<SchedJob*> queue       — waiting jobs in FCFS order

// Running jobs (to free hosts on completion)
std::map<string, SchedJob*> running_jobs   — job_id → SchedJob*
std::map<string, IntervalSet> running_alloc — job_id → hosts used
```

### Init data format (parsed from `data` in `batsim_edc_init`)

We pass energy parameters as JSON on the Batsim command line:
```json
{"B": 9.0e10, "t_s": 86400.0, "t_e": 345600.0}
```

This is parsed using `nlohmann_json` (already a project dependency) inside `batsim_edc_init`.

---

## 3. Algorithm 1: PC_IDLE

### Concept (from paper)

Convert energy budget to constant power cap: `P_limit = B / (t_e - t_s)` W.
Before launching a job, check that instantaneous platform power would not exceed `P_limit`.
No counter, no memory of history.

### Additional global state

```
double P_limit       — = B / (t_e - t_s), computed in init
double B, t_s, t_e   — energy budget parameters
```

### Power check formula

Current platform power estimate:
```
P_current = n_comp × P̃_comp + n_idle × P̃_idle
```

Power if job j is launched (j's hosts switch from idle → computing):
```
P_if_launched = P_current + m_j × (P̃_comp - P̃_idle)
```

Launch condition: `P_if_launched <= P_limit`

### How batsim_edc_take_decisions works for PC_IDLE

```
Process events:
  BatsimHelloEvent       → edc_hello
  SimulationBeginsEvent  → read nb_hosts, init free_hosts, n_idle = nb_hosts
  JobSubmittedEvent      → add to queue (or reject if m_j > nb_hosts)
  JobCompletedEvent      → free hosts, update n_comp/n_idle

Scheduling (EASY Backfilling with power check):

  Phase 1 — greedy pass:
    for each job in queue (front to back):
      if free_hosts has enough AND power_check(job) passes:
        execute job, remove from queue, update n_comp/n_idle
      else:
        break

  Phase 2 — reserve firstJob, backfill rest:
    firstJob = queue.front()
    find earliest_start_time for firstJob (scan future availability)
    compute firstJob_reservation (which hosts, at what time)
    for each remaining job in queue:
      if free_hosts has enough
         AND power_check(job) passes
         AND job does not overlap with firstJob reservation:
        execute job, remove from queue, update n_comp/n_idle
    (reservation is internal only — no Batsim API call needed)
```

### What "does not overlap with firstJob reservation" means in Batsim context

Since we only control current launches (Batsim is event-driven, not schedule-in-advance), the reservation is **simulated internally**:
- We track that firstJob will start at time `q` using `reserved_hosts` and `reserved_time`
- A backfill job "overlaps" if: `now + job.walltime > q` (it would still be running when firstJob needs to start)
- If `now + job.walltime <= q`: job finishes before firstJob starts → safe to backfill

### No monitoring needed for PC_IDLE

PC_IDLE is purely instantaneous — no C_ea, no periodic callback.

---

## 4. Algorithm 2: energyBud_IDLE

### Concept (from paper)

Maintain counter `C_ea` (joules). Accrues at rate `B/(t_e−t_s)` J/s. Deduct estimated consumption continuously. Lock energy for firstJob reservation. Correct at monitoring stages.

### Additional global state

```
double B, t_s, t_e           — budget parameters
double energy_rate           — = B / (t_e - t_s)  [J/s]
double C_ea                  — available energy counter [J]
double last_update_time      — time of last C_ea update
double last_monitor_time     — time of last monitoring stage

// For firstJob reservation
bool   has_reservation       — whether a processor+energy reservation exists
string reserved_job_id       — which job has the reservation
double reserved_start_time   — q: earliest start time for firstJob
double reserved_energy       — joules locked for firstJob
IntervalSet reserved_hosts   — processors reserved for firstJob
```

### C_ea update logic (called at start of every take_decisions)

```
dt = now - last_update_time
C_ea += energy_rate × dt                                  // Rule 1: budget accrual
C_ea -= (n_comp × P̃_comp + n_idle × P̃_idle) × dt        // deduct overestimated consumption
last_update_time = now
```

### Monitoring stage logic (triggered by add_call_me_later)

In Batsim, `add_call_me_later(t)` causes Batsim to call `take_decisions` at time `t` even if nothing else happens. We use this for monitoring.

When `now >= last_monitor_time + 600`:
```
// In simplified implementation (_IDLE, no real energy sensor):
// The overestimation correction is already baked in:
// C_ea uses P̃_comp/P̃_idle but real consumption is P_comp/P_idle.
// The difference accumulates passively in C_ea as a natural surplus.
// For full correctness, if energy reporting is available from Batsim:
//   real_energy = sum of energy from completed jobs since last monitor
//   estimated_energy = already deducted from C_ea
//   C_ea += (estimated_energy - real_energy)

last_monitor_time = now
mb->add_call_me_later(now + 600)   // schedule next monitoring stage
```

### Energy check formula

Net extra energy cost of launching job j (hosts switch idle → compute):
```
E_job(j) = m_j × (P̃_comp - P̃_idle) × wall_j
```

Launch condition: `C_ea >= E_job(j)`

When launched: `C_ea -= E_job(j)`

### How batsim_edc_take_decisions works for energyBud_IDLE

```
Process events:
  BatsimHelloEvent       → edc_hello
  SimulationBeginsEvent  → read nb_hosts, init state, schedule first monitor:
                           mb->add_call_me_later(t_s + 600)
  JobSubmittedEvent      → add to queue (or reject)
  JobCompletedEvent      → free hosts, update n_comp/n_idle, update C_ea for freed hosts

Update C_ea:
  update_Cea(now)        // accrue budget, deduct estimated consumption

Check monitoring:
  if now >= last_monitor_time + 600:
    apply_monitoring_correction()
    last_monitor_time = now
    mb->add_call_me_later(now + 600)

Scheduling (Algorithm 2 from paper):

  Phase 1 — greedy pass:
    for each job in queue (front to back):
      if free_hosts >= m_j
         AND C_ea >= E_job(job):
        allocate hosts, execute job
        C_ea -= E_job(job)
        update n_comp/n_idle
        remove from queue
      else:
        break

  Phase 2 — reserve firstJob (processors + energy), then backfill:
    firstJob = queue.front()
    q = find_earliest_start(firstJob)         // earliest time m_j hosts are free
    reserved_start_time = q
    reserved_energy = E_job(firstJob)
    C_ea -= reserved_energy                   // lock energy for firstJob

    for each remaining job in queue:
      if free_hosts >= m_j
         AND C_ea >= E_job(job)
         AND (now + job.walltime <= q):        // does not delay firstJob
        allocate hosts, execute job
        C_ea -= E_job(job)
        update n_comp/n_idle
        remove from queue

    // Release reservation — firstJob goes back to front of queue
    C_ea += reserved_energy                   // unlock energy
    // (Processor reservation is internal; no Batsim API call)
```

### Key detail: when to re-run scheduling

The algorithm is called not only on events but also at monitoring stages. When `add_call_me_later` fires, `take_decisions` is called with no new job events — only the monitoring correction changes `C_ea`, potentially allowing jobs that were previously blocked to now start. This is why the monitoring stage matters: it releases frozen energy, enabling more backfilling.

---

## 5. Algorithm 3: reducePC_IDLE

### Concept (from paper)

Same as energyBud, **except** the energy reservation for firstJob **reduces the per-second rate** instead of subtracting a lump sum from `C_ea`.

### Additional global state (on top of energyBud state)

```
double rate_reduction    — J/s reduction due to active reservation
                           = reserved_energy / (reserved_start_time - now)
```

### Reservation difference

**energyBud:** `C_ea -= J_i` (lump sum subtracted from counter)

**reducePC:** Effective rate is reduced:
```
effective_rate = energy_rate - (J_i / (q_i - now))
```

The backfill energy check changes from:
```
// energyBud check:
C_ea >= E_job(job)

// reducePC check:
E_job(job) / job.walltime <= effective_rate
// i.e., the job's average power demand must fit within the reduced rate
```

### Consequence for scheduling

A short job using all N processors has:
- High power demand: `N × (P̃_comp - P̃_idle)`
- Low total energy: `N × (P̃_comp - P̃_idle) × short_walltime`

In **energyBud**: if `C_ea` has enough joules, the burst is allowed.
In **reducePC**: if `N × (P̃_comp - P̃_idle) > effective_rate`, the job is blocked regardless of `C_ea`.

This makes reducePC more conservative — fewer backfill jobs during reservation periods → more idle hosts → more monitoring corrections → better with `_SHUT` (but worse with `_IDLE`).

### How batsim_edc_take_decisions works for reducePC_IDLE

Identical to energyBud except:
1. No `C_ea -= reserved_energy` when making the reservation
2. Instead: compute `rate_reduction = reserved_energy / (reserved_start_time - now)`
3. Effective rate = `energy_rate - rate_reduction` during the reservation window
4. Backfill check: `E_job(job) / job.walltime <= effective_rate` (instead of `C_ea >= E_job(job)`)
5. C_ea is NOT decremented for the reservation (the rate reduction IS the reservation)

---

## 6. Finding the Earliest Start Time for firstJob

Both energyBud and reducePC need to find `q` = earliest time when `m_firstJob` processors are free.

### How to compute it

We cannot predict the future in an event-driven simulator. A practical approximation:

```
running jobs have known:
  - walltime (upper bound on remaining execution)
  - allocated hosts

Simulate release times:
  create list of (walltime_remaining, hosts_freed) for all running jobs
  sort by walltime_remaining
  simulate freeing hosts one by one
  find earliest time when cumulative free hosts >= m_firstJob
```

This is an **approximation** (uses walltime, not real runtime). Since Batsim kills jobs at walltime, this is a valid upper bound — in the worst case, firstJob starts exactly when the last blocking job hits its walltime.

### What "does not overlap" means precisely

For a backfill job b:
```
does_not_overlap = (now + b.walltime <= q)
```

If job b starts now and runs for at most `b.walltime` seconds, it will finish by `now + b.walltime`. If this is ≤ `q`, it does not push back firstJob's start.

---

## 7. File Structure Plan

```
src/
  batsim_edc.h            [ORIGINAL — do not touch]
  exec1by1.cpp            [ORIGINAL — do not touch]
  energy_common.h         [NEW] shared constants and SchedJob struct
  pc_idle.cpp             [NEW] Algorithm: PC_IDLE
  energybud_idle.cpp      [NEW] Algorithm: energyBud_IDLE
  reducepc_idle.cpp       [NEW] Algorithm: reducePC_IDLE

assets/
  1machine.xml            [original]
  cluster512.xml          [original]
  2jobs.json              [original]
  more_jobs.json          [original]
  energy_workload.json    [NEW] custom workload with walltimes

scripts/
  gen_workload.py         [NEW] generate workloads with walltime variants
  compare.py              [NEW] compute metrics and plot Gantt charts

meson.build               [MODIFIED] add 3 new shared_library entries
plan.md                   [this file]
explanation.md            [paper explanation]
```

---

## 8. energy_common.h — Shared Definitions

This header is included by all three algorithm files. It contains:

```
// Power constants from Table I of the paper
P̃_idle = 100.00 W
P̃_comp = 203.12 W
MONITORING_PERIOD = 600.0 s

// Extended job struct (replaces exec1by1's SchedJob)
struct SchedJob {
  job_id          string
  nb_hosts        uint32
  walltime        double   // wall_j from paper
  submit_time     double   // r_j from paper
}

// Energy helper
double E_job(SchedJob* j)
  = j->nb_hosts × (P̃_comp - P̃_idle) × j->walltime
  // net extra cost of switching m_j hosts from idle to compute
```

---

## 9. meson.build Changes

Add three `shared_library` entries following the exact same pattern as `exec1by1`:

```meson
pc_idle = shared_library('pc_idle',
  common + ['src/pc_idle.cpp'],
  dependencies: deps, install: true)

energybud_idle = shared_library('energybud_idle',
  common + ['src/energybud_idle.cpp'],
  dependencies: deps, install: true)

reducepc_idle = shared_library('reducepc_idle',
  common + ['src/reducepc_idle.cpp'],
  dependencies: deps, install: true)
```

Where `common = ['src/batsim_edc.h', 'src/energy_common.h']`.

---

## 10. Init Data Format and Batsim Command Line

The init `data` parameter in `batsim_edc_init` comes from the Batsim command line:
```
batsim -l ./build/libpc_idle.so 0 'INIT_DATA_HERE' -p PLATFORM -w WORKLOAD
```

The second number (`0`) is the init data size; the string after it is the init data content. We will use JSON:
```
batsim -l ./build/libpc_idle.so 47 '{"B":9e10,"t_s":86400.0,"t_e":345600.0}' ...
```

Parse in `batsim_edc_init` using `nlohmann::json` (already a project dependency via meson.build).

---

## 11. Workload Requirements

All three algorithms require jobs with **walltimes** (`wall_j`). Without walltimes, energy estimation is impossible.

### Custom energy_workload.json

Must contain jobs with `walltime` field in their profile or job definition:
- At least 10–15 jobs
- Mix of sizes: 1, 2, 4, 8, 16, 32 processors
- Mix of durations: short (≤10 s), medium (10–100 s), long (>100 s)
- Walltimes = profile duration + small slack (e.g., +10 s)
- All jobs have different submission times
- FCFS and energyBud should produce visibly different schedules

### How to get walltime in the EDC

In batprotocol, from `JobSubmittedEvent`:
```cpp
auto parsed_job = event->event_as_JobSubmittedEvent();
double walltime = parsed_job->job()->walltime();
```

If walltime is `-1` or `0` (not provided), the job should be **rejected** by energy-aware schedulers (cannot estimate energy cost without it).

---

## 12. Run Commands (planned)

```sh
# Build
meson setup build && ninja -C build

# PC_IDLE
batsim -l ./build/libpc_idle.so 40 '{"B":9e10,"t_s":0,"t_e":259200}' \
  -p assets/cluster512.xml --mmax 32 -w assets/energy_workload.json -e out/pc

# energyBud_IDLE
batsim -l ./build/libenergybud_idle.so 40 '{"B":9e10,"t_s":0,"t_e":259200}' \
  -p assets/cluster512.xml --mmax 32 -w assets/energy_workload.json -e out/energybud

# reducePC_IDLE
batsim -l ./build/libreducepc_idle.so 40 '{"B":9e10,"t_s":0,"t_e":259200}' \
  -p assets/cluster512.xml --mmax 32 -w assets/energy_workload.json -e out/reducepc
```

---

## 13. Comparison Script Plan (compare.py)

Reads `out/*/jobs.csv` and computes, for each algorithm:

| Metric | Formula |
|---|---|
| Makespan | `max(finish_time) - min(submission_time)` |
| Mean waiting time | `mean(starting_time - submission_time)` |
| Mean turnaround time | `mean(finish_time - submission_time)` |
| Utilization | `sum(nb_hosts × real_duration) / (makespan × N)` |
| AVEbsld | `mean(max((wait + p) / max(p, 10), 1))` |
| Est. energy used | `sum(nb_hosts × (P_comp - P_idle) × real_duration)` |

Visualizations:
- Gantt chart per algorithm
- Bar chart comparing all metrics side by side
- Energy consumed vs. budget line (did we stay under B?)

---

## 14. Order of Implementation

```
Step 1: Read batsim_edc.h and batprotocol API
        — understand all available event types and message builder calls
        — find how to read walltime from JobSubmittedEvent
        — find how add_call_me_later works

Step 2: Create src/energy_common.h
        — SchedJob struct with walltime
        — power constants
        — E_job() helper declaration

Step 3: Implement PC_IDLE (src/pc_idle.cpp)
        — simplest: no counter, just power check
        — full EASY backfilling with power check
        — test: does it compile? does it run on 2jobs.json?

Step 4: Test PC_IDLE correctness
        — create a small workload where PC_IDLE blocks some jobs
        — verify jobs.csv shows expected behavior

Step 5: Implement energyBud_IDLE (src/energybud_idle.cpp)
        — C_ea counter + update logic
        — monitoring via add_call_me_later
        — energy reservation for firstJob
        — EASY backfilling with energy checks

Step 6: Test energyBud_IDLE
        — compare with PC_IDLE output: energyBud should start more jobs
        — verify C_ea never goes deeply negative at job start

Step 7: Implement reducePC_IDLE (src/reducepc_idle.cpp)
        — copy energyBud, change reservation logic to rate reduction
        — backfill check uses effective_rate not C_ea

Step 8: Create energy_workload.json
        — jobs with explicit walltimes
        — designed so all three algorithms differ visibly

Step 9: Run all three + compare
        — compute metrics with compare.py
        — expected ranking: energyBud > reducePC > PC on utilization/AVEbsld

Step 10: Test with multiple budget levels
        — 100%, 80%, 60% of B_max to see performance degradation
```

---

## 15. Expected Behavior per Algorithm

| Scenario | PC_IDLE | energyBud_IDLE | reducePC_IDLE |
|---|---|---|---|
| Cluster idle for 1 hour, then burst of jobs | Blocks burst (cap too low) | Allows burst (C_ea built up) | Partial burst (reduced rate limits) |
| Many small short jobs | Blocks if total power > cap | Allows freely if C_ea positive | Allows if per-second rate not exceeded |
| firstJob waiting + many backfill candidates | Backfills until power cap | Backfills until C_ea exhausted | Backfills until effective rate exceeded |
| Budget = 100% | Same as EASY Backfilling | Same as EASY Backfilling | Same as EASY Backfilling |
| Budget = 49% (idle level) | No jobs can run | Very few jobs (C_ea barely positive) | Very few jobs |
