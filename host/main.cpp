#include "n2_validation.h"

#include <iostream>

int main() {
    const codynex::n2::RunResult report =
        codynex::n2::runValidatedHostAndInProcessSuite();

    std::cout << report.json << std::endl;
    return report.pass ? 0 : 1;
}
