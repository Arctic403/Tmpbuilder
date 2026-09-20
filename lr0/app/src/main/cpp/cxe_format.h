#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace codynex::lr0 {

inline constexpr std::size_t kMaxExecutableBytes = 64U * 1024U;
inline constexpr std::size_t kMaxStates = 64U;
inline constexpr std::size_t kMaxFunctions = 64U;
inline constexpr std::size_t kMaxCodeBytes = 48U * 1024U;
inline constexpr std::size_t kMaxStackEntries = 256U;
inline constexpr std::uint64_t kMaxInstructionsPerCall = 4096U;

enum class ValueType : std::uint8_t {
    I64 = 1U
};

enum class Opcode : std::uint8_t {
    ConstI64 = 0x01U,
    LoadState = 0x02U,
    StoreState = 0x03U,
    AddI64 = 0x04U,
    Return = 0x05U
};

struct StateDecl {
    std::uint16_t id = 0U;
    ValueType type = ValueType::I64;
    bool persistent = false;
    std::int64_t initialI64 = 0;
};

struct FunctionDecl {
    std::uint16_t id = 0U;
    std::uint32_t codeOffset = 0U;
    std::uint32_t codeLength = 0U;
    std::size_t validatedPeakStack = 0U;
    std::uint64_t validatedInstructionCount = 0U;
};

struct ProgramImage {
    std::vector<StateDecl> states;
    std::vector<FunctionDecl> functions;
    std::vector<std::uint8_t> code;
    std::uint64_t schemaFingerprint = 0U;
    std::uint64_t executableHash = 0U;
    std::size_t encodedBytes = 0U;
};

struct DecodeResult {
    bool ok = false;
    ProgramImage program;
    std::string reason;
};

std::uint64_t fnv1a64(
    const std::uint8_t* data,
    std::size_t size
);

std::uint64_t computePersistentSchemaFingerprint(
    const std::vector<StateDecl>& states
);

DecodeResult decodeAndValidateExecutable(
    const std::vector<std::uint8_t>& bytes
);

std::vector<std::uint8_t> readExecutableFile(
    const std::string& path
);

}  // namespace codynex::lr0
