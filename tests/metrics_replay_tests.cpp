#include "core_test_support.hpp"
#include "cps/core/replay.hpp"
#include <map>
#include <set>

using namespace cps;
using test::job;
using test::require;
using test::near;
using test::claim;
namespace {
void handCalculatedSinglePrinter() {
    const std::vector<Job> rows{job("long",20), job("short",2), job("medium",5)};
    const auto f = replay(rows, Policy::Fcfs, {1,1000,30000});
    const auto s = replay(rows, Policy::ShortestJobFirst, {1,1000,30000});
    near(f.metrics.averageFirstWaitMs, 14000, "FCFS waits 0,20,22 seconds");
    near(s.metrics.averageFirstWaitMs, 3000, "SJF waits 0,2,7 seconds");
    near(f.metrics.averageTurnaroundMs, 23000, "FCFS completes at 20,22,27 seconds");
    near(s.metrics.averageTurnaroundMs, 12000, "SJF completes at 2,7,27 seconds");
    near(s.metrics.utilization, 1, "one continuously occupied printer");
    near(s.metrics.throughputPerSecond, 3.0/27, "throughput in tasks per second");
    require(f.finishedMs == 27000 && s.finishedMs == 27000, "same total work");
}
void simultaneousArrivalsAndIdleTime() {
    const std::vector<Job> rows{job("future",1,2000), job("long",2), job("other",1), job("queued",1)};
    const auto r = replay(rows, Policy::ShortestJobFirst, {2,1000,100});
    require(r.dispatches.size() == 4 && r.dispatches[0].jobId == "other" && r.dispatches[1].jobId == "queued",
            "all same-time arrivals compete before dispatch");
    require(r.dispatches[2].jobId == "long" && r.dispatches[2].startedMs == 1000, "remaining large job starts at release");
    require(r.dispatches[3].jobId == "future" && r.dispatches[3].startedMs == 2000, "no future knowledge at time zero");
    near(r.metrics.utilization, 5.0/6, "idle slot included in denominator");
    const auto shifted = replay({job("one",1,5000), job("later",1,8000)}, Policy::Fcfs, {2,1000,100});
    require(shifted.startedMs == 5000 && shifted.finishedMs == 9000, "window starts at first arrival");
    near(shifted.metrics.utilization, 0.25, "gap between jobs remains in window");
}
void completionArrivalTie() {
    const auto r=replay({job("first",1), job("waiting",5), job("arriving",1,1000)},
                        Policy::ShortestJobFirst,{1,1000,100});
    require(r.dispatches[1].jobId == "arriving" && r.dispatches[1].startedMs == 1000,
            "arrival at release competes with queued work");
}
void retryAndOutcomeCohorts() {
    Scheduler s; s.submit(job("success")); claim(s, Policy::Fcfs, 10);
    s.retry("success",1,30); claim(s,Policy::Fcfs,50); s.finish("success",2,true,80);
    s.submit(job("failure",1,100)); claim(s,Policy::Fcfs,110); s.finish("failure",1,false,120);
    s.submit(job("cancel",1,130)); s.cancel("cancel",140);
    s.submit(job("running",1,150)); claim(s,Policy::Fcfs,160);
    s.submit(job("queued",1,170));
    const auto stats=statistics(s.snapshot(),0,200,2);
    require(stats.completed==1 && stats.failed==1 && stats.cancelled==1 && stats.firstStarted==3,
            "failed/active starts remain in first-wait cohort");
    require(stats.queued==1 && stats.running==1, "current state counts");
    near(stats.averageFirstWaitMs,10,"first waits are 10 ms each");
    near(stats.averageTotalQueueWaitMs,30,"success has 10 + 20 ms total queue waiting");
    near(stats.averageTurnaroundMs,80,"only successful task in completion mean");
    near(stats.busyPrinterMs,100,"20 + 30 + 10 + 40 ms occupancy");
    near(stats.utilization,0.25,"two-printer observation window");
    require(stats.maxCurrentQueueWaitMs==30,"unfinished queue waiting includes current interval");
}
void windowsAndZeroSamples() {
    Scheduler s; s.submit(job("a")); claim(s,Policy::Fcfs,10); s.finish("a",1,true,30);
    s.submit(job("b",1,30)); claim(s,Policy::Fcfs,30); s.finish("b",1,true,50);
    const auto stats=statistics(s.snapshot(),20,50,1);
    require(stats.submitted==1 && stats.firstStarted==1 && stats.completed==2,"closed event cohorts at window boundaries");
    near(stats.busyPrinterMs,30,"busy intervals clipped, same-time release before start");
    near(stats.averageFirstWaitMs,0,"first start before window excluded");
    near(stats.averageTurnaroundMs,25,"completed cohort includes lifetime turnaround");
    const auto empty=statistics({},0,0,2);
    near(empty.utilization,0,"zero-duration utilization");near(empty.averageFirstWaitMs,0,"empty mean");
    require(replay({},Policy::Fcfs).jobs.empty(),"empty workload allowed");
    Scheduler future;future.submit(job("future",1,100));
    require(statistics(future.snapshot(),0,50,1).submitted==0,"future job excluded from current statistics");
}
void capacityAndInvalidWindows() {
    Scheduler s;s.submit(job("a"));s.submit(job("b"));claim(s,Policy::Fcfs,0);claim(s,Policy::Fcfs,0);
    test::throws<std::invalid_argument>([&]{ statistics(s.snapshot(),0,10,1); },"overlapping histories exceed capacity");
    test::throws<std::invalid_argument>([]{ statistics({},-1,0,1); },"negative start rejected");
    test::throws<std::invalid_argument>([]{ statistics({},2,1,1); },"reversed window rejected");
    test::throws<std::invalid_argument>([]{ statistics({},0,1,0); },"zero printer count rejected");
    s.finish("a",1,true,20);
    test::throws<std::invalid_argument>([&]{ statistics(s.snapshot(),0,10,2); },"cannot treat later snapshot as earlier one");
}
void replayValidation() {
    test::throws<std::invalid_argument>([]{ replay({job("x"),job("x")},Policy::Fcfs); },"duplicate IDs rejected");
    test::throws<std::invalid_argument>([]{ replay({job("x",0)},Policy::Fcfs); },"invalid job rejected");
    test::throws<std::invalid_argument>([]{ replay({},Policy::Fcfs,{0,1,1}); },"invalid printer count");
    test::throws<std::invalid_argument>([]{ replay({},Policy::Fcfs,{2,0,1}); },"zero service time");
    test::throws<std::invalid_argument>([]{ replay({},Policy::Fcfs,{2,1,0}); },"invalid aging interval");
    test::throws<std::overflow_error>([]{ replay({job("edge",1,MaxTimeMs)},Policy::Fcfs); },"timeline overflow");
}
void determinismAndConservation() {
    std::vector<Job> input;
    TimeMs sheets=0;
    for(int i=0;i<80;++i){input.push_back(job(std::to_string(i),i%11+1,(i%20)*37,i%5));sheets+=i%11+1;}
    for(const auto policy:{Policy::Fcfs,Policy::ShortestJobFirst,Policy::PriorityAging}) {
        const auto first=replay(input,policy,{2,13,100});const auto second=replay(input,policy,{2,13,100});
        std::set<std::string> seen;std::map<int,TimeMs> available;
        require(first.dispatches.size()==input.size(),"one dispatch per input");
        for(std::size_t i=0;i<first.dispatches.size();++i){
            const auto& a=first.dispatches[i];const auto& b=second.dispatches[i];
            require(a.jobId==b.jobId && a.printer==b.printer && a.startedMs==b.startedMs && a.finishedMs==b.finishedMs,
                    "deterministic dispatch trace");
            require(seen.insert(a.jobId).second && a.startedMs>=available[a.printer],"unique jobs and no slot overlap");
            available[a.printer]=a.finishedMs;
        }
        near(first.metrics.busyPrinterMs,static_cast<double>(sheets*13),"all submitted sheets conserve service time");
        for(const auto& j:first.jobs)require(*j.firstStartedMs>=j.submittedMs && j.state==JobState::Completed,"no premature/unfinished jobs");
    }
}
}
int main(){return test::run({{"hand-calculated one-printer strategies",handCalculatedSinglePrinter},
    {"simultaneous arrivals and idle slots",simultaneousArrivalsAndIdleTime},
    {"completion/arrival tie",completionArrivalTie},{"retry and outcome cohorts",retryAndOutcomeCohorts},
    {"window clipping and empty samples",windowsAndZeroSamples},{"capacity and window validation",capacityAndInvalidWindows},
    {"replay input boundaries",replayValidation},{"determinism and work conservation",determinismAndConservation}});}
