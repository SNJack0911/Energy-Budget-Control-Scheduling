

#include <cmath>
#include <cstdio>
#include <list>
#include <map>
#include <string>

#include <batprotocol.hpp>
#include <intervalset.hpp>
#include <nlohmann/json.hpp>

#include "batsim_edc.h"
#include "energy_common.h"

using namespace batprotocol;
using json = nlohmann::json;
using EventTS = batprotocol::fb::EventAndTimestamp;

//Global state

static MessageBuilder* mb            = nullptr;
static bool            format_binary = true;

static uint32_t    platform_nb_hosts = 0;
static IntervalSet free_hosts;
static uint32_t    n_comp = 0;

static std::list<SchedJob*>              queue;
static std::map<std::string, RunningJob> running_jobs;

static double energy_rate        = 0.0;
static double C_ea               = 0.0;
static double last_update_time   = 0.0;
static double last_monitor_time  = 0.0;
static double t_s_global         = 0.0;
static double t_e_global         = 0.0;
static double pending_correction = 0.0; // overcharge accumulated between monitoring stages

static bool in_budget_window(double now) {
    return now >= t_s_global && now <= t_e_global;
}

//C_ea management

// Idle hosts drain at P_idle (overestimate); computing hosts' extra cost
// (P_comp - P_idle) pre-charge at launch.
// Accumulated overcharge is applied at each 600-s monitoring stage.
static void update_cea(double now) {
    if (!in_budget_window(now)) {
        last_update_time = now;
        return;
    }
    double effective_start = std::max(last_update_time, t_s_global);
    double dt = now - effective_start;
    last_update_time = now;
    if (dt <= 0.0) return;

    // Drain at overestimate: C_ea goes lower than the actual budget would.
    C_ea += (energy_rate - platform_nb_hosts * P_IDLE_EST) * dt;

    // Accumulate the overcharge to be refunded at the next monitoring stage.
    // Computing hosts: overcharge is (P̃_comp - P_comp_actual) per host per second.
    // Idle hosts: overcharge is (P̃_idle - P_idle_actual) per host per second.
    uint32_t n_idle = platform_nb_hosts - n_comp;
    pending_correction += n_comp * (P_COMP_EST - P_COMP_ACTUAL) * dt
                        + n_idle * (P_IDLE_EST - P_IDLE_ACTUAL) * dt;

    // Apply correction inline every MONITORING_PERIOD instead of using callbacks.
    if (now - last_monitor_time >= MONITORING_PERIOD) {
        C_ea += pending_correction;
        pending_correction = 0.0;
        last_monitor_time += MONITORING_PERIOD;
    }
}

//Job cycle

static void launch_job(SchedJob* job, double now) {
    IntervalSet allocated = free_hosts.left(job->nb_hosts);
    free_hosts -= allocated;
    n_comp     += job->nb_hosts;
    // Only pre-charge budget for jobs launched inside the budget window.
    // Jobs launched before t_s must not drain C_ea — update_cea doesn't
    // run outside the window, so there is no mechanism to refund the charge.
    if (in_budget_window(now))
        C_ea -= compute_job_energy(job);

    RunningJob rj;
    rj.job             = job;
    rj.allocated_hosts = allocated;
    rj.start_time      = now;
    running_jobs[job->job_id] = rj;

    mb->add_execute_job(job->job_id, allocated.to_string_hyphen());
}

static void complete_job(const std::string& job_id, double now) {
    auto it = running_jobs.find(job_id);
    if (it == running_jobs.end()) return;

    RunningJob& rj = it->second;
    free_hosts += rj.allocated_hosts;
    n_comp     -= rj.job->nb_hosts;

    // Refund unused walltime pre-charge.
    // At launch we debited nb*(P̃_comp - P̃_idle)*walltime; refund the unused portion.
    // Only applies to jobs launched inside the budget window.
    if (rj.start_time >= t_s_global) {
        double actual_runtime = now - rj.start_time;
        double unused = rj.job->walltime - actual_runtime;
        if (unused > 0.0)
            C_ea += rj.job->nb_hosts * (P_COMP_EST - P_IDLE_EST) * unused;
    }

    delete rj.job;
    running_jobs.erase(it);
}

//Scheduling logic

static std::list<SchedJob*>::iterator run_greedy_pass(double now) {
    bool constrained = in_budget_window(now);
    auto it = queue.begin();
    while (it != queue.end()) {
        SchedJob* job = *it;
        bool has_resources = free_hosts.size() >= job->nb_hosts;
        bool has_energy    = !constrained || C_ea >= compute_job_energy(job);
        if (!has_resources || !has_energy) break;
        launch_job(job, now);
        it = queue.erase(it);
    }
    return it;
}

static void run_backfill_pass(
    std::list<SchedJob*>::iterator first_job_it,
    double now)
{
    if (first_job_it == queue.end()) return;

    bool constrained = in_budget_window(now);
    SchedJob* first_job = *first_job_it;
    double q = find_earliest_start_time(
        first_job->nb_hosts, now, free_hosts, running_jobs);

    double reserved_energy = constrained ? compute_job_energy(first_job) : 0.0;
    C_ea -= reserved_energy;

    auto it = std::next(first_job_it);
    while (it != queue.end()) {
        SchedJob* job = *it;
        bool fits_resources  = free_hosts.size() >= job->nb_hosts;
        bool has_energy      = !constrained || C_ea >= compute_job_energy(job);
        bool finishes_before = (now + job->walltime) <= q;

        if (fits_resources && has_energy && finishes_before) {
            launch_job(job, now);
            it = queue.erase(it);
        } else {
            ++it;
        }
    }

    C_ea += reserved_energy;
}

