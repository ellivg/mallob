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
        
        _result.result = -1; // no result present at initilization
}

void BnbJob::appl_start() {
    // Initialize pseudo-random permutation with the number of workers -
    // use the 1st integer in the job's payload as a random seed
    _perm = AdjustablePermutation(NUM_WORKERS, getDescription().getFormulaPayload(0)[0]);

    LOG(V5_DEBG, "myRank: %i myIndex: %i\n", getJobTree().getRank(), getJobTree().getIndex());

    init();
    ProcessWideThreadPool::get().addTask([this]() {loop();});
}

int BnbJob::appl_solved() {
    bool empty;
    {
        auto lock = queue_mtx.getLock();
        empty = _work_queue.empty();
    }

    if(!_finished) return -1;

    if(!empty || _working) return -1;
    {
        auto lock = solution_mtx.getLock();
        std::vector<int> _internal_solution;
        for(int i = 0; i < _best_solution.processors.size(); i++) {
            for(int j = 0; j < _best_solution.processors.at(i).size(); j++) {
                _internal_solution.push_back(_best_solution.processors[i][j]);
            }
        }
        _result.result = 0;
        _result.setSolution(std::move(_internal_solution));

        LOG(V2_INFO, "%s", transform_for_log("End", _best_solution).c_str());
    }
    return _result.result;
}

JobResult&& BnbJob::appl_getResult() {
    return std::move(_result);
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
            usleep(1000*100); //add cond_var here too?
            return;
        }
        if (_waiting) {
            LOG(V2_INFO, "Waiting\n");
            usleep(1000*100); //add cond_var here too?
            return;
        }

        JobMessage msg = getMessageTemplate();
        msg.tag = MSG_WORK_STEALING_QUERY;
        msg.payload = {0}; // irrelevant

        // Send
        int randomIndex = rand() % NUM_WORKERS;
        // Use our JobComm to convert the tree index into an addressable MPI rank.
        int recvRank = getJobComm().getWorldRankOrMinusOne(randomIndex);
        
        if (recvRank == -1 || getJobTree().getRank() == randomIndex) {
            LOG(V2_INFO, "AHHHHHHH\n");
        } else {
            msg.treeIndexOfDestination = randomIndex;
            msg.contextIdOfDestination = getJobComm().getContextIdOrZero(randomIndex);
            assert(msg.contextIdOfDestination != 0);
            // Send
            getJobTree().send(recvRank, MSG_SEND_APPLICATION_MESSAGE, msg);
            LOG(V2_INFO, "[msg] Work queue is empty. Message sent to %i.\n", recvRank);
            _waiting = 1;
        }
    }

    if(getJobTree().isRoot() && _send_messages) {
        // Use our JobComm to convert the tree index into an addressable MPI rank.
        int recvRank = getJobComm().getWorldRankOrMinusOne(1);

        if (recvRank == -1) {
            LOG(V2_INFO, "AHHHHHHH\n");
        } else {
            // Found a valid rank!
            JobMessage msg = getMessageTemplate();
            msg.payload = {787};
            msg.treeIndexOfDestination = 1;
            msg.contextIdOfDestination = getJobComm().getContextIdOrZero(1);
            assert(msg.contextIdOfDestination != 0);
            // Send
            //getJobTree().send(recvRank, MSG_SEND_APPLICATION_MESSAGE, msg);
            //LOG(V2_INFO, "Message returned to sender %i.\n", recvRank);
    }
}
}

// React to an incoming message.
void BnbJob::appl_communicate(int source, int mpiTag, JobMessage& msg) {
    LOG(V2_INFO, "[msg] Message %i with Payload %i from %i received.\n", msg.tag, msg.payload[0], source);  
    usleep(1000*100); // for easy reading purposes 

    if (msg.tag == MSG_WORK_STEALING_QUERY) {
        LOG(V2_INFO, "Processing\n");
        // Use our JobComm to convert the tree index into an addressable MPI rank.
        int recvRank = getJobComm().getWorldRankOrMinusOne(source);

        if (recvRank == -1) {
            LOG(V2_INFO, "[msg] Message couldn't be send. Try again\n");
        } else {
            // Found a valid rank!
            msg.payload = splitQueue();
            msg.tag = MSG_WORK_STEALING_ANSWER;
            msg.treeIndexOfDestination = source;
            msg.contextIdOfDestination = getJobComm().getContextIdOrZero(source);
            assert(msg.contextIdOfDestination != 0);
            // Send
            getJobTree().send(recvRank, MSG_SEND_APPLICATION_MESSAGE, msg);
        }
        LOG(V2_INFO, "[msg] Message returned to sender %i.\n", recvRank);
        return;
    }

    if(msg.tag == MSG_WORK_STEALING_ANSWER) {
        if(msg.payload[0] == -1) {
           // _finished = 1;
            _waiting = 0;
        } else {
            LOG(V2_INFO, "[msg] Filling work queue.\n");
            addToQueue(msg.payload);
            LOG(V2_INFO, "[msg] Work queue is filled.\n", source, msg.tag, msg.payload[0]);
        } 
    }
}

int BnbJob::getDemand() const {
    // return Job::getDemand();
    return NUM_WORKERS; // we strictly want this number of workers
}

//PRIVATE METHODS

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

    if(!getJobTree().isRoot()) return;

    //initial work
    auto lock = queue_mtx.getLock();
    std::vector<std::vector<int>> processors(_nr_processors, std::vector<int>(1, 0));
    Work work = {0, tasks, processors};
    _work_queue.push(work);
    _best_length = -1;
    _working = 1;

    LOG(V2_INFO, "%s", transform_for_log("Beginning", work).c_str());
}

