
#include "bnb_reader.hpp"

#include <fstream>
#include <iostream>

bool BnbReader::read(const std::vector<std::string>& filenames, JobDescription& desc) {

    // read the description and write serialized data 
    // using desc.addPermanentData and desc.addTransientData

    desc.beginInitialization(0);

    //open file
    std::ifstream ifile(filenames[0].c_str(), std::ios::in);

    if (!ifile.is_open()) {
        std::cerr << "There was a problem opening the input file!\n";
        return false;
    }

    //parse first line
    std::string problem = "";
    std::string type = "";
    int nr_processes = 0;
    int nr_cores = 0;
    
    ifile >> problem;
    ifile >> type;
    ifile >> nr_processes;
    ifile >> nr_cores;

    if(!(problem=="p" && type=="p_cmax")) {
        std::cerr << "This is the wrong problem!\n";
        return false;
    }

    desc.addData(nr_processes);
    desc.addData(nr_cores);
    
    //parse main file
    for (int curr_process = 0; curr_process < nr_processes; ++curr_process) {
        int p_length = 0;
        ifile >> p_length;
        desc.addData(p_length);
    }

    //parse end
    int end = -1;
    ifile >> end;

    if(!(end==0)) {
        std::cerr << "The file length is not as written!\n";
        return false;
    }

    ifile.close();
    desc.endInitialization();

    // success
    return true;
}
