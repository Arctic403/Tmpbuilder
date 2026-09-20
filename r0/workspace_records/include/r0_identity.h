#pragma once

#include "r0_types.h"

#include <vector>

namespace codynex::r0::workspace_records {

// Runs only authoritative/exact identity reasoning.
//
// This function intentionally does not perform text similarity or infer a
// heuristic rename. Any unmatched evidence is left for later lanes.
IdentityResult correlateExactIdentity(
    const std::vector<FileEvidence>& before,
    const std::vector<FileEvidence>& after
);

}  // namespace codynex::r0::workspace_records
