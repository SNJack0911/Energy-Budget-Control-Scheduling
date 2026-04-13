

#include <cstdio>
#include <list>
#include <map>
#include <string>

#include <batprotocol.hpp>
#include <intervalset.hpp>

#include "batsim_edc.h"
#include "energy_common.h"

using namespace batprotocol;
using EventTS = batprotocol::fb::EventAndTimestamp;

// Global state

static MessageBuilder* mb            = nullptr;
static bool            format_binary = true;

static uint32_t    platform_nb_hosts = 0;
static IntervalSet free_hosts;

static std::list<SchedJob*>              queue;
static std::map<std::string, RunningJob> running_jobs;

//Job cycle

static void launch_job(SchedJob* job, double now) {
    IntervalSet allocated = free_hosts.left(job->nb_hosts);
    free_hosts -= allocated;

    RunningJob rj;
    rj.job             = job;
    rj.allocated_hosts = allocated;
    rj.start_time      = now;
    running_jobs[job->job_id] = rj;

    mb->add_execute_job(job->job_id, allocated.to_string_hyphen());
}

static void complete_job(const std::string& job_id) {
    auto it = running_jobs.find(job_id);
    if (it == running_jobs.end()) return;

    RunningJob& rj = it->second;
    free_hosts += rj.allocated_hosts;
    delete rj.job;
    running_jobs.erase(it);
}

// Scheduling logic

// greedy FCFS: stop at the first job that does not fit on free hosts.
static std::list<SchedJob*>::iterator run_greedy_pass(double now) {
    auto it = queue.begin();
    while (it != queue.end()) {
        SchedJob* job = *it;
        if (free_hosts.size() < job->nb_hosts) break;
        launch_job(job, now);
        it = queue.erase(it);
    }
    return it;
}

// backfill: launch jobs that fit and finish before firstJob's shadow time.
static void run_backfill_pass(
    std::list<SchedJob*>::iterator first_job_it,
    double now)
{
    if (first_job_it == queue.end()) return;

    SchedJob* first_job = *first_job_it;
    double q = find_earliest_start_time(
        first_job->nb_hosts, now, free_hosts, running_jobs);

    auto it = std::next(first_job_it);
    while (it != queue.end()) {
        SchedJob* job = *it;
        bool fits_resources  = free_hosts.size() >= job->nb_hosts;
        bool finishes_before = (now + job->walltime) <= q;

        if (fits_resources && finishes_before) {
            launch_job(job, now);
            it = queue.erase(it);
        } else {
            ++it;
        }
    }
}

static void run_easy_backfilling(double now) {
    auto first_job_it = run_greedy_pass(now);
    run_backfill_pass(first_job_it, now);
}

// Event handlers

static void handle_simulation_begins(const EventTS* event) {
    auto* e = event->event_as_SimulationBeginsEvent();
    platform_nb_hosts = e->computation_host_number();
    free_hosts = IntervalSet(IntervalSet::ClosedInterval(0, platform_nb_hosts - 1));
}

static void handle_job_submitted(const EventTS* event) {
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

static void handle_job_completed(const EventTS* event) {
    auto* e = event->event_as_JobCompletedEvent();
    complete_job(e->job_id()->str());
}

// EDC

uint8_t batsim_edc_init(const uint8_t* data, uint32_t size, uint32_t flags) {
    format_binary = ((flags & BATSIM_EDC_FORMAT_BINARY) != 0);
    if ((flags & (BATSIM_EDC_FORMAT_BINARY | BATSIM_EDC_FORMAT_JSON)) != flags) {
        printf("easy: unknown flags, cannot initialize.\n");
        return 1;
    }

    mb = new MessageBuilder(!format_binary);
    (void) data; (void) size;  // init JSON not used

    printf("easy: unconstrained EASY backfilling\n");
    return 0;
}

uint8_t batsim_edc_deinit() {
    delete mb;
    mb = nullptr;

    for (auto* job : queue) delete job;
    queue.clear();

    for (auto& [id, rj] : running_jobs) delete rj.job;
    running_jobs.clear();

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
                mb->add_edc_hello("easy", "0.1.0");
                break;
            case fb::Event_SimulationBeginsEvent:
                handle_simulation_begins(event);
                break;
            case fb::Event_JobSubmittedEvent:
                handle_job_submitted(event);
                break;
            case fb::Event_JobCompletedEvent:
                handle_job_completed(event);
                break;
            default:
                break;
        }
    }

    run_easy_backfilling(now);

    mb->finish_message(now);
    serialize_message(*mb, !format_binary,
                      const_cast<const uint8_t**>(decisions), decisions_size);
    return 0;
}
