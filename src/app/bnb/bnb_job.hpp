
#ifndef DOMPASCH_MALLOB_BNB_JOB_HPP
#define DOMPASCH_MALLOB_BNB_JOB_HPP

#include "app/job.hpp"
#include "util/sys/threading.hpp"

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

    AdjustablePermutation _perm; // Represents a pseudo-random permutation of a set of integers [0..n).

    Work _best_solution;
    int _best_length;

    Mutex solution_mtx;
    Mutex queue_mtx;
    ConditionVariable _loop_cond_var;

    bool _working {false};
    bool _waiting {false};
    bool _finished {false};
    bool _send_messages {false}; // Whether we can send messages

    static const int MSG_ROUNDTRIP = 1; // internal message tag for our round-trip messages
    static const int MSG_TEST = 2;
    static const int MSG_WORK_STEALING_QUERY = 31;
    static const int MSG_WORK_STEALING_ANSWER = 32;

    static const int NUM_WORKERS = 2; // # workers we request and require   
    
    void init();
    void loop();

    Work branch(Work& work);

    std::vector<int> splitQueue();
    void addToQueue(std::vector<int>& message);

    std::vector<int> compute_processor_length(const std::vector<std::vector<int>>& processors);
    std::string transform_for_log(const std::string& reason, const Work& work);

    void insertResult(int resultCode, const std::vector<int>& solution);

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
};

#endif
