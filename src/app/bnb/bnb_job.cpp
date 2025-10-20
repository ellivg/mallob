#include "bnb_job.hpp"

#include <iostream>
#include <string>
#include <cmath>

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

int BnbJob::appl_solved() { //TODO CHANGES HERE
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
        for(int i = 0; i < _best_solution.machines.size(); i++) {
            for(int j = 0; j < _best_solution.machines.at(i).size(); j++) {
                _internal_solution.push_back(_best_solution.machines[i][j]);
            }
        }
        _result.result = 0;
        _result.setSolution(std::move(_internal_solution));

        LOG(V2_INFO, "%s", transform_for_log("[solved] End", _best_solution).c_str());
    }

    //tracker
    tracker.time_since_activation = (Timer::elapsedSeconds()  - tracker.activation_time);
    tracker.perc_working = tracker.time_spent_working / tracker.time_since_activation;
    LOG(V2_INFO, "[tracking] Percentage of time spent working: %f\n", tracker.perc_working);
    LOG(V2_INFO, "[tracking] Number of explored nodes: %i\n", num_expl_nodes);
    LOG(V2_INFO, 
        "[tracking] Number of queries in total: %i - succesful: %i - before msgs allowed: %i - rank invalid: %i - reply empty: %i\n", 
        tracker.num_queries, tracker.num_succ_queries,  tracker.num_nonsucc_nomsg, tracker.num_nonsucc_rankinvld, tracker.num_nonsucc_empty);

    return _result.result;
}

JobResult&& BnbJob::appl_getResult() {
    return std::move(_result);
}

// Called periodically by the main thread to allow the worker to emit messages.
void BnbJob::appl_communicate() {
    // Are enough workers available?
    if (getJobTree().isRoot() && !_send_messages && getVolume() < NUM_WORKERS) {
        if (getAgeSinceActivation() < 1) return; // wait for up to 1s after appl_start

        LOG(V2_INFO, "[msg] Unable to get %i workers within 1 second - giving up\n", NUM_WORKERS);
        // Report an "unknown" result (code 0)
        insertResult(0, {-1});
        return;
    }

    //Allow messages after conditions are met
    if (!_send_messages && getVolume() == NUM_WORKERS
            && getJobComm().getWorldRankOrMinusOne(NUM_WORKERS-1) >= 0) {

        LOG(V2_INFO, "[msg] Messages allowed starting now\n");
        _send_messages = true;
    }

    //Check if new work needs to be requested and request if possible
    bool empty;
    {
        auto lock = queue_mtx.getLock();
        empty = _work_queue.empty();
    }
    if(empty) {
        tracker.num_queries++;

        if (!_send_messages) {
            LOG(V2_INFO, "[msg] Tried requesting work but messages are not allowed\n");
            tracker.num_nonsucc_nomsg++;
            usleep(1000*1000); //wait 1s (until {giving up message} is sent) until trying again
        } else if (_waiting) {
            LOG(V2_INFO, "[msg] Waiting\n");
            usleep(1000*100); //wait 0.1s to account for operations to fill queue (TODO maybe change?)
            tracker.num_queries--; //because were still waiting on the last one to be filled
        } else if (!_finished) {
            //Request work
            JobMessage msg = getMessageTemplate();
            msg.tag = MSG_WORK_STEALING_QUERY;
            msg.payload = {0}; // irrelevant

            // Check if request can be sent
            int randomIndex = rand() % NUM_WORKERS;
            // Use our JobComm to convert the tree index into an addressable MPI rank.
            int recvRank = getJobComm().getWorldRankOrMinusOne(randomIndex);
            if (recvRank == -1 || getJobTree().getRank() == randomIndex) {
                LOG(V2_INFO, "[msg] Tried requesting work but receiving rank was invalid or my own: %i\n", recvRank);
                tracker.num_nonsucc_rankinvld++;
            } else {
                //Send
                msg.treeIndexOfDestination = randomIndex;
                msg.contextIdOfDestination = getJobComm().getContextIdOrZero(randomIndex);
                assert(msg.contextIdOfDestination != 0);

                getJobTree().send(recvRank, MSG_SEND_APPLICATION_MESSAGE, msg);
                LOG(V2_INFO, "[msg] Requested work stealing from: %i\n", recvRank);
                _waiting = 1;
            }            
        }
    }

    // Periodic All-Reduction to determine if all threads are waiting and have not sent work -> program is finished
    if (_periodic_reduction.ready()) tryStartReduction();
    tryEndReduction();

}

