# Explanation: Towards Energy Budget Control in HPC

**Reference:** Pierre-François Dutot, Yiannis Georgiou, David Glesser, Laurent Lefèvre, Millian Poquet, Issam Raïs. CCGrid 2017, pp. 381–390. HAL Id: hal-01533417

---

## 1. Abstract (verbatim)

> *Energy consumption has become one of the most critical issues in the evolution of High Performance Computing systems (HPC). Controlling the energy consumption of HPC platforms is not only a way to control the cost but also a step forward on the road towards exaflops. Powercapping is a widely studied technique that guarantees that the platform will not exceed a certain power threshold instantaneously but it gives no flexibility to adapt job scheduling to a longer term energy budget control.*
>
> *We propose a job scheduling mechanism that extends the backfilling algorithm to become energy-aware. Simultaneously, we adapt resource management with a node shutdown technique to minimize energy consumption whenever needed. This combination enables an efficient energy consumption budget control on a cluster during a period of time. The technique is experimented, validated and compared with various alternatives through extensive simulations. Experimentation results show high system utilization and limited bounded slowdown along with interesting outcomes in energy efficiency while respecting an energy budget during a particular time period.*

**Index Terms:** HPC; Resource Management; Scheduling; Energy Budget.

---

## 2. The Problem

### Scale

Sunway TaihuLight (TOP500 leader at time of writing) develops 93 petaflop/s and consumes **15 MW**. For such platforms, the electricity bill over their lifespan is **roughly equal to their hardware cost**. Energy is the primary obstacle to exascale computing.

### Existing approach: Powercapping

Powercapping limits instantaneous power to a fixed threshold. It controls energy as `∫ power dt = energy`.

**Drawback:** Power is controlled independently of the instant load. A recent study [3] showed that while the upper power-bound is important, power variations do not affect the final energy cost in most use cases. Powercapping gives **no flexibility** to adapt scheduling to a longer-term energy budget.

**Previous work by same authors [6]:** Controlling only power increases the turnaround time of big jobs (harder to "fit" in a powercap). This is why they focus on energy budgeting — keep the benefit of cost control without discriminating against any type of job.

### Why not DVFS?

Controlling energy of jobs with DVFS is not trivial [6, 7]. A given DVFS value may either increase or decrease total energy consumption depending on application type. Without precise knowledge of each job, the scheduler cannot guarantee DVFS reduces energy. Not used in this paper.

### The proposal: Energy Budget Control

Define:
- A **time frame** `[t_s, t_e]`
- A **total energy limit** `B` in **joules**

Constraint: energy consumed by the whole platform during `[t_s, t_e]` must **not exceed B**.

This is more flexible than powercapping: if the cluster saves energy in one period, it can burst above the average rate later.

---

## 3. Section II — Problem Description

### A. Jobs in HPC

A job `j` is defined by:
- `r_j` — release date (submission time)
- `m_j` — number of requested processors
- `wall_j` — walltime: user-provided upper bound on runtime; job is **killed** if it exceeds this
- `p_j` — real processing time: **NOT known in advance**

Simplification: all processors are totally ordered; a job must run on neighboring processors.

### B. Energy Model

Three processor states:

| State | Power |
|---|---|
| Running a job | P_comp |
| Idle (on, not computing) | P_idle |
| Switched off | P_off |

Transition costs:
- Switch off: P_on→off during t_on→off
- Switch on: P_off→on during t_off→on

**Opportunistic shutdown:** Shut down idle nodes (P_off << P_idle → energy savings). Cost: switching on/off takes several minutes at maximum power.

**Energy model justification:** A more precise measure of energy would have prohibitive cost — hardware sensors are not accurate enough [5], software architecture cost, data management cost. The simple model is sufficient for the use cases studied (verified by experiments in Section V-B).

### C. Three Evaluation Metrics

1. **Utilization** — proportion of processors used during a time period (cluster owner perspective)
2. **AVEbsld** — average bounded slowdown (end-user perspective)
3. **Energy consumed** — not minimized, but **controlled** in a period of time

---

## 4. Section III — Related Work

