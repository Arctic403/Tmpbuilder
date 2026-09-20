#pragma once

#include "../app/src/main/cpp/cxe_format.h"

#include <cstdint>
#include <string>
#include <vector>

namespace codynex::lr0::hosttest {

struct HostStateDecl {
    bool persistent = false;
    std::int64_t initialI64 = 0;
};

class FunctionCode {
public:
    FunctionCode& constI64(std::int64_t value);
    FunctionCode& loadState(std::uint16_t stateId);
    FunctionCode& storeState(std::uint16_t stateId);
    FunctionCode& addI64();
    FunctionCode& returnValue();

    const std::vector<std::uint8_t>& bytes() const;

private:
    std::vector<std::uint8_t> bytes_;
};

std::vector<std::uint8_t> buildExecutable(
    const std::vector<HostStateDecl>& states,
    const std::vector<FunctionCode>& functions
);

void refreshIntegrity(std::vector<std::uint8_t>& bytes);

bool writeBinaryFile(
    const std::string& path,
    const std::vector<std::uint8_t>& bytes
);

}  // namespace codynex::lr0::hosttest