// React to an incoming message.
void BnbJob::appl_communicate(int source, int mpiTag, JobMessage& msg) {
    LOG(V2_INFO, "[msg] Message %i with Payload %i from %i received.\n", msg.tag, msg.payload[0], source);

    if(_finished) {
        LOG(V2_INFO, "[msg] Message will not be entirely processed as program is finished\n");

        // Use our JobComm to convert the tree index into an addressable MPI rank.
        int recvRank = getJobComm().getWorldRankOrMinusOne(source);

        if (recvRank == -1) {
            LOG(V2_INFO, "[msg] Message couldn't be answered as requesting rank is invalid\n");
            tracker.num_nonsucc_rankinvld++;
        } else {
            // Returning work
            msg.tag = MSG_FINISHED;
            
            //Send
            msg.treeIndexOfDestination = source;
            msg.contextIdOfDestination = getJobComm().getContextIdOrZero(source);
            assert(msg.contextIdOfDestination != 0);
            getJobTree().send(recvRank, MSG_SEND_APPLICATION_MESSAGE, msg);
            if(msg.payload[0] != -1) _sent_work = true;
            LOG(V2_INFO, "[msg] Message returned to sender %i with tag %i\n", msg.tag);
        }
        return;
    }

    if (msg.tag == MSG_WORK_STEALING_QUERY) {
        LOG(V2_INFO, "[msg] Processing work stealing query from %i\n", source);

        // Use our JobComm to convert the tree index into an addressable MPI rank.
        int recvRank = getJobComm().getWorldRankOrMinusOne(source);

        if (recvRank == -1) {
            LOG(V2_INFO, "[msg] Work stealing query couldn't be answered as requesting rank is invalid\n");
            tracker.num_nonsucc_rankinvld++;
        } else {
            // Returning work
            msg.tag = MSG_WORK_STEALING_ANSWER;
            msg.payload = splitQueue();
            
            //Send
            msg.treeIndexOfDestination = source;
            msg.contextIdOfDestination = getJobComm().getContextIdOrZero(source);
            assert(msg.contextIdOfDestination != 0);
            getJobTree().send(recvRank, MSG_SEND_APPLICATION_MESSAGE, msg);
            if(msg.payload[0] != -1) _sent_work = true;
            LOG(V2_INFO, "[msg] Message returned to sender %i with payload[0] = %i and _sent_work = %i\n", recvRank, msg.payload[0], _sent_work);
        }
        return;
    }

    if(msg.tag == MSG_WORK_STEALING_ANSWER) {
        if(msg.payload[0] == -1) {
            _waiting = 0;
            tracker.num_nonsucc_empty++;
        } else {
            LOG(V2_INFO, "[msg] Work stealing query successful. Filling work queue.\n");
            addToQueue(msg.payload);
            LOG(V2_INFO, "[msg] Work queue is filled.\n", source, msg.tag, msg.payload[0]);
            tracker.num_succ_queries++;

        // Confirm work is received
        // Use our JobComm to convert the tree index into an addressable MPI rank.
        int recvRank = getJobComm().getWorldRankOrMinusOne(source);

        if (recvRank == -1) {
            LOG(V2_INFO, "[msg] Work stealing confirmation couldn't be answered as answering rank is invalid\n");
        } else {
            // Returning work
            msg.tag = MSG_WORK_STEALING_DONE;
            msg.payload = {0}; //irrelevant
            
            //Send
            msg.treeIndexOfDestination = source;
            msg.contextIdOfDestination = getJobComm().getContextIdOrZero(source);
            assert(msg.contextIdOfDestination != 0);
            getJobTree().send(recvRank, MSG_SEND_APPLICATION_MESSAGE, msg);
            LOG(V2_INFO, "[msg] Message returned to sender %i.\n", recvRank);
        }
        }
        return;
    }

    if(msg.tag == MSG_WORK_STEALING_DONE) {
        _sent_work = false;
        return;
    }

    if(msg.tag == MSG_FINISHED) {
        _finished = 1;
        return;
    }
}

