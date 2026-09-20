#pragma once

#include "n2_plan_common.h"

#include <string>

namespace codynex::n2 {

struct RunResult {
    bool pass = false;
    std::string json;
};

RunResult runHostAndInProcessSuite();

RunResult prepareColdCheckpointProof(
    const std::string& path,
    PlanRepresentation preRepresentation,
    bool damageCase
);

RunResult resumeColdCheckpointProof(
    const std::string& path,
    PlanRepresentation postRepresentation
);

}  // namespace codynex::n2
