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
    
    //initialize empty solution
    std::vector<std::vector<int>> cores(_nr_cores, std::vector<int>(1, 0));

    //print beginning (preliminary)
    LOG(V2_INFO, "Test\n");
    log("Beginning", processes, cores);

    //insert solver here
    std::vector<std::vector<int>> solution = compute(processes, cores);

    log("End", processes, solution);
    
    //insert JobResult here
}

std::vector<std::vector<int>> BnbJob::compute(std::vector<int> processes, std::vector<std::vector<int>> cores) {
    std::vector<int> core_length = compute_core_length(cores);

    //if no new processes
    if (processes.empty()) return cores;

    //get current process
    int curr_process = processes[0];
    std::vector<int> new_processes = processes;
    new_processes.erase(new_processes.begin());


    std::vector<int> lengths(_nr_cores, -1);
    std::vector<std::vector<std::vector<int>>> solutions(_nr_cores, cores);

    //add newest process to all cores and branch
    for (int i = 0; i < _nr_cores; i ++) {
        std::vector<std::vector<int>> cores_new = cores;

        cores_new[i].pop_back();
        cores_new[i].push_back(curr_process);
        cores_new[i].push_back(0);

        std::vector<std::vector<int>> solution = compute(new_processes, cores_new);
        
        //find length of solution
        std::vector<int> new_core_length = compute_core_length(solution);
        lengths[i] = *std::max_element(new_core_length.begin(), new_core_length.end());
        solutions[i] = solution;
    }

    //find index of solution with shortest length
    std::vector<int>::iterator min_length = std::min_element(lengths.begin(), lengths.end());
    int index = std::distance(lengths.begin(), min_length);

    return solutions.at(index);
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

void BnbJob::log(std::string reason, std::vector<int> processes, std::vector<std::vector<int>> cores) {
    // turn vectors to strings
    std::string str_processes = "";
    for(int i = 0; i < processes.size(); ++i) {
        str_processes.append(" ");
        str_processes.append(std::to_string(processes.at(i)));
    }
    std::string str_core_lengths = "";
    for(int i = 0; i < _nr_cores; i++) {
        str_core_lengths.append(" ");
        str_core_lengths.append(std::to_string(compute_core_length(cores).at(i)));
    }
    std::string str_cores = "";
    for(int i = 0; i < _nr_cores; ++i) {
        for( int j = 0; j < cores[i].size(); j++) {
            str_cores.append(" ");
            str_cores.append(std::to_string(cores[i][j]));
        }
    }

    LOG(V2_INFO, "%s: (Nr Processes: %i) (Nr Cores: %i) (Processes:%s) (Core Lengths:%s) (Cores:%s)\n",
        reason.c_str(), _nr_processes, _nr_cores, str_processes.c_str(), str_core_lengths.c_str(), str_cores.c_str());
}