### Why not existing powercap solutions?

**Powercap mechanisms have two major drawbacks:**
1. Require high knowledge about running applications (to tune DVFS or similar)
2. Delay big jobs (harder to fit in a powercap)

**Gholkar et al. [15]:** 2-level hierarchical powercapping based on RAPL — adapted to modern Intel processors but not architecture-independent.

**Elissev et al. [17]:** Energy-aware scheduling on Bluewonder using CPU frequency scaling, implemented on LSF (proprietary RJMS). Manages cluster energy budget but no observed energy reduction.

**Murali et al. [18]:** Metascheduler for multiple HPC centers, reduces overall cost by adapting energy consumption to electricity price.

**Yang et al. [19]:** 2-period scheduling (one with energy limit, one without) — not scalable, hardly extendable.

**Khemka et al. [20]:** Maximize "utility" function with daily energy budget via offline heuristic. Uses utility functions vs. classic scheduling objectives used in this paper.

### Resource and Job Management Systems

Current HPC centers use one software called the **RJMS (Resources and Jobs Management System)**, or simply the scheduler. It monitors resources and executes parallel jobs. At this scale, scheduling must be very efficient → **greedy algorithms such as EASY Backfilling [4]** are commonly used. These algorithms do not take energy into account.

---

## 5. Algorithm 1 — EASY Backfilling (exact from paper)

```
Algorithm 1: The EASY Backfilling Algorithm

for job ∈ queue do
    if system has enough processors to start job now
    then
        launch job;
        remove job from queue;
    else
        break;
    end
end

firstJob = pop first element of queue;
Reserve processors in the future for firstJob;

for job ∈ queue do
    if system has enough processors to start job now
       and does not overlap with firstJob reservation
    then
        launch job;
        remove job from queue;
    end
end

Remove the processor reservation of firstJob;
Push back firstJob at the top of queue;
```

### Why EASY is widely used (from paper)

> *"This EASY backfilling policy is quite aggressive since only the first job in the queue cannot be delayed by backfilled jobs, which leads to an increased resource utilization rate. The popularity of this algorithm can then be explained by: 1) the ease of implementation, 2) the ease of extending the basic policy, 3) the high resource utilization rate implied by this aggressive backfilling policy, and 4) the scalability of being present-focused."*

### When is the algorithm called?

> *"The EASY Backfilling algorithm is called whenever a job arrives or some resources are freed."*

---

## 6. Section IV — Proposed Algorithm

### A. Desired Properties

The algorithm must:
1. **Strictly respect** the energy budget
2. Be **modular** enough to support extra features (e.g., opportunistic shutdown)
3. Be **efficient on large-scale platforms**
4. **Avoid dramatic changes** over currently implemented solutions

### B. Two Rules

To comply with the energy budget under all circumstances:

**Rule 1 — No early overspending:**
> *"Avoid spending the whole budget too early, as it would unbalance the performances during the budget period. To this end, the budget's energy is made available gradually over time, at a rate of B/(t_e − t_s) joules per second."*

**Rule 2 — No energy debts:**
> *"Never have energy debts. Thus, before taking the decision of running a job, we have to ensure that enough energy is available for the entire duration of the job execution. This comprises taking the past, present and future power consumption of the whole cluster into account."*

**Paper's analogy:** *"If we replace energy by money and running jobs by buying stuff, these two rules could describe how someone that never wants to be in debt would manage his monthly paycheck."*

### Figure 1 — Core Idea

The paper's Figure 1 shows:
- A power rate line at `B/(t_e - t_s)` W
- Job 1 runs **below** this rate → energy is **saved** (area labeled "1")
- That saved energy is then **spent** to run job 2 which temporarily exceeds the rate (area labeled "2")

This is impossible with a strict powercap — it would block job 2. `energyBud` accumulates savings and reinvests them.

---

## 7. Algorithm 2 — energyBud (exact from paper)

The paper shows Algorithm 2 with **additions over EASY Backfilling underlined and colored in brown**:

