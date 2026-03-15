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
#include "util/sys/watchdog.hpp"

BnbJob::BnbJob(const Parameters& params, const JobSetup& setup, AppMessageTable& table)
    : Job(params, setup, table) {

        // Sending job messages along the job tree (i.e., to a direct parent or child)
        // is very easy via the convenience methods like getJobTree().sendToParent(msg) etc.
        // In our case, since we want to send messages to arbitrary workers in our tree,
        // we need a JobComm instance to be constructed for us in the background.
        assert(_params.jobCommUpdatePeriod() > 0 || log_return_false("[ERROR] For this application to work,"
            " you must explicitly enable job communicators with the -jcup option, e.g., -jcup=0.1\n"));
        
        _result.result = -1; // no result present at initilization
        _result.id = getId();
        _result.revision = 0;

        _num_workers = MyMpi::size(MPI_COMM_WORLD);
}

void BnbJob::appl_start() {
    // Initialize pseudo-random permutation with the number of workers -
    // use the 1st integer in the job's payload as a random seed
    _perm = AdjustablePermutation(_num_workers, getDescription().getFormulaPayload(0)[0]);

    LOG(V5_DEBG, "myRank: %i myIndex: %i\n", getJobTree().getRank(), getJobTree().getIndex());

    init();
    future = ProcessWideThreadPool::get().addTask([this]() {loop();});
}

void BnbJob::appl_terminate() {
    {
        auto lock = list_mtx.getLock();
        _stopSearch = true;
    }
    _loop_cond_var.notify();
    LOG(V5_DEBG, "[term] Terminated\n");
}

int BnbJob::appl_solved() {

    // _finished has two meanings:
    // - I should stop working -> _stopSearch
    // - I have a globally best solution I'd like to report -> _reportableSolution
    
    if(!_stopSearch || !_reportableSolution) return -1;
    LOG(V5_DEBG, "[solved] _stopSearch = %i, _reportableSolution = %i\n", _stopSearch, _reportableSolution);

    bool empty;
    {
        auto lock = list_mtx.getLock();
        empty = _work_list.empty();
    }

    //assert(empty && !_working);

    {
        auto lock = solution_mtx.getLock();
        std::vector<int> _internal_solution;
        for(int i = 0; i < _best_solution.machines.size(); i++) {
            for(int j = 0; j < _best_solution.machines.at(i).size(); j++) {
                _internal_solution.push_back(_best_solution.machines[i][j]);
            }
        }
        _result.result = 10;
        _result.setSolutionToSerialize(_internal_solution.data(), _internal_solution.size());

        LOG(V2_INFO, "%s", transform_for_log("[solved] End", _best_solution).c_str());
    }

    return _result.result;
}

JobResult&& BnbJob::appl_getResult() {
    return std::move(_result);
}

