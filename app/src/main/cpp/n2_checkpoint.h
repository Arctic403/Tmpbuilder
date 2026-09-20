#pragma once

#include "n2_core.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace codynex::n2 {

inline constexpr std::size_t kMaxCheckpointBytes = 8192U;

struct CheckpointLedger {
    std::size_t checkpointBytes = 0U;
    std::size_t substrateSerializedBytes = 0U;
    std::size_t generatedMachinerySerializedBytes = 0U;
    std::size_t authoritySerializedBytes = 0U;
    std::uint64_t checkpointHash = 0U;
    std::uint64_t schemaHash = 0U;
};

struct DecodedCheckpoint {
    bool ok = false;
    int width = 0;
    int height = 0;
    std::vector<Replica> replicas;
    std::uint64_t checkpointHash = 0U;
    std::string reason;
};

std::vector<std::uint8_t> encodeCheckpoint(
    const Mesh& mesh,
    CheckpointLedger* ledger
);

DecodedCheckpoint decodeCheckpoint(
    const std::vector<std::uint8_t>& bytes
);

bool restoreCheckpoint(
    const DecodedCheckpoint& decoded,
    Mesh& mesh
);

bool writeCheckpointFile(
    const std::string& path,
    const std::vector<std::uint8_t>& bytes
);

std::vector<std::uint8_t> readCheckpointFile(
    const std::string& path
);

bool removeCheckpointFile(const std::string& path);

std::uint64_t checkpointSchemaHash();

}  // namespace codynex::n2