```
Algorithm 2: Energy Budget Backfilling Algorithm

for job ∈ queue do
    if system has enough processors
       AND enough energy is available to start job now    ← NEW
    then
        launch job;
        remove job from queue;
    else
        break;
    end
end

firstJob = pop first element of queue;
Reserve processors in the future for firstJob;
Reserve energy in the future for firstJob;               ← NEW

for job ∈ queue do
    if system has enough processors
       AND enough energy is available to start job now    ← NEW
       AND does not overlap with firstJob reservation
    then
        launch job;
        remove job from queue;
    end
end

Remove the processor reservation of firstJob;
Remove the energy reservation of firstJob;               ← NEW
Push back firstJob at the top of queue;
```

Only **4 additions** over EASY:
1. Energy check before launching in greedy pass
2. Energy reservation for firstJob (lock energy)
3. Energy check before launching in backfill pass
4. Release energy reservation after backfill pass

### Why energy reservation for firstJob is critical

> *"However, because of our set of rules, if jobs were backfilled the usual fashion, they might delay the first job by stealing its energy. Consequently, our algorithm also makes energy reservation for the first job when it cannot be started immediately."*

Without energy reservation: backfill jobs consume `C_ea` → when firstJob's processor reservation time `q` arrives, there is no energy left → firstJob still cannot start → **infinite starvation**.

### When does energyBud run?

> *"Our algorithm is run in the same cases [as EASY], but also when more energy is available, namely at every monitoring stage."*

### Special cases

> *"In the particular case of the energy budget is unlimited (B = ∞), our algorithm produces the same schedules as EASY Backfilling's ones. Additionally, if the energy budget B is very small, our algorithm will start the jobs in the order of the queue."*

---

## 8. Implementation Details: C_ea Counter

### What is C_ea?

> *"A counter named C_ea stores the amount of available energy, i.e. the amount of energy that the algorithm is allowed to spend at the present time. C_ea equals to the amount of energy made available since the beginning of the budget period (via rule 1), minus the energy which has been consumed by the cluster."*

### C_ea update at every algorithm call

```
// Step 1: Accrue budget (Rule 1)
C_ea += (B / (t_e - t_s)) × elapsed_time

// Step 2: Deduct estimated consumption
C_ea -= (n_idle × P̃_idle + n_comp × P̃_comp) × elapsed_time

// Step 3: If making a reservation for firstJob
C_ea -= E(firstJob)      // lock energy

// Step 4: When releasing reservation
C_ea += E(firstJob)      // unlock
```

### C_ea update at every monitoring stage (every 10 min)

```
C_ea += (estimated_consumption_since_last_monitor - real_consumption_measured)
```

> *"Monitoring stages allow to obtain the amount of energy which has really been consumed by the cluster. Therefore, C_ea is incremented during these stages (because the algorithm always overestimates this amount of energy)."*

### Energy Consumption Estimation (Section IV.C.2)

The algorithm uses **overestimated** power values P̃_comp and P̃_idle (maximum observed, not average):

> *"With overestimated P̃_comp and P̃_idle, the algorithm will overestimate the energy consumption of the cluster in the future. However, at every monitoring stage, the real energy consumption is measured and C_ea is updated with the available energy minus the energy actually consumed. Thus, this overestimation leads to saving more energy in the counter, which will allow more jobs to be started later on."*

Platform energy estimation formula:
```
P̃_platform = n_idle × P̃_idle + n_comp × P̃_comp
```

- To estimate P̃_comp: run a CPU-intensive benchmark (e.g., LINPACK), use the **maximum** observed value.
- To estimate P̃_idle: observe processors doing nothing, use the **maximum** observed value.

### Monitoring Period Choice

> *"Finally, we chose a monitoring period of 10 minutes, as it appears to be a good trade-off between precision and monitoring overhead. This choice is complex as it depends on the available energy sensors and the way to gather data from the computing nodes to the controlling node."*

> *"The more precise the monitoring of the energy is, the more precisely our algorithm will respect the energy budget. Increasing the monitoring period increases overheads as a fraction of the computing resources has to be used to gather and transmit the data."*

### Interactions with Opportunistic Shutdown (Section IV.C.3)