int BnbJob::getDemand() const {
    // return Job::getDemand();
    return NUM_WORKERS; // we strictly want this number of workers
}

//PRIVATE METHODS

void BnbJob::init() {
    //read problem
    size_t problem_size = getDescription().getFormulaPayloadSize(0);
    int const *problem = getDescription().getFormulaPayload(0);

    //divide into categories
    _nr_tasks = problem[0];
    _nr_machines = problem[1];
    std::vector<int> tasks;
    for(int i = 0; i < _nr_tasks; ++i) {
        tasks.push_back(problem[i+2]);
    }

    appr_amount_of_expl = pow(2.0, _nr_tasks);

    //initial work (only done by root)
    if(getJobTree().isRoot()) {
        auto lock = queue_mtx.getLock();
        std::vector<std::vector<int>> machines(_nr_machines, std::vector<int>(1, 0));
        Work work = {0, tasks, machines, {-1, -1}};
        _work_queue.push(work);
        _working = 1;

        //initialize lower bound
        _curr_lower_bound = tasks[0];

        int average_size = 0;
        for (int i = 0; i < tasks.size(); i++) average_size += tasks[i];
        average_size /= tasks.size();
        if (_curr_lower_bound < average_size) _curr_lower_bound = average_size;

        int possible_lower_bound = tasks[_nr_machines] + tasks[_nr_machines+1];
        if(_curr_lower_bound < possible_lower_bound) _curr_lower_bound = possible_lower_bound;

        LOG(V2_INFO, "%s", transform_for_log("[start] Beginning", work).c_str());
    }

    //start tracking time
    tracker.activation_time = Timer::elapsedSeconds();
    tracker.work_start_time = tracker.activation_time;
}

void BnbJob::loop() {
    do {
        //check if queue is empty and stop working if necessary
        bool empty;
        {
            auto lock = queue_mtx.getLock();
            empty = _work_queue.empty();

            if(empty) {
                LOG(V2_INFO, "[queue] Queue empty. Stopping Loop\n");
                _working = 0;

                _loop_cond_var.waitWithLockedMutex(lock, [&]() {return _working;});
                LOG(V2_INFO, "[queue] Restarting loop\n");

            }
        }

        //else: work
        Work curr_work;
        {
            auto lock = queue_mtx.getLock();
            curr_work = _work_queue.front();
            _work_queue.pop();
        }
        usleep(1000*10); //TODO work on removing
        LOG(V5_DEBG, "%s", transform_for_log("[queue] In Loop. Currently at:", curr_work));
        if (num_expl_nodes % 1000 == 0) LOG(V2_INFO, "[queue] In loop. Jobs left: %i\n", _work_queue.size()+1);

        //tracker
        tracker.work_start_time = Timer::elapsedSeconds();

        branch(curr_work);
        num_expl_nodes++;

        //tracker
        tracker.time_spent_working += (Timer::elapsedSeconds() - tracker.work_start_time);
         
        //compare solutions
        if (curr_work.completed == 1) {
            //find length of solution
            int new_length = -1;
            std::vector<int> new_machine_workload = machine_workloads(curr_work.machines);
            new_length = *std::max_element(new_machine_workload.begin(), new_machine_workload.end());
        
            if (_curr_upper_bound == -1 || new_length < _curr_upper_bound) {
                auto lock = solution_mtx.getLock();
                _best_solution = curr_work;
                _curr_upper_bound = new_length;
            }        
        }
    } while(_working);
}

