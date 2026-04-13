#pragma once

#include <string>
#include <cstdint>
#include <vector>
#include <map>
#include <algorithm>
#include <batprotocol.hpp>
#include <intervalset.hpp>

// Power constants from paper, for the 512-host SDSC Blue cluster.
// P_idle / P_comp are overestimates used by the scheduler; actual measured values
// are used in the monitoring correction to refund the accumulated overcharge.
static constexpr double P_IDLE_EST        = 100.000; // P_idle [W]: estimated idle power (overestimate)
static constexpr double P_COMP_EST        = 203.120; // P_comp [W]: estimated compute power (overestimate)
static constexpr double P_OFF_EST         =   9.750; // P_off  [W]: switched-off host standby power
static constexpr double MONITORING_PERIOD =   600.0; // 10 minutes [s]

// Actual measured power (used for monitoring correction in energyBud/reducePC).
static constexpr double P_IDLE_ACTUAL =  95.000; // W: actual measured idle power
static constexpr double P_COMP_ACTUAL = 190.740; // W: actual measured compute power

// A job waiting in the queue
struct SchedJob {
    std::string job_id;
    uint32_t    nb_hosts;
    double      walltime;  // wall_j: user-provided upper bound on runtime [s]
};

// A job currently running on the platform
// Note: IntervalSet is in global namespace (separate library from batprotocol)
struct RunningJob {
    SchedJob*   job;
    IntervalSet allocated_hosts;
    double      start_time; // simulation time when job was launched [s]
};

// Net extra energy cost of running job j (its hosts switch from idle to compute).
// Uses overestimated constants (P̃_comp, P̃_idle) as per the paper.
// The monitoring correction at each 600-s stage refunds the overcharge.
// E_job(j) = m_j * (P̃_comp - P̃_idle) * wall_j
inline double compute_job_energy(const SchedJob* job) {
    return job->nb_hosts * (P_COMP_EST - P_IDLE_EST) * job->walltime;
}

// Find the earliest simulation time when nb_hosts_needed hosts will be free.
// Uses job walltimes as upper bounds on completion time.
inline double find_earliest_start_time(
    uint32_t nb_hosts_needed,
    double now,
    const IntervalSet& free_hosts,
    const std::map<std::string, RunningJob>& running_jobs)
{
    if (free_hosts.size() >= nb_hosts_needed)
        return now;

    std::vector<std::pair<double, uint32_t>> releases;
    releases.reserve(running_jobs.size());
    for (const auto& [id, rj] : running_jobs)
        releases.push_back({rj.start_time + rj.job->walltime, rj.job->nb_hosts});
    std::sort(releases.begin(), releases.end());

    uint32_t available = free_hosts.size();
    for (const auto& [completion_time, hosts_freed] : releases) {
        available += hosts_freed;
        if (available >= nb_hosts_needed)
            return completion_time;
    }
    return now; // unreachable if platform has enough total hosts
}
