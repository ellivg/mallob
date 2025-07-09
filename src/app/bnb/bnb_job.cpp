#include "bnb_job.hpp"

#include <iostream>
#include <string>

#include "app/job.hpp"
#include "app/job_tree.hpp"
#include "comm/msgtags.h"
#include "data/job_transfer.hpp"
#include "util/logger.hpp"
#include "util/permutation.hpp"
#include "util/sys/thread_pool.hpp"

BnbJob::BnbJob(const Parameters& params, const JobSetup& setup, AppMessageTable& table)
    : Job(params, setup, table) {

        // Sending job messages along the job tree (i.e., to a direct parent or child)
        // is very easy via the convenience methods like getJobTree().sendToParent(msg) etc.
        // In our case, since we want to send messages to arbitrary workers in our tree,
        // we need a JobComm instance to be constructed for us in the background.
        assert(_params.jobCommUpdatePeriod() > 0 || log_return_false("[ERROR] For this application to work,"
            " you must explicitly enable job communicators with the -jcup option, e.g., -jcup=0.1\n"));
        // no result present
        _result.result = -1;
}

void BnbJob::appl_start() {
    // Initialize pseudo-random permutation with the number of workers -
    // use the 1st integer in the job's payload as a random seed
    _perm = AdjustablePermutation(NUM_WORKERS, getDescription().getFormulaPayload(0)[0]);

    LOG(V2_INFO, "myRank: %i myIndex: %i\n", getJobTree().getRank(), getJobTree().getIndex());


    //this only needs to get done once in the root
    if(getJobTree().isRoot()) init();

    ProcessWideThreadPool::get().addTask([this]() {loop();});
    
    //insert JobResult here
}

void BnbJob::init() {
    LOG(V2_INFO, "This is root.\n");

    //get problem
    size_t problem_size = getDescription().getFormulaPayloadSize(0);
    int const *problem = getDescription().getFormulaPayload(0);

    //divide into categories
    _nr_processes = problem[0];
    _nr_cores = problem[1];
    std::vector<int> processes;
    for(int i = 0; i < _nr_processes; ++i) {
        processes.push_back(problem[i+2]);
    }

    //initial task
    std::vector<std::vector<int>> cores(_nr_cores, std::vector<int>(1, 0));
    Task task = {0, processes, cores};
    _task_queue.push(task);

    //print beginning
    log("Beginning", task);

    //insert solver here
    _best_length = -1;
}

void BnbJob::loop() {

    LOG(V2_INFO, "myRank: %i myIndex: %i\n", getJobTree().getRank(), getJobTree().getIndex());

    if(!(getJobTree().isRoot())) {
        LOG(V2_INFO, "I am also here.\n");
    }

    while(!_task_queue.empty()) {
        Task curr_task = _task_queue.front();
        _task_queue.pop();

        Task solution = branch(curr_task);

                
        //compare solutions
        if (solution.completed == 1) {
            //find length of solution
            int new_length = -1;
            std::vector<int> new_core_length = compute_core_length(solution.cores);
            new_length = *std::max_element(new_core_length.begin(), new_core_length.end());
        
            if (_best_length == -1 || new_length < _best_length) {
                //LOG(V2_INFO, "%i\n", getJobTree().getIndex());
                //log("NEWWWWW", curr_task);
                _best_solution = solution;
                _best_length = new_length;
            }        
        }

    }
}

BnbJob::Task BnbJob::branch(Task task) {

    std::vector<int> core_length = compute_core_length(task.cores);

    //if no new processes
    if (task.processes.empty()) {
        task.completed = 1;
        return task;
    }

    //get current process
    std::vector<int> new_processes = task.processes;
    int curr_process = new_processes[0];
    new_processes.erase(new_processes.begin());

    //add newest process to all cores and branch
    for (int i = 0; i < _nr_cores; i ++) {
        std::vector<std::vector<int>> new_cores = task.cores;

        new_cores[i].pop_back();
        new_cores[i].push_back(curr_process);
        new_cores[i].push_back(0);
              
        Task new_task = {0, new_processes, new_cores};
        _task_queue.push(new_task);  
    }

    return task;
}

std::vector<int> BnbJob::compute_core_length(std::vector<std::vector<int>> cores) {
    std::vector<int> core_length;
    for (int i = 0; i < _nr_cores; i++) {
        int curr_length = 0;
        int j = 0;
        while(cores[i][j] != 0) {
            curr_length += cores[i][j];
            j++;
        }
        core_length.push_back(curr_length);
    }
    return core_length;
}