> *"The algorithm we proposed does not need to be modified to work with opportunistic shutdown. Indeed, the power consumption of an off node is lesser than an idle node's one. This leads our algorithm to overestimate even more the cluster's energy consumption, which would make a greater amount of energy available after monitoring stages."*

When a node is shut down: real power = P_off = 9.75 W, but algorithm estimates P̃_idle = 100 W. Gap per node per second = 90.25 W. At monitoring stage this large surplus is returned to C_ea → many more jobs can run.

---

## 9. reducePC — Alternative Closer to Powercap (Section IV.D)

### Concept

> *"Making B/(t_e − t_s) joules available each second is close to having a powercap limit of B/(t_e − t_s). The rules introduced previously can be seen as rules which allows to violate the powercap in some cases (these cases being mostly 'when energy is available')."*

The paper proposes a **slightly modified** version of energyBud that is even closer to powercap behavior: **`reducePC`**.

### The Key Difference (exact from paper)

> *"The difference between energyBud and reducePC lies in how the jobs respect their energy reservation. In energyBud, the reserved energy is subtracted from C_ea. In reducePC, we reduce the number of joules made available per second (as if the powercap had been reduced). If job i makes a reservation of J_i joules at time q_i (and thus guarantees to start at q_i), the number of joules available per second is reduced by J_i/(q_i − now) during the time period between now and q_i."*

**energyBud reservation:**
```
C_ea -= J_i
// Other jobs can start if remaining C_ea covers their total energy cost
```

**reducePC reservation:**
```
effective_rate -= J_i / (q_i - now)   [joules/second]
// Other jobs can only start if their power demand ≤ effective_rate
```

### Practical consequence (exact from paper)

> *"The main difference between the two algorithms can be observed when a short job, which uses all the available processors, is being backfilled while there is an ongoing energy reservation. In energyBud, if enough energy is available, the job can be launched. However, in reducePC, as the number of joules available per second has been reduced, the job cannot be started at the present time."*

**Result:** reducePC is more conservative → fewer backfill jobs during reservation periods → more idle nodes → more potential shutdown savings. With `_SHUT`, reducePC can outperform energyBud. With `_IDLE`, energyBud wins.

---

## 10. Section V — Evaluation

### A. Simulator: Batsim [24]

> *"We chose to use Batsim rather than an ad-hoc simulator for separation of concerns, to avoid implementation issues and to ensure the durability of the algorithms we propose."*

Based on SimGrid [25]. All heuristics integrated into Batsim code base. Reproduction scripts: `https://github.com/glesserd/energybudget-expe`

### B. Simulation Calibration

Measured on **Taurus Grid5000 cluster [26]**: 16 Dell PowerEdge R720 nodes, 2× Intel Xeon E5-2630 each, wattmeters at 1-second granularity.

- **Idle:** Reserve nodes, idle for 200 s → average = P_idle, maximum = P̃_idle
- **Compute:** LINPACK benchmark → average = P_comp, maximum = P̃_comp
- **Switch:** 50 switch-on + 50 switch-off operations per node → average → P_off→on, t_off→on, P_on→off, t_on→off

### C. Testset

Three traces from the **Parallel Workload Archive [27]**, 10 disjoint 1-week extracts each = **30 input workloads**:

| Trace | Size | Year | Length |
|---|---|---|---|
| Curie | 80,640 processors | 2012 | 3 months |
| MetaCentrum | 3,356 processors | 2013 | 6 months |
| SDSC-Blue | 1,152 processors | 2003 | 32 months |

High-utilization weeks were selected (scheduling decisions have more impact).

**Budget window:** 3 days in the **middle** of each 1-week trace. The 4 surrounding days run unconstrained.

**Budget levels:** 100%, 90%, 80%, 70%, 60%, 50%, 49%, 30%
- 100% = all processors computing at P_comp for 3 days (no constraint)
- 49% = all processors idle for 3 days (minimum useful budget)
- 30% = below idle energy (extreme stress test)

**4 algorithms × 2 shutdown modes × 30 workloads × 8 budgets = 1,470 total configurations**

