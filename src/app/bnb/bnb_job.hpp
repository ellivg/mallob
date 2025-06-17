
#ifndef DOMPASCH_MALLOB_BNB_JOB_HPP
#define DOMPASCH_MALLOB_BNB_JOB_HPP

#include "app/job.hpp"

#include <queue>

/*
Minimally compiling example "application" for a Mallob job. 
Edit and extend for your application. 
*/
class BnbJob : public Job {

private:
    JobResult _result;
    size_t _nr_processes;
    int _nr_cores;
    
    struct Task{
        bool completed;
        std::vector<int> processes;
        std::vector<std::vector<int>> cores;
    };

    std::queue<Task> _task_queue;

public:
    BnbJob(const Parameters& params, const JobSetup& setup, AppMessageTable& table);
    void appl_start() override;
    void appl_suspend() override {}
    void appl_resume() override {}
    void appl_terminate() override {}
    int appl_solved() override {return -1;}
    JobResult&& appl_getResult() override {return std::move(_result);}
    void appl_communicate() override {}
    void appl_communicate(int source, int mpiTag, JobMessage& msg) override {}
    void appl_dumpStats() override {}
    bool appl_isDestructible() override {return true;}
    void appl_memoryPanic() override {}

    Task compute(Task task);
    std::vector<int> compute_core_length(std::vector<std::vector<int>> cores);
    void log(std::string reason, Task task);
};

#endif
