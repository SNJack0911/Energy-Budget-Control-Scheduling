# Energy Budget Scheduling with Batsim

Implementation of six energy-aware scheduling algorithms from:
> Dutot et al., *Towards Energy Budget Control in HPC*, CCGrid 2017

Evaluated on the SDSC Blue Horizon trace (512 hosts) across 10 weekly workloads and 8 budget levels.

---

## Build

Requires a nix shell (`nix develop`) or a manual install of `batprotocol-cpp`, `intervalset`, `nlohmann_json`, Meson, and Ninja.

```sh
meson setup build
ninja -C build
```

---

## Run

```sh
bash run_all.sh
```

Runs 6 algorithms × 10 workloads × 8 budget levels (480 simulations) plus 10 unconstrained EASY baseline runs (490 total).

Results are written to `out/<algo>/budget<pct>/<workload>/` and `out/easy/baseline/<workload>/`.

Each run produces:
- `jobs.csv` — per-job timing and outcome
- `schedule.csv` — aggregate statistics (makespan, job counts)
- `consumed_energy.csv` — cumulative energy from Batsim's energy plugin
- `pstate_changes.csv` — host power state transitions

### Budget window

The energy constraint is active for the middle 3 days of each 7-day trace:

| Parameter | Value | Description |
|-----------|-------|-------------|
| `t_s` | `172800 s` (day 2) | Budget window start |
| `t_e` | `432000 s` (day 5) | Budget window end |

### Budget levels

| Level | Joules | Notes |
|-------|--------|-------|
| 100% | 2.531×10¹⁰ J | All hosts computing for 3 days |
| 90%  | 2.278×10¹⁰ J | |
| 80%  | 2.025×10¹⁰ J | |
| 70%  | 1.772×10¹⁰ J | |
| 60%  | 1.519×10¹⁰ J | |
| 50%  | 1.266×10¹⁰ J | |
| 49%  | 1.240×10¹⁰ J | ≈ all hosts idle for 3 days (idle energy floor) |
| 30%  | 7.594×10⁹ J  | Below idle floor — only achievable with shutdown |

### Power constants (Table I of the paper)

| Constant | Value | Role |
|----------|-------|------|
| `P̃_idle` | 100 W | Estimated idle power — used by scheduler (overestimate) |
| `P̃_comp` | 203.12 W | Estimated compute power — used by scheduler (overestimate) |
| `P_off` | 9.75 W | Switched-off host standby power |
| `P_idle_actual` | 95 W | Actual idle power (from platform XML) — used in monitoring correction |
| `P_comp_actual` | 190.74 W | Actual compute power (from platform XML) — used in monitoring correction |
| Monitoring period | 600 s | Interval at which overcharge correction is applied to C_ea |

---

## Algorithms

All six use EASY backfilling as the base scheduling policy. A seventh (`easy`) serves as the unconstrained baseline.

### easy (baseline)

Pure EASY backfilling with no energy constraints. Used as the reference baseline.

### PC (Power Cap)

Converts the budget to a constant power ceiling: `P_limit = B / (t_e − t_s)`.
A job launches only if the instantaneous platform power after launch stays within `P_limit`.
No joule counter — purely an instantaneous check.

### energyBud

Maintains a joule counter `C_ea` accruing at `B / (t_e − t_s)` J/s.
Jobs are pre-charged at launch: `E_job = m_j × (P̃_comp − P̃_idle) × wall_j`.
Every 600 s a monitoring correction refunds the accumulated overcharge (estimated minus actual power).
Backfill temporarily reserves `E_job(firstJob)` in `C_ea` before scheduling smaller jobs.

### reducePC

Same as energyBud but the backfill reservation reduces the effective per-second rate instead of locking a lump sum:
`effective_rate = energy_rate − E_job(firstJob) / (q − now)`.
A backfill job passes if its average power demand fits within this reduced rate.

### IDLE vs SHUTDOWN variants

Each algorithm has two variants:

- **IDLE** — idle hosts remain on at `P̃_idle`
- **SHUTDOWN** — idle hosts are switched off to `P_off` after each scheduling pass and woken on demand. The energy saved is recaptured into `C_ea` via the monitoring correction, allowing more jobs to launch.

---

## Analysis

Open and run `analysis.ipynb` after simulations complete.

Produces three charts saved to `out/`:
- `utilization.png` — normalized system utilization vs budget level (with unconstrained EASY baseline)
- `energy.png` — relative energy consumed vs budget level (full-week energy / 7-day peak)
- `avebsld.png` — average bounded slowdown vs budget level

Metrics are averaged across all 10 weekly workloads per algorithm per budget level.
