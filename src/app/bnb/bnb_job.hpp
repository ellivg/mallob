
#ifndef DOMPASCH_MALLOB_BNB_JOB_HPP
#define DOMPASCH_MALLOB_BNB_JOB_HPP

#include "app/job.hpp"
#include "util/sys/threading.hpp"
#include "util/periodic_event.hpp"
#include "util/sys/timer.hpp"
#include "comm/job_tree_broadcast.hpp"
#include "comm/job_tree_all_reduction.hpp"

#include <queue>

/*
Minimally compiling example "application" for a Mallob job. 
Edit and extend for your application. 
*/
class BnbJob : public Job {

private:
    JobResult _result;
    std::future<void> future;

    size_t _nr_tasks;
    int _nr_machines;
    
    struct Work{
        bool completed;
        std::vector<int> tasks;
        std::vector<std::vector<int>> machines;
        std::array<int, 2> last_assigned; //The length of the job which was assigned last and where it was assigned to
    };

    struct Tracker{
        float activation_time {-1};
        float time_since_activation {0};
        float work_start_time {-1};
        float time_spent_working {0};
        float perc_working {-1};

        float not_work_start_time_empty {-1};
        float time_spent_not_working_empty {0};
        bool not_work_wait {0};
        float not_work_start_time_wait {-1};
        float time_spent_not_working_wait {0};
        float not_work_start_time_work {-1};
        float time_spent_not_working_work {0};
        float not_work_start_time_after {-1};
        float time_spent_not_working_after {0};
        float messages_start_time {-1};
        float time_spent_messages {0};
        float waiting_start_time {-1};
        float time_spent_waiting {0};

        int num_queries {0};
        int num_succ_queries {0};
        int num_nonsucc_nomsg {0};
        int num_nonsucc_rankinvld {0};
        int num_nonsucc_empty {0};
    };

    Tracker tracker;

    long appr_amount_of_expl; //approximative amount of explorations (2^nr tasks)
    int num_expl_nodes;

    std::queue<Work> _work_queue;

    AdjustablePermutation _perm; // Represents a pseudo-random permutation of a set of integers [0..n).

    Work _best_solution;
    struct Bounds{
        int curr_lower_bound {-1};
        int curr_upper_bound {-1};
        int curr_best_solution {-1};
    };
    Bounds bounds;

    Mutex solution_mtx;
    Mutex queue_mtx;
    ConditionVariable _loop_cond_var;

    bool _working {false};
    bool _waiting {false};

    // Types of ending the program
    bool _stopSearch {false};
    bool _reportableSolution {false};

    bool _send_messages {false}; // Whether we can send messages
    bool _sent_work {false}; // Whether we have sent work to another thread
    
    bool _first {true}; // TODO

    // A JobTreeBroadcast instance represents one single, plain broadcast along the job tree.
    std::unique_ptr<JobTreeBroadcast> _bcast;
    // A JobTreeAllReduction instance represents one single all-reduction along the job tree.
    std::unique_ptr<JobTreeAllReduction> _red;

    // Internal message tags which must be different for each pair of potentially concurrent
    // or directly adjacent collective operations.
    static const int BCAST_INIT {1};
    static const int ALLRED {2};

    PeriodicEvent<100> _periodic_reduction;

    static const int MSG_ROUNDTRIP = 1; // internal message tag for our round-trip messages
    static const int MSG_TEST = 2;
    static const int MSG_WORK_STEALING_QUERY = 31;
    static const int MSG_WORK_STEALING_ANSWER = 32;
    static const int MSG_WORK_STEALING_DONE = 33;
    static const int MSG_FINISHED = 4;

    static const int NUM_WORKERS = 4; // # workers we request and require   
    
    void init();
    void loop();

    void branch(Work& work);
    void pruning_three_jobs_left(Work& work, std::vector<int>& machine_workload);

    std::vector<int> splitQueue();
    void addToQueue(std::vector<int>& message);

    std::vector<int> machine_workloads(const std::vector<std::vector<int>>& machines);
    std::string transform_for_log(const std::string& reason, const Work& work);
    void printTracking();

    void tryStartReduction();
    void tryEndReduction();

    void insertResult(int resultCode, const std::vector<int>& solution);

public:
    BnbJob(const Parameters& params, const JobSetup& setup, AppMessageTable& table);
    void appl_start() override;
    void appl_suspend() override {}
    void appl_resume() override {}
    void appl_terminate() override;
    int appl_solved();
    JobResult&& appl_getResult() override;
    void appl_communicate();
    void appl_communicate(int source, int mpiTag, JobMessage& msg);
    void appl_dumpStats() override {}

    // Return whether this worker can be cleaned up without any blocking or waiting.
    // In our case, this is the case when no background task is pending.
    // Note that, as long as this returns false, 
    bool appl_isDestructible() override {
        // you can, and need to, advance communication at this point
        // so that everything that is still going on can conclude nicely
        appl_communicate();
        if (_bcast) return false;
        if (_red) return false;
        return true; // all communication and background computation concluded
    }

    void appl_memoryPanic() override {}

    int getDemand() const override;    
};

#endif