// Called periodically by the main thread to allow the worker to emit messages.
void BnbJob::appl_communicate() {
    // Are enough workers available?
    if (getJobTree().isRoot() && !_send_messages && getVolume() < _num_workers) {
        if (getAgeSinceActivation() < 1) return; // wait for up to 1s after appl_start

        LOG(V2_INFO, "[msg] Unable to get %i workers within 1 second - giving up\n", _num_workers);

        // Report an "unknown" result (code 0)
        insertResult(0, {-1});

        return;
    }

    //Allow messages after conditions are met
    if (!_send_messages && getVolume() == _num_workers && getJobComm().getWorldRankOrMinusOne(_num_workers-1) >= 0) {

        LOG(V5_DEBG, "[msg] Messages allowed starting now\n");
        _send_messages = true;
    }

    //Check if new work needs to be requested and request if possible
    bool empty;
    {
        auto lock = list_mtx.getLock();
        empty = _work_list.empty();
    }
    if(empty && !_stopSearch && !_reportableSolution) {
        tracker.num_queries++;

        if (!_send_messages) {
            LOG(V5_DEBG, "[msg] Tried requesting work but messages are not allowed\n");
            tracker.num_nonsucc_nomsg++;
        } else if (_waiting) {
            LOG(V5_DEBG, "[msg] Waiting\n");
            tracker.num_queries--; //because were still waiting on the last one to be filled
/*         } else if (_first && !getJobTree().isRoot()) { // requesting from root
            //Request work
            JobMessage msg = getMessageTemplate();
            msg.tag = MSG_WORK_STEALING_QUERY;
            msg.payload = {0}; // irrelevant

            // Check if request can be sent
            int randomIndex = 0;
            // Use our JobComm to convert the tree index into an addressable MPI rank.
            int recvRank = getJobComm().getWorldRankOrMinusOne(randomIndex);
            if (recvRank == -1 || getJobTree().getRank() == randomIndex) {
                LOG(V5_DEBG, "[msg] Tried requesting work but receiving rank was invalid or my own: %i\n", recvRank);
                tracker.num_nonsucc_rankinvld++;
            } else {
                //Send
                msg.treeIndexOfDestination = randomIndex;
                msg.contextIdOfDestination = getJobComm().getContextIdOrZero(randomIndex);
                assert(msg.contextIdOfDestination != 0);

                getJobTree().send(recvRank, MSG_SEND_APPLICATION_MESSAGE, msg);
                LOG(V2_INFO, "[msg] Requested work stealing from: %i\n", recvRank);
                _waiting = 1;
                _first = 0;
            }  */           
        } else {
            //Request work
            JobMessage msg = getMessageTemplate();
            msg.tag = MSG_WORK_STEALING_QUERY;
            msg.payload = {0}; // irrelevant

            // Check if request can be sent
            int randomIndex = rand() % _num_workers;
            int recvRank = getJobComm().getWorldRankOrMinusOne(randomIndex); // use JobComm to convert tree index into addressable MPI rank
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
    if (!_stopSearch && !_reportableSolution && _periodic_reduction.ready()) tryStartReduction();
    tryEndReduction();
}

// React to an incoming message.
void BnbJob::appl_communicate(int source, int mpiTag, JobMessage& msg) {
    LOG(V2_INFO, "[msg] Message %i with Payload %i from %i received.\n", msg.tag, msg.payload[0], source);

    if(msg.tag == MSG_FINISHED) {
        {
            auto lock = list_mtx.getLock();
            _stopSearch = true;
        }
        _loop_cond_var.notify();
        LOG(V2_INFO, "[term] Finished set to 1\n");
        return;
    }

    if(_stopSearch || _reportableSolution) {
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
            LOG(V2_INFO, "[msg] Message returned to sender %i with tag %i\n", source, msg.tag);
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
            //tracker
            tracker.num_nonsucc_empty++;
        } else {
            LOG(V2_INFO, "[msg] Work stealing query successful. Filling work stack.\n");
            addToQueue(msg.payload);
            LOG(V2_INFO, "[msg] Work stack is filled.\n", source, msg.tag, msg.payload[0]);
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
}

int BnbJob::getDemand() const {
    // return Job::getDemand();
    return _num_workers; // we strictly want this number of workers
}

//PRIVATE METHODS

// Initialize problem
void BnbJob::init() {
    // Read problem
    size_t problem_size = getDescription().getFormulaPayloadSize(0);
    int const *problem = getDescription().getFormulaPayload(0);

    // Fill variables with problem input
    _nr_tasks = problem[0];
    _nr_machines = problem[1];
    std::vector<int> tasks;
    for(int i = 0; i < _nr_tasks; ++i) tasks.push_back(problem[i+2]);

    appr_amount_of_expl = pow((long double) 2.0, (long double) _nr_tasks);

    // Initializing work – only done by root
    if(getJobTree().isRoot()) {
        auto lock = list_mtx.getLock();
        std::vector<std::vector<int>> machines(_nr_machines, std::vector<int>(1, 0));
        Work work = {0, tasks, machines, {-1, -1}};
        _work_list.push_back(work);
        _working = 1;

        //initialize lower bound
        bounds.curr_lower_bound = tasks[0];

        int average_size = 0;
        for (int i = 0; i < tasks.size(); i++) average_size += tasks[i];
        average_size /= tasks.size();
        if (bounds.curr_lower_bound < average_size) bounds.curr_lower_bound = average_size;

        int possible_lower_bound = tasks[_nr_machines] + tasks[_nr_machines+1];
        if(bounds.curr_lower_bound < possible_lower_bound) bounds.curr_lower_bound = possible_lower_bound;
        LOG(V2_INFO, "[bou] Initiliased to {lower, upper, best} = {%i, %i, %i}.\n", bounds.curr_lower_bound, bounds.curr_upper_bound, bounds.curr_best_solution);

        LOG(V2_INFO, "%s", transform_for_log("[start] Beginning", work).c_str());
    }
}

// Calculate solution by looping over jobs
void BnbJob::loop() {
    // Set watchdog for loop iteration
    Watchdog watchdog(true, 500, true);
    watchdog.setWarningPeriod(500);
    watchdog.setAbortPeriod(10'000);

    // Start tracker
    tracker.activation_time = Timer::elapsedSeconds();
    tracker.wait_threads_time = tracker.activation_time;
    LOG(V5_DEBG, "[track] Start tracker & Start wait threads\n");
    
    // Possibly wait for all threads
    if(getJobTree().isRoot() && !_send_messages) {
        LOG(V2_INFO, "[stack] Start waiting for other threads: %i\n", _send_messages);
        float timer = Timer::elapsedSeconds();
        while(!_send_messages) {
            if(Timer::elapsedSeconds() - timer == 1) {
                LOG(V5_DEBG, "Still waiting:%i\n", _send_messages);
                timer += 1;
            }
        }
        LOG(V2_INFO, "[stack] End waiting: %i\n", _send_messages);
    }

    // Switch tracker
    tracker.curr_time = Timer::elapsedSeconds();
    tracker.time_spent_wait_threads = tracker.curr_time - tracker.wait_threads_time;
    tracker.outside_time = tracker.curr_time;
    LOG(V5_DEBG, "[track] End wait threads & Start outside\n");
    
    // Loop over jobs
    do {
        // Switch tracker
        tracker.curr_time = Timer::elapsedSeconds();
        tracker.time_spent_outside += tracker.curr_time - tracker.outside_time;
        tracker.check_time = tracker.curr_time;
        LOG(V5_DEBG, "[track] End outside & Start empty & Start check\n");

        //check if stack is empty and stop working if necessary
        bool empty;
        {
            auto lock = list_mtx.getLock();
            empty = _work_list.empty();

            // Switch tracker
            tracker.curr_time = Timer::elapsedSeconds();
            tracker.time_spent_check += tracker.curr_time - tracker.check_time;
            tracker.wait_full_time = tracker.curr_time;
            // actually here

            if(empty) {
                LOG(V2_INFO, "[track] End check & Start wait full\n");

                LOG(V5_DEBG, "[stack] Stack empty. Stopping Loop\n");
                _working = 0;

                _loop_cond_var.waitWithLockedMutex(lock, [&]() {return (_working || _stopSearch || _reportableSolution);});
                LOG(V5_DEBG, "[stack] working: %i or finished: %i %i\n", _working, _stopSearch, _reportableSolution);

                // Switch tracker
                tracker.curr_time = Timer::elapsedSeconds();
                tracker.time_spent_wait_full += tracker.curr_time - tracker.wait_full_time;
                tracker.outside_time = tracker.curr_time;
                LOG(V2_INFO, "[track] End wait full & Start outside\n");

                if(_stopSearch || _reportableSolution) break;
                LOG(V5_DEBG, "[stack] Restarting loop: %i\n", _work_list.size());
                
                // Switch tracker
                tracker.curr_time = Timer::elapsedSeconds();
                tracker.time_spent_outside += tracker.curr_time - tracker.outside_time;
                tracker.wait_full_time = tracker.curr_time;
                LOG(V2_INFO, "[track] End outside & Start wait full\n");

                LOG(V2_INFO, "[track] End wait full & Start get work\n");
            }
        }

        // Switch tracker
        tracker.curr_time = Timer::elapsedSeconds();
        tracker.time_spent_wait_full += tracker.curr_time - tracker.wait_full_time;
        tracker.get_work_time = tracker.curr_time;
        // actually here

        //else: work
        Work curr_work;
        {
            auto lock = list_mtx.getLock();
            curr_work = _work_list.back();
            _work_list.pop_back();
        }

        LOG(V5_DEBG, "%s", transform_for_log("[stack] In Loop. Currently at:", curr_work).c_str());
        if (num_expl_nodes % 100000 == 0) LOG(V2_INFO, "[stack] In loop. Jobs left: %i\n", _work_list.size()+1);

        // Switch tracker
        tracker.curr_time = Timer::elapsedSeconds();
        tracker.time_spent_get_work += tracker.curr_time - tracker.get_work_time;
        tracker.work_time = tracker.curr_time;
        LOG(V5_DEBG, "[track] End get work & Start work\n");

        branch(curr_work);
        num_expl_nodes++;

        // Switch tracker
        tracker.curr_time = Timer::elapsedSeconds();
        tracker.time_spent_work += tracker.curr_time - tracker.work_time;
        tracker.compare_time = tracker.curr_time;
        LOG(V5_DEBG, "[track] End work & Start compare\n");
        
        //compare solutions
        if (curr_work.completed == 1) {
            //find length of solution
            int new_length = -1;
            std::vector<int> new_machine_workload = machine_workloads(curr_work.machines);
            new_length = *std::max_element(new_machine_workload.begin(), new_machine_workload.end());
        
            if (bounds.curr_best_solution == -1 || new_length < bounds.curr_best_solution) {
                auto lock = solution_mtx.getLock();
                _best_solution = curr_work;
                bounds.curr_best_solution = new_length;
                if (bounds.curr_lower_bound > bounds.curr_best_solution) bounds.curr_lower_bound = bounds.curr_best_solution;
                if (bounds.curr_upper_bound == -1 || bounds.curr_upper_bound > bounds.curr_best_solution) bounds.curr_upper_bound = bounds.curr_best_solution;
                LOG(V2_INFO, "[bou] Updated to {lower, upper, best} = {%i, %i, %i}.\n", bounds.curr_lower_bound, bounds.curr_upper_bound, bounds.curr_best_solution);
            }        
        }

        watchdog.reset();

        // Switch tracker
        tracker.curr_time = Timer::elapsedSeconds();
        tracker.time_spent_compare += tracker.curr_time - tracker.compare_time;
        tracker.outside_time = tracker.curr_time;
        LOG(V5_DEBG, "[track] End compare & Start outside\n");
    } while(_working && !_stopSearch && !_reportableSolution);
    LOG(V2_INFO, "[stack] Succesfully broken out of loop\n");

    // Finish tracker
    tracker.curr_time = Timer::elapsedSeconds();
    tracker.time_spent_outside += tracker.curr_time - tracker.outside_time;
    tracker.time_since_activation = tracker.curr_time - tracker.activation_time;
    LOG(V5_DEBG, "[track] End outside & End tracker\n");
    printTracking();
}

void BnbJob::branch(Work& work) {
    std::vector<int> machine_workload = machine_workloads(work.machines);
    
    //if no new tasks
    if (work.tasks.empty()) {
        work.completed = 1;
        return;
    }

    int max_loop = _nr_machines;

    // if three assignments left (Rule No 3)
    if (work.tasks.size() == 3) {
        pruning_three_jobs_left(work, machine_workload);
        return;
    }

    // if i < m then only the i least loaded processors need to be considered (Rule No 4)
    if (work.tasks.size() < _nr_machines) {
        //TODO: fragen ob diese (in place und custom) sortierung fine ist
        LOG(V5_DEBG, "%s", transform_for_log("before", work).c_str());
        struct
        {
            bool operator()(std::vector<int> a, std::vector<int> b) const { 
                return (std::accumulate(a.begin(), a.end(), 0)) < (std::accumulate(b.begin(), b.end(), 0));
            }
        }
        customSort;
    
        std::sort(work.machines.begin(), work.machines.end(), customSort);
        LOG(V5_DEBG, "%s", transform_for_log("after", work).c_str());

        max_loop = work.tasks.size();
    }

    // if there is still a valid solution given the upper bound (Rule No 5)
    if (work.tasks[0] == work.tasks[work.tasks.size() - 1] && bounds.curr_upper_bound != -1) {
        int sum = 0;
        for (int x = 0; x < work.machines.size(); x++) {
            sum += (bounds.curr_upper_bound - machine_workload[x]) / work.tasks[0];
        }
        if (sum < work.tasks.size()) return;
    }
    
    //get current task
    std::vector<int> new_tasks = work.tasks;
    int curr_task = new_tasks[0];
    new_tasks.erase(new_tasks.begin());
    
    //add newest task to all machines and branch
    for (int i = 0; i < max_loop; i ++) {
        //PRUNING
        bool prune = false;

        //if multiple machines with same length (Rule No 1)
        for (int j = 0; j < i; j++) {
            if (machine_workload[i] == machine_workload[j]) prune = true;
        }

        //prune if length of last assigned job equal length of current job (Rule No 2)
        //if ((work.last_assigned[0] == curr_task) && (work.last_assigned[1] != -1) && (work.last_assigned[1] < i)) prune = true;
        

        //END PRUNING
        if (prune) continue;

        auto lock = list_mtx.getLock();
        std::vector<std::vector<int>> new_machines = work.machines;
        
        new_machines[i].pop_back();
        new_machines[i].push_back(curr_task);
        new_machines[i].push_back(0);
              
        Work new_work = {0, new_tasks, new_machines, {curr_task, i}};
        _work_list.push_back(new_work);
    }
    
    return;
}

void BnbJob::pruning_three_jobs_left(Work& work, std::vector<int>& machine_workload) {
    // (1) Assign each of the jobs to the least loaded processor respectively
    {
        Work curr_work = work;
        std::vector<int> curr_machine_workload = machine_workload;
        for (int i = 0; i < 3; i++) {
            int index_smallest_workload = std::distance(std::begin(curr_machine_workload), std::min_element(std::begin(curr_machine_workload), std::end(curr_machine_workload)));

            std::vector<int> new_tasks = work.tasks;
            int curr_task = new_tasks[0];
            new_tasks.erase(new_tasks.begin());

            std::vector<std::vector<int>> new_machines = work.machines;
            new_machines[index_smallest_workload].pop_back();
            new_machines[index_smallest_workload].push_back(curr_task);
            new_machines[index_smallest_workload].push_back(0);

            curr_work = {0, new_tasks, new_machines, {curr_task, index_smallest_workload}};
            curr_machine_workload = machine_workloads(new_machines);
        }
        {
            auto lock = list_mtx.getLock();
            _work_list.push_back(curr_work);
        }
    }

    // (2) Assign the third-to-last job to the second least loaded processor, then assign the other two jobs as in (1)
    {
        Work curr_work = work;
        std::vector<int> curr_machine_workload = machine_workload;
        int index_smallest_workload;
        //SECOND SMALLEST ONLY THIS TIME
        {
            std::vector<int> copy_machine_workload = curr_machine_workload;
            index_smallest_workload = std::distance(copy_machine_workload.begin(), std::min_element(copy_machine_workload.begin(), copy_machine_workload.end()));
            copy_machine_workload.erase(copy_machine_workload.begin()+index_smallest_workload);
            auto second_smallest_workload = std::min_element(copy_machine_workload.begin(), copy_machine_workload.end());
            index_smallest_workload = std::distance(curr_machine_workload.begin(), std::find(curr_machine_workload.begin(), curr_machine_workload.end(), *second_smallest_workload));
        }
        for (int i = 0; i < 3; i++) {
            std::vector<int> new_tasks = work.tasks;
            int curr_task = new_tasks[0];
            new_tasks.erase(new_tasks.begin());

            std::vector<std::vector<int>> new_machines = work.machines;
            new_machines[index_smallest_workload].pop_back();
            new_machines[index_smallest_workload].push_back(curr_task);
            new_machines[index_smallest_workload].push_back(0);

            curr_work = {0, new_tasks, new_machines, {curr_task, index_smallest_workload}};
            curr_machine_workload = machine_workloads(new_machines);
            int index_smallest_workload = std::distance(std::begin(curr_machine_workload), std::min_element(std::begin(curr_machine_workload), std::end(curr_machine_workload)));
        }
        {
            auto lock = list_mtx.getLock();
            _work_list.push_back(curr_work);
        }
    }
}

std::vector<int> BnbJob::splitQueue() {
    auto lock = list_mtx.getLock();
    std::vector<int> sendQueue;
    
    if (_work_list.size() < 2) {
        LOG(V2_INFO, "[steal] I am not sending work: _work_list.size() = %i\n", _work_list.size());
        sendQueue.push_back(-1);
    } else if ((appr_amount_of_expl - num_expl_nodes) < 100) {
        LOG(V2_INFO, "[steal] I am not sending work: appr_amount_of_expl = %i, num_explored_nodes = %i\n", appr_amount_of_expl, num_expl_nodes);
        sendQueue.push_back(-1);
    } else {
        // Send the search tree that has been explored the least (aka stack element 0)
        LOG(V2_INFO, "[steal] I am sending work\n");

        Work work_front = _work_list.front();
        _work_list.pop_front();
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

    return sendQueue;
}

void BnbJob::addToQueue(std::vector<int>& message) {
    LOG(V2_INFO, "[adding] Adding starting now\n");
    auto lock = list_mtx.getLock();
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

        _work_list.push_back(work);
    }

    _working = 1;
    _waiting = 0;
    _loop_cond_var.notify();
    LOG(V2_INFO, "[adding] Adding ending now\n");
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

// Print all tracking information
void BnbJob::printTracking() {
    //LOG(V2_INFO, "[tracking] Number of explored nodes: %i\n", num_expl_nodes);

    LOG(V2_INFO, 
        "[tracking] Number of queries in total: %i - succesful: %i - before msgs allowed: %i - rank invalid: %i - reply empty: %i\n", 
        tracker.num_queries, tracker.num_succ_queries,  tracker.num_nonsucc_nomsg, tracker.num_nonsucc_rankinvld, tracker.num_nonsucc_empty);

    float perc_all = 0;

    {
        float perc_wait_threads = tracker.time_spent_wait_threads / tracker.time_since_activation;
        perc_all += perc_wait_threads;
        LOG(V2_INFO, "[tracking] Time wait threads: 0 %f\n", perc_wait_threads);

        float perc_check = tracker.time_spent_check / tracker.time_since_activation;
        perc_all += perc_check;
        LOG(V2_INFO, "[tracking] Time check: 1 %f\n", perc_check);

        float perc_wait_full = tracker.time_spent_wait_full / tracker.time_since_activation;
        perc_all += perc_wait_full;
        LOG(V2_INFO, "[tracking] Time wait full: 2 %f\n", perc_wait_full);

        float perc_get_work = tracker.time_spent_get_work / tracker.time_since_activation;
        perc_all += perc_get_work;
        LOG(V2_INFO, "[tracking] Time get work: 3 %f\n", perc_get_work);

        float perc_work = tracker.time_spent_work / tracker.time_since_activation;
        perc_all += perc_work;
        LOG(V2_INFO, "[tracking] Time work: 4 %f\n", perc_work);

        float perc_compare = tracker.time_spent_compare / tracker.time_since_activation;
        perc_all += perc_compare;
        LOG(V2_INFO, "[tracking] Time compare: 5 %f\n", perc_compare);

        float perc_outside = tracker.time_spent_outside / tracker.time_since_activation;
        perc_all += perc_outside;
        LOG(V2_INFO, "[tracking] Time outside: 6 %f\n", perc_outside);
    }

    LOG(V2_INFO, "[track] All time: %f\n", perc_all);
}

void BnbJob::tryStartReduction() {
    JobMessage baseMsg = getMessageTemplate();
    baseMsg.tag = ALLRED;
    _red.reset(new JobTreeAllReduction(getJobTree().getSnapshot(), baseMsg, std::vector<int>(), [](std::list<std::vector<int>>& contribs) {
        
        int sum = 0; //contrib.at(0) is whether worker is finished
        int all_lower_bound = -1; //contrib.at(1) is current lower bound
        int all_upper_bound = -1; //contrib.at(2) is current upper bound

        for (auto& contrib : contribs) {
            LOG(V5_DEBG, "[red] Contribution: %i, %i, %i\n", contrib.at(0), contrib.at(1), contrib.at(2));
            sum += contrib.at(0);
            if(contrib.at(1) != -1 && (all_lower_bound == -1 || contrib.at(1) < all_lower_bound)) all_lower_bound = contrib.at(1);
            if(contrib.at(2) != -1 && (all_upper_bound == -1 || contrib.at(1) > all_upper_bound)) all_upper_bound = contrib.at(2);
        }

        std::vector<int> contrib = {sum, all_lower_bound, all_upper_bound};
        return contrib;
    }));

    // Contribution: 0 if finished a.k.a. waiting (not working) and not sent work
    LOG(V5_DEBG, "[red] _waiting = %i & _sent_work = %i & _working = %i\n", _waiting, _sent_work, _working);
    const int contrib0 = !((!_working) && (!_sent_work));
    const int contrib1 = bounds.curr_lower_bound;
    const int contrib2 = bounds.curr_upper_bound;
    LOG(V2_INFO, "[red] & [bou] Contributed {finished, lowerBound, upperBound} = {%i, %i, %i} to all-reduction.\n", contrib0, contrib1, contrib2);
    _red->contribute({contrib0, contrib1, contrib2});
}

void BnbJob::tryEndReduction() {
    if (!_red) return;
    if(!_red->advance().isValid()) LOG(V5_DEBG, "[red] all-reduction without validity\n");
    if (!_red->advance().hasResult()) return;

    LOG(V5_DEBG, "[red] All-Reduction completed.\n");

    auto result = _red->extractResult();
    int nbActive = *result.data();
    int lowerBound = *(result.data()+1);
    int upperBound = *(result.data()+2);
    LOG(V2_INFO, "[red] & [bou] All-Reduction resulted in {finished, lowerBound, upperBound} = {%i, %i, %i}.\n", nbActive, lowerBound, upperBound);

    if(nbActive == 0) {
        {
            auto lock = list_mtx.getLock();
            _stopSearch = true;
        }
        _loop_cond_var.notify();
        LOG(V2_INFO, "[red] Thread will stop search: nbActive = %i\n", nbActive);
        
        if (upperBound == bounds.curr_best_solution) {
            {
                auto lock = list_mtx.getLock();
                _reportableSolution = true;
            }
            _loop_cond_var.notify();
            LOG(V2_INFO, "[red] & [bou] Thread has reportable solution: upperBound = %i, lowerBound = %i, currSolution = %i\n", upperBound, lowerBound, bounds.curr_best_solution);
        }
    }

    // Three fields: best known upper, lower bound, cost of currently present solution.
    // Update the former two here with the result of the all reduction.

    if(lowerBound == bounds.curr_best_solution) {
        {
            auto lock = list_mtx.getLock();
            _reportableSolution = true;
        }
        _loop_cond_var.notify();
        LOG(V2_INFO, "[red] & [bou] Thread has reportable solution: upperBound = %i, lowerBound = %i, currSolution = %i\n", upperBound, lowerBound, bounds.curr_best_solution);
    } 

    LOG(V2_INFO, "[bou] Before reduction: {lower, upper, best} = {%i, %i, %i}\n", bounds.curr_lower_bound, bounds.curr_upper_bound, bounds.curr_best_solution);
    bounds.curr_upper_bound = upperBound;
    bounds.curr_lower_bound = lowerBound;
    LOG(V2_INFO, "[bou] After redution: {lower, upper, best} = {%i, %i, %i}\n", bounds.curr_lower_bound, bounds.curr_upper_bound, bounds.curr_best_solution);

    // Conclude the all-reduction, allowing for this worker to be destructed later
    _red.reset();
}
