
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
    size_t _nr_tasks;
    int _nr_processors;
    
    struct Work{
        bool completed;
        std::vector<int> tasks;
        std::vector<std::vector<int>> processors;
    };

    std::queue<Work> _work_queue;
    Work _best_solution;
    int _best_length;
    Mutex solution_mtx;
    Mutex queue_mtx;

    static const int MSG_ROUNDTRIP = 1; // internal message tag for our round-trip messages
    static const int MSG_TEST = 2;
    static const int MSG_QUEUE_EMPTY = 3;
    static const int NUM_WORKERS = 2; // # workers we request and require

    // Represents a pseudo-random permutation of a set of integers [0..n).
    AdjustablePermutation _perm;

    // Whether we already started our roundtrip.
    bool _started_roundtrip {false};

    void insertResult(int resultCode, const std::vector<int>& solution);
    std::vector<int> splitQueue();
    void addToQueue(std::vector<int> message);

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
    Work branch(Work work);
    std::vector<int> compute_processor_length(std::vector<std::vector<int>> processors);
    void log(std::string reason, Work work);
};

#endif