**PC algorithm detail:** Power limit = `B / (t_e - t_s)` W. Jobs not executed if they cause `P̃_platform > P_limit`.

---

## 11. Table I — Calibration Values (exact)

| Measure | Value |
|---|---|
| P_idle | 95.00 W |
| P_comp | 190.74 W |
| P_off→on | 125.17 W |
| t_off→on | 151.52 s |
| P_on→off | 101.00 W |
| t_on→off | 6.10 s |
| P_off | 9.75 W |
| **P̃_idle** (estimated, used by algorithm) | **100.00 W** |
| **P̃_comp** (estimated, used by algorithm) | **203.12 W** |
| **monitoring period** | **10 min** |

> *"For the sake of simplicity we attribute the per node measurements, calculated during the calibration, to per processor values in our model."*

---

## 12. Evaluation Metrics (exact formulas)

### Utilization

```
Utilization = Σ_j (m_j × p_j) / (time_period × N)
```

Computed over the **whole week**. Higher is better (cluster owner perspective).

### AVEbsld — Average Bounded Slowdown

```
AVEbsld = (1/n) × Σ_j max( (wait_j + p_j) / max(p_j, τ), 1 )
```

Where:
- `wait_j = start_j − r_j` (waiting time)
- `p_j` = real processing time
- `τ = 10 seconds` (bounding constant, standard in literature)
- `n` = number of jobs

Computed over **all jobs** of each trace. Lower is better. Value of 1 = perfect (no wait).

### Relative Energy Consumed

Energy consumed relative to maximum possible:
```
E_relative = E_actual / (N × P_comp × (t_e - t_s))
```

Not minimized — only controlled and monitored.

### Normalization

All measures are normalized across traces to remove between-subject variability [28]. Graphs present means of normalized measures with traces as the between-subject variable.

---

## 13. Results — Figures and Key Findings

### Figure 2 — Normalized mean utilization vs. budget

`energyBud` outperforms all other algorithms across all budget levels. `reducePC` performs better than `energyBud` only when shutdown is activated for reducePC and deactivated for energyBud. Below 49% budget, algorithms without shutdown all converge (no energy to save via shutdown below idle consumption level).

### Figure 3 — Normalized mean AVEbsld vs. budget

AVEbslds are very high because high-utilization traces were chosen (many jobs waiting). AVEbslds increase more for low budgets as resources are limited during a fair part of the week.

`energyBud` with and without shutdown outperforms all others. Surprisingly, `powercap` is not the worst in AVEbsld.

### Figure 4 — Normalized mean energy consumed vs. budget

> *"It appears clearly that when opportunistic shutdown is on, the cluster consumes less energy. Also, energyBud consumes more energy than reducePC which consumes more than powercap. Our algorithms do not try to minimize the energy consumption. They try to keep it under a certain value."*

> *"powercap has a very low energy consumption which can be seen as a non-utilization of the energy saved. At the opposite, energyBud is quite successful at using saved energy as it has a high utilization while having a high energy consumption."*

### Figure 5 — Performance/Energy Trade-offs (scatter plots at 5 budget levels)

**(a) Utilization vs. Relative energy consumed** — best = upper left (high util, low energy)
**(b) AVEbsld vs. Relative energy consumed** — best = lower left (low AVEbsld, low energy)

`energyBud_SHUT` is consistently in the best region of both plots. `powercap` is on the Pareto curve but at very low utilization.

### E. Comparison with standard powercap

> *"Surprisingly, for an energy budget of 90% all points are above the [theoretical baseline] line. It means that, even with a simple powercap mechanism, we achieve a better energy efficiency than EASY Backfilling. Presumably, the small limitation in energy reduces the fragmentation and thus improves the utilization."*

For `energyBud_SHUT`: above the line at 60% budget and above.
For `energyBud_IDLE`: above the line at 80% budget and above.

### H. Which is the best — reducePC or energyBud?

> *"We consider energyBud as the best of the two proposed algorithms. This algorithm provides the best utilization and AVEbsld results. Even more, we have seen that using this algorithm for not so low energy budget increases the energy efficiency of the cluster compared to the standard EASY Backfilling."*

