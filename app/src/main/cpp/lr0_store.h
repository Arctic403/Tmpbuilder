#pragma once

#include "lr0_runtime.h"

#include <cstdint>
#include <string>
#include <vector>

namespace codynex::lr0 {

struct StoreResult {
    bool ok = false;
    bool recovered = false;
    bool fallbackUsed = false;
    int activeSlot = -1;
    std::uint64_t programHash = 0U;
    std::string reason;
};

class RecoveryStore {
public:
    explicit RecoveryStore(std::string rootDirectory);

    StoreResult commit(
        const std::vector<std::uint8_t>& programBytes,
        const PersistentStateImage& state
    );

    StoreResult recover(
        LiveRuntime& runtime,
        std::vector<std::uint8_t>& activeProgramBytes
    );

private:
    std::string slotProgramPath(int slot) const;
    std::string slotStatePath(int slot) const;
    std::string recordPath() const;

    std::string rootDirectory_;
};

}  // namespace codynex::lr0
