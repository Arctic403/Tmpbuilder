#pragma once

#include <string>

namespace codynex::n0 {

struct RunResult {
    bool pass;
    std::string json;
};

RunResult runAll();

}  // namespace codynex::n0
