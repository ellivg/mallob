#include "bnb_job.hpp"

#include <iostream>
#include <string>

BnbJob::BnbJob(const Parameters& params, const JobSetup& setup, AppMessageTable& table)
    : Job(params, setup, table) {
}

void BnbJob::appl_start() {
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
    Task best_solution;
    int best_length = -1;

    while(!_task_queue.empty()) {
        Task curr_task = _task_queue.front();
        _task_queue.pop();

        Task solution = compute(curr_task);

                
        //compare solutions
        if (solution.completed == 1) {
            //find length of solution
            int new_length = -1;
            std::vector<int> new_core_length = compute_core_length(solution.cores);
            new_length = *std::max_element(new_core_length.begin(), new_core_length.end());
        
            if (best_length == -1 || new_length < best_length) {
                //log("NEWWWWW", curr_task);
                best_solution = solution;
                best_length = new_length;
            }        
        }

    }


    log("End", best_solution);
    
    //insert JobResult here
}

BnbJob::Task BnbJob::compute(Task task) {

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
