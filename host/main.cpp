#include "n1_suite.h"

#include <iostream>

int main() {
    const codynex::n1::RunResult report =
        codynex::n1::runAll();

    std::cout << report.json << std::endl;
    return report.pass ? 0 : 1;
}
