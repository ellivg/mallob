
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
    Task _best_solution;
    int _best_length;

    static const int MSG_ROUNDTRIP = 1; // internal message tag for our round-trip messages
    static const int NUM_WORKERS = 2; // # workers we request and require

    // Represents a pseudo-random permutation of a set of integers [0..n).
    AdjustablePermutation _perm;

    // Whether we already started our roundtrip.
    bool _started_roundtrip {false};

    void insertResult(int resultCode, const std::vector<int>& solution);
    void advancePingPongMessage(JobMessage& msg);

public:
    BnbJob(const Parameters& params, const JobSetup& setup, AppMessageTable& table);
    void appl_start() override;
    void appl_suspend() override {}
    void appl_resume() override {}
    void appl_terminate() override {}
    int appl_solved();
    JobResult&& appl_getResult() override;
    void appl_communicate();
    void appl_communicate(int source, int mpiTag, JobMessage& msg);
    void appl_dumpStats() override {}
    bool appl_isDestructible() override {return true;}
    void appl_memoryPanic() override {}

    int getDemand() const override;

    void init();
    void loop();
    Task branch(Task task);
    std::vector<int> compute_core_length(std::vector<std::vector<int>> cores);
    void log(std::string reason, Task task);
};

#endif