void BnbJob::log(std::string reason, Task task) {
    // turn vectors to strings
    std::string str_processes = "";
    for(int i = 0; i < task.processes.size(); ++i) {
        str_processes.append(" ");
        str_processes.append(std::to_string(task.processes.at(i)));
    }
    std::string str_core_lengths = "";
    for(int i = 0; i < _nr_cores; i++) {
        str_core_lengths.append(" ");
        str_core_lengths.append(std::to_string(compute_core_length(task.cores).at(i)));
    }
    std::string str_cores = "";
    for(int i = 0; i < _nr_cores; ++i) {
        for( int j = 0; j < task.cores[i].size(); j++) {
            str_cores.append(" ");
            str_cores.append(std::to_string(task.cores[i][j]));
        }
    }

    LOG(V2_INFO, "%s: (Completion: %i) (Nr Processes: %i) (Nr Cores: %i) (Processes:%s) (Core Lengths:%s) (Cores:%s)\n",
        reason.c_str(), task.completed, _nr_processes, _nr_cores, str_processes.c_str(), str_core_lengths.c_str(), str_cores.c_str());
}

int BnbJob::getDemand() const {
    // return Job::getDemand();
    return NUM_WORKERS; // we strictly want this number of workers
}

// Called periodically by the main thread to allow the worker to emit messages.
void BnbJob::appl_communicate() {

    // Not enough workers available?
    if (getJobTree().isRoot() && !_started_roundtrip && getVolume() < NUM_WORKERS) {
        if (getAgeSinceActivation() < 1) return; // wait for up to 1s after appl_start

        LOG(V2_INFO, "[dummy] Unable to get %i workers within 1 second - giving up\n", NUM_WORKERS);
        // Report an "unknown" result (code 0)
        insertResult(0, {-1});
        _started_roundtrip = true;
        return;
    }

    // Workers available and valid job communicator present?
    if (getJobTree().isRoot() && !_started_roundtrip && getVolume() == NUM_WORKERS
            && getJobComm().getWorldRankOrMinusOne(NUM_WORKERS-1) >= 0) {

        // craft a message to ping-pong around
        _started_roundtrip = true;
        JobMessage msg = getMessageTemplate();
        msg.tag = MSG_ROUNDTRIP;
        msg.payload = {0}; // indicates the number of bounces so far
        LOG(V2_INFO, "[dummy] starting round trip\n");
        advancePingPongMessage(msg);
    }
}

// React to an incoming message. (This becomes relevant only if you send custom messages)
void BnbJob::appl_communicate(int source, int mpiTag, JobMessage& msg) {
    int depth = msg.payload[0];
    LOG(V2_INFO, "[dummy] received round trip msg: %i bounces\n", depth);
    if (depth == NUM_WORKERS) {
        LOG(V2_INFO, "[dummy] round trip finished!\n");
        insertResult(10, std::vector<int>(msg.payload.begin()+1, msg.payload.end()));
    } else {
        advancePingPongMessage(msg);
    }
}

// Mark the job as done, with the provided result code and solution.
void BnbJob::insertResult(int resultCode, const std::vector<int>& solution) {
    _result.id = getId();
    _result.revision = getRevision();
    _result.result = resultCode;
    _result.setSolutionToSerialize(solution.data(), solution.size());
}

// Process an incoming (or internally crafted) round-trip message and,
// if possible, advance it by one more bounce.
void BnbJob::advancePingPongMessage(JobMessage& msg) {

    // msg.payload[0] is the number of bounces the message already did
    int permutedIndex = _perm.get(msg.payload[0]);
    // Use our JobComm to convert the tree index into an addressable MPI rank.
    int recvRank = getJobComm().getWorldRankOrMinusOne(permutedIndex);
    LOG(V2_INFO, "[dummy] next job tree index of round trip: %i, rank: %i\n", permutedIndex, recvRank);

    if (recvRank == -1) {
        // The job communicator has no valid rank for this job tree index!
        // Reset the round-trip by sending a fresh message to the root.
        msg.payload = {0};
        getJobTree().sendToRoot(msg);
    } else {
        // Found a valid rank!
        // Add this process to the history, but not if it's the very first one (the root)
        if (msg.payload[0] > 0) msg.payload.push_back(getJobTree().getRank());
        msg.payload[0]++; // add one bounce
        // We need to add these addressing values to the message explicitly because we're
        // messaging an arbitrary worker in the tree. For direct relatives, it is easier
        // to use the according convenience methods like getJobTree().sendToParent(msg).
        msg.treeIndexOfDestination = permutedIndex;
        msg.contextIdOfDestination = getJobComm().getContextIdOrZero(permutedIndex);
        assert(msg.contextIdOfDestination != 0);
        // Send
        getJobTree().send(recvRank, MSG_SEND_APPLICATION_MESSAGE, msg);
    }
}

int BnbJob::appl_solved() {
    if(!_task_queue.empty()) return -1;
    if(getJobTree().isRoot()) {
        std::vector<int> _internal_solution;
        for(int i = 0; i < _best_solution.cores.size(); i++) {
            for(int j = 0; j < _best_solution.cores.at(i).size(); j++) {
                _internal_solution.push_back(_best_solution.cores[i][j]);
            }
        }
        _result.result = 0;
        _result.setSolution(std::move(_internal_solution));

        log("End", _best_solution);
    }
    return _result.result;
}

JobResult&& BnbJob::appl_getResult() {
    return std::move(_result);
}
