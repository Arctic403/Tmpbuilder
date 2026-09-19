#include <iostream>

#include "../app/src/main/cpp/codynex_n0.h"

int main() {
    const codynex::n0::RunResult report =
        codynex::n0::runAll();

    std::cout << report.json << std::endl;
    return report.pass ? 0 : 1;
}