---

## 14. Table II — Gain from Opportunistic Shutdown (exact)

Percent change when activating shutdown: `(y_SHUT − y_IDLE) / y_IDLE`
**For AVEbsld and Energy: negative values are better.**

| Measure | powercap | reducePC | energyBud |
|---|---|---|---|
| AVEbsld | +0.16% | +0.88% | **−8.61%** |
| Utilization | −0.05% | +4.95% | **+5.74%** |
| Number of jobs started | −0.05% | +1.4% | **+1.47%** |
| Energy consumed | **−4.74%** | −1.78% | −1.42% |

Key reading:
- `energyBud` gains the most from shutdown: −8.61% AVEbsld (dramatically less waiting), +5.74% utilization
- `powercap` saves the most energy (−4.74%) but gains nothing in utilization — the saved energy is **wasted**
- `reducePC` is in between

---

## 15. Theoretical Performance Baseline Formula (exact)

The black line in Figure 2:

```
f(budget) = ū_EASYbf × (3/7 × budget + 4/7)
```

Where:
- `ū_EASYbf` = mean normalized utilization of standard EASY Backfilling
- `3/7` = fraction of the week that is budget-constrained (3 days out of 7)
- `4/7` = fraction that is unconstrained

> *"This is not the identity because the energy budget only lasts 3 days during the 7 days considered."*

**Example at 50% budget:**
```
f(0.5) = ū_EASYbf × (3/7 × 0.5 + 4/7) = ū_EASYbf × 0.786
```
→ 50% budget causes only ~21% expected utilization loss, not 50%.

A point **above** this line = the algorithm achieves **better energy efficiency** than unconstrained EASY Backfilling.

---

## 16. Conclusion (exact)

> *"The purpose of this work was to extend the widely-used EASY backfilling algorithm, to comply with periods during which energy availability is limited.*
>
> *We proposed two new alternatives and showed their effectiveness on a wide range of scenarios, which have been assessed through simulation. These two new algorithms not only provide a way to control the energy consumption of computing platforms, but they also optimized metrics such as system utilization and bounded slowdown.*
>
> *Moreover, when the amount of available energy is large, the algorithms we proposed improve the energy efficiency of the cluster.*
>
> *As this work is an improvement of EASY backfilling, our algorithms still support most existing extensions of this algorithm, such as advanced reservations, preemption mechanisms or the establishment of a maximum power limit."*

**Future work mentioned:**
1. Implement on real RJMS (Slurm, OAR) in production
2. Integrate job-level power control (RAPL, CPU powercapping)
3. Evaluate with GEO [29] (dynamically adapts frequency/power across nodes)
4. Make energy budget dynamic — follow electricity price variations

---

## 17. Key Implementation Reference

### The 3 algorithms

| Algorithm | Core mechanism |
|---|---|
| `PC_IDLE` | `P_limit = B/(t_e−t_s)` W. Don't launch if `P̃_platform + m_j×(P̃_comp−P̃_idle) > P_limit`. |
| `energyBud_IDLE` | Counter `C_ea` accrues at `B/(t_e−t_s)` J/s, deducts estimated consumption, locks energy for firstJob, corrects at monitoring stages. |
| `reducePC_IDLE` | Same as energyBud but reservation reduces effective J/s rate by `J_i/(q_i−now)` instead of subtracting from `C_ea`. |

### Exact parameter values to use

```cpp
double P_IDLE_EST  = 100.00;   // P̃_idle  [W]
double P_COMP_EST  = 203.12;   // P̃_comp  [W]
double MONITORING_PERIOD = 600.0; // 10 minutes [s]
```

### Energy check condition

Before launching job j:
```
C_ea >= m_j × (P̃_comp - P̃_idle) × wall_j
```
(Net extra energy cost: nodes switching from idle to compute state)

### Performance ranking (from paper results)

```
energyBud_SHUT > reducePC_SHUT ≥ energyBud_IDLE > reducePC_IDLE > PC_IDLE
```

For this tutorial (no shutdown): **`energyBud_IDLE`** is the main target algorithm.
