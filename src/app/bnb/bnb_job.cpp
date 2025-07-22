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

    LOG(V5_DEBG, "myRank: %i myIndex: %i\n", getJobTree().getRank(), getJobTree().getIndex());

    if(getJobTree().isRoot()) init();

    ProcessWideThreadPool::get().addTask([this]() {loop();});
}

void BnbJob::init() {
    size_t problem_size = getDescription().getFormulaPayloadSize(0);
    int const *problem = getDescription().getFormulaPayload(0);

    //divide into categories
    _nr_tasks = problem[0];
    _nr_cores = problem[1];
    std::vector<int> tasks;
    for(int i = 0; i < _nr_tasks; ++i) {
        tasks.push_back(problem[i+2]);
    }

    //initial work
    auto lock = queue_mtx.getLock();
    std::vector<std::vector<int>> cores(_nr_cores, std::vector<int>(1, 0));
    Work work = {0, tasks, cores};
    _work_queue.push(work);
    _best_length = -1;

    log("Beginning", work);
}

void BnbJob::loop() {
    while(!_work_queue.empty()) {
        auto lock = queue_mtx.getLock();
        Work curr_work = _work_queue.front();
        _work_queue.pop();

        Work solution = branch(curr_work);
                
        //compare solutions
        if (solution.completed == 1) {
            //find length of solution
            int new_length = -1;
            std::vector<int> new_core_length = compute_core_length(solution.cores);
            new_length = *std::max_element(new_core_length.begin(), new_core_length.end());
        
            if (_best_length == -1 || new_length < _best_length) {
                auto lock = solution_mtx.getLock();
                _best_solution = solution;
                _best_length = new_length;
            }        
        }

    }
}

BnbJob::Work BnbJob::branch(Work work) {
    std::vector<int> core_length = compute_core_length(work.cores);

    //if no new tasks
    if (work.tasks.empty()) {
        work.completed = 1;
        return work;
    }

    //get current task
    std::vector<int> new_tasks = work.tasks;
    int curr_task = new_tasks[0];
    new_tasks.erase(new_tasks.begin());

    //add newest task to all cores and branch
    for (int i = 0; i < _nr_cores; i ++) {
        auto lock = queue_mtx.getLock();
        std::vector<std::vector<int>> new_cores = work.cores;

        new_cores[i].pop_back();
        new_cores[i].push_back(curr_task);
        new_cores[i].push_back(0);
              
        Work new_work = {0, new_tasks, new_cores};
        _work_queue.push(new_work);  
    }

    return work;
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

void BnbJob::log(std::string reason, Work work) {
    // turn vectors to strings
    std::string str_tasks = "";
    for(int i = 0; i < work.tasks.size(); ++i) {
        str_tasks.append(" ");
        str_tasks.append(std::to_string(work.tasks.at(i)));
    }
    std::string str_core_lengths = "";
    for(int i = 0; i < _nr_cores; i++) {
        str_core_lengths.append(" ");
        str_core_lengths.append(std::to_string(compute_core_length(work.cores).at(i)));
    }
    std::string str_cores = "";
    for(int i = 0; i < _nr_cores; ++i) {
        for( int j = 0; j < work.cores[i].size(); j++) {
            str_cores.append(" ");
            str_cores.append(std::to_string(work.cores[i][j]));
        }
    }

    LOG(V2_INFO, "%s: (Completion: %i) (Nr Tasks: %i) (Nr Cores: %i) (Tasks:%s) (Core Lengths:%s) (Cores:%s)\n",
        reason.c_str(), work.completed, _nr_tasks, _nr_cores, str_tasks.c_str(), str_core_lengths.c_str(), str_cores.c_str());
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

    if(!getJobTree().isRoot() && _work_queue.empty()) {
        JobMessage msg = getMessageTemplate();
        msg.tag = MSG_QUEUE_EMPTY;
        msg.payload = {3}; // irrelevant
        // Send
        getJobTree().sendToRoot(msg);
        LOG(V2_INFO, "Work queue is empty.\n");
    }
}

// React to an incoming message.
void BnbJob::appl_communicate(int source, int mpiTag, JobMessage& msg) {
    LOG(V2_INFO, "Message %i with Payload %i from %i received.\n", msg.tag, msg.payload[0], source);   

    if (getJobTree().isRoot()) {
        msg.payload = splitQueue();
        msg.returnToSender(source, mpiTag);
        LOG(V2_INFO, "Message returned to sender.\n");
    }

    if(!(getJobTree().isRoot())) {
        addToQueue(msg.payload);
        LOG(V2_INFO, "Message processed\n");
    }
}

std::vector<int> BnbJob::splitQueue() {
    auto lock = queue_mtx.getLock();
    std::vector<int> sendQueue;

    if (_work_queue.size() < 2) {
        sendQueue.push_back(-1);
    } else {
        int length = _work_queue.size();
        int sendLength = length / 2;
        for (int i = 0; i < sendLength; i++) {
            Work work_front = _work_queue.front();
            std::vector<int> vector_front;

            vector_front.push_back(work_front.completed);
            vector_front.push_back(-2); // -2 is inside work and -3 (see later) between works as just one delimiter is not enough
            vector_front.insert(vector_front.end(), work_front.tasks.begin(), work_front.tasks.end());
            vector_front.push_back(-2);
            for (int i = 0; i < work_front.cores.size(); i++) vector_front.insert(vector_front.end(), work_front.tasks.begin(), work_front.tasks.end());
        }
    }

    return sendQueue;
}

void BnbJob::addToQueue(std::vector<int> message) {

}

// Mark the job as done, with the provided result code and solution.
void BnbJob::insertResult(int resultCode, const std::vector<int>& solution) {
    _result.id = getId();
    _result.revision = getRevision();
    _result.result = resultCode;
    _result.setSolutionToSerialize(solution.data(), solution.size());
}

int BnbJob::appl_solved() {
    if(!_work_queue.empty()) return -1;
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
