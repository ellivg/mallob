
#ifndef DOMPASCH_MALLOB_BNB_READER_HPP
#define DOMPASCH_MALLOB_BNB_READER_HPP

#include <string>
#include <vector>

#include "data/job_description.hpp"

class JobDescription;

namespace BnbReader {
    /*
    Read a revision of a job of the "Branch and Bound" application.
    */
    bool read(const std::vector<std::string>& filenames, JobDescription& desc);
};

#endif