BnbJob::Work BnbJob::branch(Work& work) {
    std::vector<int> machine_workload = machine_workloads(work.machines);
    
    //if no new tasks
    if (work.tasks.empty()) {
        work.completed = 1;
        return work;
    }
    
    //get current task
    std::vector<int> new_tasks = work.tasks;
    int curr_task = new_tasks[0];
    new_tasks.erase(new_tasks.begin());
    
    //add newest task to all machines and branch
    for (int i = 0; i < _nr_machines; i ++) {
        //PRUNING
        bool prune = false;

        //if multiple machines with same length
        for (int j = 0; j < i; j++) {
            if (machine_workload[i] == machine_workload[j]) prune = true;
        }

        //prune if length of last assigned job equal length of current job
        //if ((work.last_assigned[0] == curr_task) && (work.last_assigned[1] != -1) && (work.last_assigned[1] < i)) prune = true;
        

        //END PRUNING
        if (prune) continue;

        auto lock = queue_mtx.getLock();
        std::vector<std::vector<int>> new_machines = work.machines;
        
        new_machines[i].pop_back();
        new_machines[i].push_back(curr_task);
        new_machines[i].push_back(0);
              
        Work new_work = {0, new_tasks, new_machines, {curr_task, i}};
        _work_queue.push(new_work);  
    }
    
    return work;
}

