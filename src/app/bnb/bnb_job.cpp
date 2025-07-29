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
        _working = 0;
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
    _nr_processors = problem[1];
    std::vector<int> tasks;
    for(int i = 0; i < _nr_tasks; ++i) {
        tasks.push_back(problem[i+2]);
    }

    //initial work
    auto lock = queue_mtx.getLock();
    std::vector<std::vector<int>> processors(_nr_processors, std::vector<int>(1, 0));
    Work work = {0, tasks, processors};
    _work_queue.push(work);
    _best_length = -1;
    _working = 1;

    log("Beginning", work);
}

void BnbJob::loop() {
    
    while(_working && Timer::elapsedSeconds() <= 5) {

        bool empty;
        {
            auto lock = queue_mtx.getLock();
            empty = _work_queue.empty();
        }

        if(empty) {
            usleep(1000); // 1 milliseconds
            continue;
        }

        Work curr_work;
        {
            auto lock = queue_mtx.getLock();
            curr_work = _work_queue.front();
            _work_queue.pop();
        }
        
        branch(curr_work);
         
        //compare solutions
        if (curr_work.completed == 1) {
            //find length of solution
            int new_length = -1;
            std::vector<int> new_processor_length = compute_processor_length(curr_work.processors);
            new_length = *std::max_element(new_processor_length.begin(), new_processor_length.end());
        
            if (_best_length == -1 || new_length < _best_length) {
                auto lock = solution_mtx.getLock();
                _best_solution = curr_work;
                _best_length = new_length;
            }        
        }
    }
}

BnbJob::Work BnbJob::branch(Work& work) {
    std::vector<int> processor_length = compute_processor_length(work.processors);
    
    //if no new tasks
    if (work.tasks.empty()) {
        work.completed = 1;
        return work;
    }
    
    //get current task
    std::vector<int> new_tasks = work.tasks;
    int curr_task = new_tasks[0];
    new_tasks.erase(new_tasks.begin());
    
    //add newest task to all processors and branch
    for (int i = 0; i < _nr_processors; i ++) {
        auto lock = queue_mtx.getLock();
        std::vector<std::vector<int>> new_processors = work.processors;
        
        new_processors[i].pop_back();
        new_processors[i].push_back(curr_task);
        new_processors[i].push_back(0);
              
        Work new_work = {0, new_tasks, new_processors};
        _work_queue.push(new_work);  
    }
    
    return work;
}

std::vector<int> BnbJob::compute_processor_length(const std::vector<std::vector<int>>& processors) {
    std::vector<int> processor_length;
    for (int i = 0; i < _nr_processors; i++) {
        int curr_length = 0;
        int j = 0;
        while(processors[i][j] != 0) {
            curr_length += processors[i][j];
            j++;
        }
        processor_length.push_back(curr_length);
    }
    return processor_length;
}

void BnbJob::log(const std::string& reason, const Work& work) {
    // turn vectors to strings
    std::string str_tasks = "";
    for(int i = 0; i < work.tasks.size(); ++i) {
        str_tasks.append(" ");
        str_tasks.append(std::to_string(work.tasks.at(i)));
    }
    std::string str_processor_lengths = "";
    for(int i = 0; i < _nr_processors; i++) {
        str_processor_lengths.append(" ");
        str_processor_lengths.append(std::to_string(compute_processor_length(work.processors).at(i)));
    }
    std::string str_processors = "";
    for(int i = 0; i < _nr_processors; ++i) {
        for( int j = 0; j < work.processors[i].size(); j++) {
            str_processors.append(" ");
            str_processors.append(std::to_string(work.processors[i][j]));
        }
    }

    LOG(V2_INFO, "%s: (Completion: %i) (Nr Tasks: %i) (Nr Processors: %i) (Tasks:%s) (Processor Lengths:%s) (Processors:%s)\n",
        reason.c_str(), work.completed, _nr_tasks, _nr_processors, str_tasks.c_str(), str_processor_lengths.c_str(), str_processors.c_str());
}

int BnbJob::getDemand() const {
    // return Job::getDemand();
    return NUM_WORKERS; // we strictly want this number of workers
}

// Called periodically by the main thread to allow the worker to emit messages.
void BnbJob::appl_communicate() {
    // Not enough workers available?
    if (getJobTree().isRoot() && !_send_messages && getVolume() < NUM_WORKERS) {
        if (getAgeSinceActivation() < 1) return; // wait for up to 1s after appl_start

        LOG(V2_INFO, "[dummy] Unable to get %i workers within 1 second - giving up\n", NUM_WORKERS);
        // Report an "unknown" result (code 0)
        insertResult(0, {-1});
        _send_messages= true;
        return;
    }

    if (!_send_messages && getVolume() == NUM_WORKERS
            && getJobComm().getWorldRankOrMinusOne(NUM_WORKERS-1) >= 0) {

        // figure out new text here
        _send_messages = true;
        LOG(V2_INFO, "HERE\n");
    }

    //subject to change
    bool empty;
    {
        auto lock = queue_mtx.getLock();
        empty = _work_queue.empty();
    }

    if(empty) {
        if (!_send_messages) {
            LOG(V2_INFO, "Not ready yet: %i\n", _send_messages);
            usleep(1000*100);
            return;
        }
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
            for (int i = 0; i < work_front.processors.size(); i++) vector_front.insert(vector_front.end(), work_front.tasks.begin(), work_front.tasks.end());
        }
    }

    return sendQueue;
}

void BnbJob::addToQueue(std::vector<int>& message) {

    _working = 1;
}

// Mark the job as done, with the provided result code and solution.
void BnbJob::insertResult(int resultCode, const std::vector<int>& solution) {
    _result.id = getId();
    _result.revision = getRevision();
    _result.result = resultCode;
    _result.setSolutionToSerialize(solution.data(), solution.size());
}

int BnbJob::appl_solved() {
    bool empty;
    {
        auto lock = queue_mtx.getLock();
        empty = _work_queue.empty();
    }

    if(!empty) return -1;
    if(getJobTree().isRoot()) {
        auto lock = solution_mtx.getLock();
        std::vector<int> _internal_solution;
        for(int i = 0; i < _best_solution.processors.size(); i++) {
            for(int j = 0; j < _best_solution.processors.at(i).size(); j++) {
                _internal_solution.push_back(_best_solution.processors[i][j]);
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
