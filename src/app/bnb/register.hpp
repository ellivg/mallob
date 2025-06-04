
#pragma once

#include "app/app_message_subscription.hpp"
#include "app/app_registry.hpp"
#include "data/job_processing_statistics.hpp"
#include "bnb_job.hpp"
#include "bnb_reader.hpp"

void register_mallob_app_bnb() {
    app_registry::registerApplication("BNB",
        // Job reader
        [](const Parameters& params, const std::vector<std::string>& files, JobDescription& desc) {
            return BnbReader::read(files, desc);
        },
        // Job creator
        [](const Parameters& params, const Job::JobSetup& setup, AppMessageTable& table) -> Job* {
            return new BnbJob(params, setup, table);
        },
        // Job solution formatter
        [](const Parameters& params, const JobResult& result, const JobProcessingStatistics& stat) {
            // An actual application would nicely format the result here ...
            return nlohmann::json();
        }
    );
}