std::vector<int> BnbJob::splitQueue() {
    auto lock = queue_mtx.getLock();
    std::vector<int> sendQueue;
    
    if (_work_queue.size() < 2 || (appr_amount_of_expl - num_expl_nodes) < 100) {
        sendQueue.push_back(-1);
    } else {
        int length = _work_queue.size();
        int sendLength = length / 2;
        LOG(V5_DEBG, "[msg] Work queue is: %i\n", length);

        for (int i = 0; i < sendLength; i++) {
            Work work_front = _work_queue.front();
            _work_queue.pop();
            std::vector<int> vector_front;

            vector_front.push_back(work_front.completed);
            vector_front.push_back(-2); // -2 is inside work and -3 (see later) between works as just one delimiter is not enough
            vector_front.insert(vector_front.end(), work_front.tasks.begin(), work_front.tasks.end());
            vector_front.push_back(-2);
            for (int i = 0; i < work_front.machines.size(); i++) {
                vector_front.insert(vector_front.end(), work_front.machines.at(i).begin(), work_front.machines.at(i).end());
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
    // (0/1) (-2) (tasks: (0/...)_nr_tasks) (-2) (machines: (0/...)_nr_machines) (-3)
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

        //tasks
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

        //machines
        std::vector<std::vector<int>> machines(_nr_machines, std::vector<int>(1, 0));
        for (int i = 0; i < _nr_machines; i++) {
            next = message.front();
            message.erase(message.begin());
            machines.at(i).pop_back(); // delete initial 0
            while(next != -2) {
                machines.at(i).push_back(next);    

                next = message.front();
                message.erase(message.begin()); 
            }

            //-2
            assert(next == -2);
        }
        work.machines = machines;

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

std::vector<int> BnbJob::machine_workloads(const std::vector<std::vector<int>>& machines) {
    std::vector<int> machine_workload;
    for (int i = 0; i < _nr_machines; i++) {
        int curr_length = 0;
        int j = 0;
        while(machines[i][j] != 0) {
            curr_length += machines[i][j];
            j++;
        }
        machine_workload.push_back(curr_length);
    }
    return machine_workload;
}

std::string BnbJob::transform_for_log(const std::string& reason, const Work& work) {
    // turn vectors to strings
    std::string str_tasks = "";
    for(int i = 0; i < work.tasks.size(); ++i) {
        str_tasks.append(" ");
        str_tasks.append(std::to_string(work.tasks.at(i)));
    }
    std::string str_machine_workloads = "";
    for(int i = 0; i < _nr_machines; i++) {
        str_machine_workloads.append(" ");
        str_machine_workloads.append(std::to_string(machine_workloads(work.machines).at(i)));
    }
    std::string str_machines = "";
    for(int i = 0; i < _nr_machines; ++i) {
        for( int j = 0; j < work.machines[i].size(); j++) {
            str_machines.append(" ");
            str_machines.append(std::to_string(work.machines[i][j]));
        }
    }

    //assemble and is there a better way??
    std::string log_string = reason.c_str();
    log_string.append(": (Completion: ");
    log_string.append(std::to_string(work.completed));
    log_string.append(") (Nr Tasks: ");
    log_string.append(std::to_string(_nr_tasks));
    log_string.append(") (Nr Machines: ");
    log_string.append(std::to_string(_nr_machines));
    log_string.append(") (Tasks:");
    log_string.append(str_tasks.c_str());
    log_string.append(") (Machine Workloads:");
    log_string.append(str_machine_workloads.c_str());
    log_string.append(") (Machines:");
    log_string.append(str_machines.c_str());
    log_string.append(")\n");

    return log_string;
}

void BnbJob::tryStartReduction() {
    JobMessage baseMsg = getMessageTemplate();
    baseMsg.tag = ALLRED;
    _red.reset(new JobTreeAllReduction(getJobTree().getSnapshot(), baseMsg, std::vector<int>(), [](std::list<std::vector<int>>& contribs) {
        
        int sum = 0; //contrib.at(0) is whether worker is finished
        int all_lower_bound = -1; //contrib.at(1) is current lower bound
        int all_upper_bound = -1; //contrib.at(2) is current upper bound

        for (auto& contrib : contribs) {
            LOG(V5_DEBG, "Contribution: %i, %i, %i\n", contrib.at(0), contrib.at(1), contrib.at(2));
            sum += contrib.at(0);
            if(contrib.at(1) != -1 && (all_lower_bound == -1 || contrib.at(1) < all_lower_bound)) all_lower_bound = contrib.at(1);
            if(contrib.at(2) != -1 && (all_upper_bound == -1 || contrib.at(1) > all_upper_bound)) all_upper_bound = contrib.at(2);
        }

        std::vector<int> contrib = {sum, all_lower_bound, all_upper_bound};
        return contrib;
    }));

    // Contribution: 0 if finished a.k.a. waiting (not working) and not sent work
    LOG(V2_INFO, "[red] _waiting = %i & _sent_work = %i & _working = %i\n", _waiting, _sent_work, _working);
    const int contrib0 = !((!_working) && (!_sent_work));
    const int contrib1 = _curr_lower_bound;
    const int contrib2 = _curr_upper_bound;
    LOG(V2_INFO, "[red] contribute {%i, %i, %i} to all-reduction\n", contrib0, contrib1, contrib2);
    _red->contribute({contrib0, contrib1, contrib2});
}

void BnbJob::tryEndReduction() {
    if (!_red) return;
    if (!_red->advance().hasResult()) return;

    LOG(V2_INFO, "[red] all-reduction complete\n");

    auto result = _red->extractResult();
    int res0 = *result.data();
    int res1 = *(result.data()+1);
    int res2 = *(result.data()+2);
    LOG(V5_DEBG, "[red] Result has been found\n");
    LOG(V2_INFO, "[red] Result is: %i, %i, %i\n", res0, res1, res2);

    if(res0 == 0) {
        _finished = true;
    }

    if(res2 != -1 && res2 == _curr_lower_bound) {
        _finished = true;
    }

    // Conclude the all-reduction, allowing for this worker to be destructed later
    _red.reset();
}