void BnbJob::loop() {
//loop doesn't start if _working = 0 from the beginning
    if (!_working) {
        bool empty;
        {
            auto lock = queue_mtx.getLock();
            empty = _work_queue.empty();

            if(empty) {
                LOG(V2_INFO, "[queue] Queue empty. Waiting\n");
                _working = 0;
                _loop_cond_var.waitWithLockedMutex(lock, [&]() {return _working;});
                LOG(V2_INFO, "Restarting loop\n");
            }
        }
    }

    
    while(_working) {

        bool empty;
        {
            auto lock = queue_mtx.getLock();
            empty = _work_queue.empty();

            if(empty) {
                LOG(V2_INFO, "Stopping Loop\n");
                _working = 0;
                _loop_cond_var.waitWithLockedMutex(lock, [&]() {return _working;});
                LOG(V2_INFO, "Restarting loop\n");
            }
        }

        Work curr_work;
        {
            auto lock = queue_mtx.getLock();
            curr_work = _work_queue.front();
            _work_queue.pop();
        }
        usleep(1000*100);
        LOG(V2_INFO, "In loop. Jobs left: %i\n", _work_queue.size());
        LOG(V5_DEBG, "%s", transform_for_log("In Loop. Currently at:", curr_work));
        
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

std::vector<int> BnbJob::splitQueue() {
    auto lock = queue_mtx.getLock();
    std::vector<int> sendQueue;

    if (_work_queue.size() < 2) {
        sendQueue.push_back(-1);
    } else {
        int length = _work_queue.size();
        int sendLength = length / 2;
        LOG(V5_DEBG, "[msg] Work queue is: %i\n", _work_queue.size());
        if (sendLength > 100) sendLength = 100; //this seems to be a bottleneck, so maybe change way for sending entirely
        for (int i = 0; i < sendLength; i++) {
            Work work_front = _work_queue.front();
            _work_queue.pop();
            std::vector<int> vector_front;

            vector_front.push_back(work_front.completed);
            vector_front.push_back(-2); // -2 is inside work and -3 (see later) between works as just one delimiter is not enough
            vector_front.insert(vector_front.end(), work_front.tasks.begin(), work_front.tasks.end());
            vector_front.push_back(-2);
            for (int i = 0; i < work_front.processors.size(); i++) {
                vector_front.insert(vector_front.end(), work_front.processors.at(i).begin(), work_front.processors.at(i).end());
                vector_front.push_back(-2);
            }
            vector_front.push_back(-3);
            
            sendQueue.insert(sendQueue.end(), vector_front.begin(), vector_front.end());
        }
    }

    return sendQueue;
}

void BnbJob::addToQueue(std::vector<int>& message) {
    
    auto lock = queue_mtx.getLock();
    int next;

    //add one work at a time
    //it has to look like this:
    // (0/1) (-2) (tasks: (0/...)_nr_tasks) (-2) (processors: (0/...)_nr_processors) (-3)
    //clean up??
    while (!message.empty()) {
        Work work;

        //completed
        next = message.front();
        message.erase(message.begin());
        assert(next == 0 || next == 1);
        work.completed = next;

        //-2
        next = message.front();
        message.erase(message.begin());
        assert(next == -2);

        //tasks (is there a function to not do this in a loop?)
        next = message.front();
        message.erase(message.begin());
        while (next != -2) {
            assert(next > 0);
            work.tasks.push_back(next);

            next = message.front();
            message.erase(message.begin());
        }

        //-2
        assert(next == -2);

        //processors
        std::vector<std::vector<int>> processors(_nr_processors, std::vector<int>(1, 0));
        for (int i = 0; i < _nr_processors; i++) {
            next = message.front();
            message.erase(message.begin());
            processors.at(i).pop_back(); // delete initial 0
            while(next != -2) {
                processors.at(i).push_back(next);    

                next = message.front();
                message.erase(message.begin()); 
            }

            //-2
            assert(next == -2);
        }
        work.processors = processors;

        //-3
        next = message.front();
        message.erase(message.begin());
        assert(next == -3);

        _work_queue.push(work);
    }

    _working = 1;
    _waiting = 0;
    _loop_cond_var.notify();
}

// Mark the job as done, with the provided result code and solution.
void BnbJob::insertResult(int resultCode, const std::vector<int>& solution) {
    _result.id = getId();
    _result.revision = getRevision();
    _result.result = resultCode;
    _result.setSolutionToSerialize(solution.data(), solution.size());
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

std::string BnbJob::transform_for_log(const std::string& reason, const Work& work) {
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

    //assemble and is there a better way??
    std::string log_string = reason.c_str();
    log_string.append(": (Completion: ");
    log_string.append(std::to_string(work.completed));
    log_string.append(") (Nr Tasks: ");
    log_string.append(std::to_string(_nr_tasks));
    log_string.append(") (Nr Processors: ");
    log_string.append(std::to_string(_nr_processors));
    log_string.append(") (Tasks:");
    log_string.append(str_tasks.c_str());
    log_string.append(") (Processor Lengths:");
    log_string.append(str_processor_lengths.c_str());
    log_string.append(") (Processors:");
    log_string.append(str_processors.c_str());
    log_string.append(")\n");

    return log_string;
}
