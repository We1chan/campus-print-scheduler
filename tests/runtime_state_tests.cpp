#include "cps/core/scheduler.hpp"
#include "cps/runtime/state_store.hpp"

#include <QFile>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

cps::Job makeJob(const std::string& id, cps::TimeMs submitted) {
    cps::Job job;
    job.id = id;
    job.title = "persistent demo";
    job.pages = 4;
    job.copies = 2;
    job.submittedMs = submitted;
    job.priority = 7;
    job.maxAttempts = 3;
    return job;
}
}

int main() {
    try {
        QTemporaryDir directory;
        require(directory.isValid(), "temporary directory");
        const QString path = directory.filePath("state.json");

        cps::Scheduler scheduler({30000}, [] { return cps::TimeMs{5000}; });
        require(scheduler.submit(makeJob("job-1", 1000)), "submit");
        const auto first = scheduler.claim(cps::Policy::PriorityAging, 2000);
        require(first && first->attempts == 1, "first claim");
        require(scheduler.retry(first->id, first->attempts, 2500), "retry");
        const auto second = scheduler.claim(cps::Policy::PriorityAging, 3000);
        require(second && second->attempts == 2, "second claim");
        require(scheduler.finish(second->id, second->attempts, true, 3500), "finish");

        cps::PersistentState state;
        state.schedulerOptions.agingIntervalMs = 30000;
        state.policy = cps::Policy::PriorityAging;
        state.printers = 2;
        state.savedAtMs = 4000;
        state.jobs = scheduler.snapshot();

        QString error;
        require(cps::writePersistentState(path, state, error), "write state");

        cps::PersistentState loaded;
        require(cps::readPersistentState(path, loaded, error), "read state");
        require(loaded.policy == cps::Policy::PriorityAging, "policy round trip");
        require(loaded.printers == 2, "printer count round trip");
        require(loaded.jobs.size() == 1 && loaded.jobs[0].attempts == 2, "job round trip");
        require(loaded.jobs[0].state == cps::JobState::Completed, "terminal state round trip");
        require(loaded.jobs[0].runs.size() == 2, "run history round trip");

        cps::Scheduler restored(loaded.schedulerOptions, [] { return cps::TimeMs{4000}; });
        require(restored.restore(loaded.jobs, loaded.savedAtMs), "scheduler restore");
        require(restored.snapshot()[0].finishedMs == cps::TimeMs{3500}, "finish time round trip");

        QFile corrupt(path);
        require(corrupt.open(QIODevice::WriteOnly | QIODevice::Truncate), "open corrupt state");
        require(corrupt.write("{\"schema_version\":2}") > 0, "write corrupt state");
        corrupt.close();
        require(!cps::readPersistentState(path, loaded, error), "reject incompatible state");

        require(corrupt.open(QIODevice::WriteOnly | QIODevice::Truncate), "reopen corrupt state");
        require(corrupt.write("{not-json") > 0, "write malformed state");
        corrupt.close();
        require(!cps::readPersistentState(path, loaded, error), "reject malformed state");

        std::cout << "Persistent scheduler state round-trip and validation passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
