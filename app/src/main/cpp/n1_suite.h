#pragma once

#include <string>

namespace codynex::n1 {

struct RunResult {
    bool pass;
    std::string json;
};

RunResult runAll();

}  // namespace codynex::n1