// If the queue head is blocked only on energy (has enough hosts), schedule one
// targeted wakeup callback at the time C_ea will be sufficient.  This avoids
// the simulation terminating with queued jobs when no running jobs remain.
static void schedule_energy_wakeup_if_needed(
    std::list<SchedJob*>::iterator first_job_it, double now)
{
    if (first_job_it == queue.end()) return;
    if (!in_budget_window(now)) return;

    SchedJob* job = *first_job_it;
    if (free_hosts.size() < job->nb_hosts) return; // blocked on resources, not energy

    double needed = compute_job_energy(job);
    if (C_ea >= needed) return; // energy already sufficient (shouldn't happen here)

    double net_rate = energy_rate - platform_nb_hosts * P_IDLE_EST;
    if (net_rate <= 0.0) return;

    double wakeup_time = now + (needed - C_ea) / net_rate;
    if (wakeup_time < t_e_global) {
        auto trigger = TemporalTrigger::make_one_shot(
            static_cast<uint64_t>(std::ceil(wakeup_time)));
        mb->add_call_me_later("energy_wakeup", trigger);
    }
}

static void run_easy_backfilling_with_energy_budget(double now) {
    auto first_job_it = run_greedy_pass(now);
    run_backfill_pass(first_job_it, now);
    schedule_energy_wakeup_if_needed(first_job_it, now);
}

//Event handlers

static void handle_simulation_begins(const EventTS* event, double now) {
    auto* e = event->event_as_SimulationBeginsEvent();
    platform_nb_hosts = e->computation_host_number();
    free_hosts = IntervalSet(IntervalSet::ClosedInterval(0, platform_nb_hosts - 1));
    n_comp = 0;
    last_update_time = now;
    last_monitor_time = t_s_global;
}

static void handle_job_submitted(const EventTS* event, double now) {
    auto* e   = event->event_as_JobSubmittedEvent();
    auto* job = new SchedJob();
    job->job_id   = e->job_id()->str();
    job->nb_hosts = e->job()->resource_request();
    job->walltime = e->job()->walltime();

    if (job->nb_hosts > platform_nb_hosts || job->walltime <= 0) {
        mb->add_reject_job(job->job_id);
        delete job;
        return;
    }
    queue.push_back(job);
}

static void handle_job_completed(const EventTS* event, double now) {
    auto* e = event->event_as_JobCompletedEvent();
    complete_job(e->job_id()->str(), now);
}

//EDC

uint8_t batsim_edc_init(const uint8_t* data, uint32_t size, uint32_t flags) {
    format_binary = ((flags & BATSIM_EDC_FORMAT_BINARY) != 0);
    if ((flags & (BATSIM_EDC_FORMAT_BINARY | BATSIM_EDC_FORMAT_JSON)) != flags) {
        printf("energybud_idle: unknown flags, cannot initialize.\n");
        return 1;
    }

    mb = new MessageBuilder(!format_binary);

    std::string init_str(reinterpret_cast<const char*>(data), size);
    auto params = json::parse(init_str);
    double B   = params["B"].get<double>();
    double t_s = params["t_s"].get<double>();
    double t_e = params["t_e"].get<double>();
    energy_rate = B / (t_e - t_s);
    C_ea        = 0.0;
    t_s_global  = t_s;
    t_e_global  = t_e;

    printf("energybud_idle: B=%.2e J, t_s=%.0f s, t_e=%.0f s, rate=%.2f W\n",
           B, t_s, t_e, energy_rate);
    return 0;
}

uint8_t batsim_edc_deinit() {
    delete mb;
    mb = nullptr;

    for (auto* job : queue) delete job;
    queue.clear();

    for (auto& [id, rj] : running_jobs) delete rj.job;
    running_jobs.clear();

    pending_correction = 0.0;
    last_monitor_time  = 0.0;
    C_ea = 0.0;

    return 0;
}

uint8_t batsim_edc_take_decisions(
    const uint8_t*  what_happened,
    uint32_t        what_happened_size,
    uint8_t**       decisions,
    uint32_t*       decisions_size)
{
    (void) what_happened_size;

    auto* parsed = deserialize_message(*mb, !format_binary, what_happened);
    double now = parsed->now();
    mb->clear(now);

    auto nb_events = parsed->events()->size();
    for (unsigned int i = 0; i < nb_events; ++i) {
        auto* event = (*parsed->events())[i];
        switch (event->event_type()) {
            case fb::Event_BatsimHelloEvent:
                mb->add_edc_hello("energybud_idle", "0.1.0");
                break;
            case fb::Event_SimulationBeginsEvent:
                handle_simulation_begins(event, now);
                break;
            case fb::Event_JobSubmittedEvent:
                handle_job_submitted(event, now);
                break;
            case fb::Event_JobCompletedEvent:
                handle_job_completed(event, now);
                break;
            default:
                break;
        }
    }

    update_cea(now);

    run_easy_backfilling_with_energy_budget(now);

    mb->finish_message(now);
    serialize_message(*mb, !format_binary,
                      const_cast<const uint8_t**>(decisions), decisions_size);
    return 0;
}
