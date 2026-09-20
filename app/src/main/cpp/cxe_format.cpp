#include "cxe_format.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <limits>
#include <utility>
#include <vector>

namespace codynex::lr0 {
namespace {

inline constexpr std::uint16_t kFormatVersion = 1U;
inline constexpr std::size_t kHeaderBytes = 28U;
inline constexpr std::size_t kStateEntryBytes = 12U;
inline constexpr std::size_t kFunctionEntryBytes = 12U;
inline constexpr std::size_t kTrailerBytes = 8U;

bool readU8(
    const std::vector<std::uint8_t>& data,
    std::size_t& offset,
    std::uint8_t& out
) {
    if (offset >= data.size()) {
        return false;
    }

    out = data[offset++];
    return true;
}

bool readU16(
    const std::vector<std::uint8_t>& data,
    std::size_t& offset,
    std::uint16_t& out
) {
    if (offset + 2U > data.size()) {
        return false;
    }

    out =
        static_cast<std::uint16_t>(data[offset]) |
        static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(data[offset + 1U]) << 8U
        );

    offset += 2U;
    return true;
}

bool readU32(
    const std::vector<std::uint8_t>& data,
    std::size_t& offset,
    std::uint32_t& out
) {
    if (offset + 4U > data.size()) {
        return false;
    }

    out = 0U;

    for (unsigned shift = 0U; shift < 32U; shift += 8U) {
        out |=
            static_cast<std::uint32_t>(data[offset++])
            << shift;
    }

    return true;
}

bool readU64(
    const std::vector<std::uint8_t>& data,
    std::size_t& offset,
    std::uint64_t& out
) {
    if (offset + 8U > data.size()) {
        return false;
    }

    out = 0U;

    for (unsigned shift = 0U; shift < 64U; shift += 8U) {
        out |=
            static_cast<std::uint64_t>(data[offset++])
            << shift;
    }

    return true;
}

bool readI64(
    const std::vector<std::uint8_t>& data,
    std::size_t& offset,
    std::int64_t& out
) {
    std::uint64_t raw = 0U;

    if (!readU64(data, offset, raw)) {
        return false;
    }

    static_assert(
        sizeof(raw) == sizeof(out),
        "CXE1 requires 64-bit integer storage"
    );

    std::memcpy(&out, &raw, sizeof(out));
    return true;
}

void appendFingerprintByte(
    std::uint64_t& hash,
    std::uint8_t value
) {
    hash ^= static_cast<std::uint64_t>(value);
    hash *= 1099511628211ULL;
}

void appendFingerprintU16(
    std::uint64_t& hash,
    std::uint16_t value
) {
    appendFingerprintByte(
        hash,
        static_cast<std::uint8_t>(value & 0xffU)
    );
    appendFingerprintByte(
        hash,
        static_cast<std::uint8_t>((value >> 8U) & 0xffU)
    );
}

bool validateFunctionCode(
    const std::vector<std::uint8_t>& code,
    const std::vector<StateDecl>& states,
    FunctionDecl& function,
    std::string& reason
) {
    const std::size_t begin =
        static_cast<std::size_t>(function.codeOffset);

    const std::size_t length =
        static_cast<std::size_t>(function.codeLength);

    if (
        length == 0U ||
        begin > code.size() ||
        length > code.size() - begin
    ) {
        reason = "function-code-range-invalid";
        return false;
    }

    const std::size_t end = begin + length;
    std::size_t pc = begin;
    std::size_t stackDepth = 0U;
    std::size_t peakStack = 0U;
    std::uint64_t instructionCount = 0U;
    bool endedWithReturn = false;

    while (pc < end) {
        if (instructionCount >= kMaxInstructionsPerCall) {
            reason = "function-instruction-limit-exceeded";
            return false;
        }

        const std::uint8_t rawOpcode = code[pc++];
        ++instructionCount;

        switch (static_cast<Opcode>(rawOpcode)) {
            case Opcode::ConstI64: {
                if (pc + 8U > end) {
                    reason = "const-i64-truncated";
                    return false;
                }

                pc += 8U;
                ++stackDepth;
                break;
            }

            case Opcode::LoadState: {
                if (pc + 2U > end) {
                    reason = "load-state-truncated";
                    return false;
                }

                const std::uint16_t stateId =
                    static_cast<std::uint16_t>(code[pc]) |
                    static_cast<std::uint16_t>(
                        static_cast<std::uint16_t>(code[pc + 1U]) << 8U
                    );

                pc += 2U;

                if (
                    static_cast<std::size_t>(stateId) >=
                    states.size()
                ) {
                    reason = "load-state-id-invalid";
                    return false;
                }

                ++stackDepth;
                break;
            }

            case Opcode::StoreState: {
                if (pc + 2U > end) {
                    reason = "store-state-truncated";
                    return false;
                }

                const std::uint16_t stateId =
                    static_cast<std::uint16_t>(code[pc]) |
                    static_cast<std::uint16_t>(
                        static_cast<std::uint16_t>(code[pc + 1U]) << 8U
                    );

                pc += 2U;

                if (
                    static_cast<std::size_t>(stateId) >=
                    states.size()
                ) {
                    reason = "store-state-id-invalid";
                    return false;
                }

                if (stackDepth < 1U) {
                    reason = "store-state-stack-underflow";
                    return false;
                }

                --stackDepth;
                break;
            }

            case Opcode::AddI64: {
                if (stackDepth < 2U) {
                    reason = "add-i64-stack-underflow";
                    return false;
                }

                --stackDepth;
                break;
            }

            case Opcode::Return: {
                if (pc != end) {
                    reason = "return-not-final";
                    return false;
                }

                if (stackDepth > 1U) {
                    reason = "return-stack-not-balanced";
                    return false;
                }

                endedWithReturn = true;
                break;
            }

            default:
                reason = "unknown-opcode";
                return false;
        }

        peakStack = std::max(peakStack, stackDepth);

        if (peakStack > kMaxStackEntries) {
            reason = "function-stack-limit-exceeded";
            return false;
        }
    }

    if (!endedWithReturn) {
        reason = "function-missing-return";
        return false;
    }

    function.validatedPeakStack = peakStack;
    function.validatedInstructionCount = instructionCount;
    return true;
}

}  // namespace

std::uint64_t fnv1a64(
    const std::uint8_t* data,
    std::size_t size
) {
    std::uint64_t hash = 1469598103934665603ULL;

    if (data == nullptr && size != 0U) {
        return 0U;
    }

    for (std::size_t i = 0U; i < size; ++i) {
        hash ^= static_cast<std::uint64_t>(data[i]);
        hash *= 1099511628211ULL;
    }

    return hash;
}

std::uint64_t computePersistentSchemaFingerprint(
    const std::vector<StateDecl>& states
) {
    std::uint64_t hash = 1469598103934665603ULL;

    for (const StateDecl& state : states) {
        if (!state.persistent) {
            continue;
        }

        appendFingerprintU16(hash, state.id);
        appendFingerprintByte(
            hash,
            static_cast<std::uint8_t>(state.type)
        );
        appendFingerprintByte(hash, 0x01U);
    }

    return hash;
}

DecodeResult decodeAndValidateExecutable(
    const std::vector<std::uint8_t>& bytes
) {
    DecodeResult result;

    if (bytes.size() > kMaxExecutableBytes) {
        result.reason = "executable-too-large";
        return result;
    }

    if (bytes.size() < kHeaderBytes + kTrailerBytes) {
        result.reason = "executable-too-small";
        return result;
    }

    if (
        bytes[0] != 'C' ||
        bytes[1] != 'X' ||
        bytes[2] != 'E' ||
        bytes[3] != '1'
    ) {
        result.reason = "magic-mismatch";
        return result;
    }

    const std::size_t trailerOffset =
        bytes.size() - kTrailerBytes;

    std::size_t trailerReader = trailerOffset;
    std::uint64_t storedIntegrity = 0U;

    if (!readU64(bytes, trailerReader, storedIntegrity)) {
        result.reason = "integrity-trailer-invalid";
        return result;
    }

    const std::uint64_t computedIntegrity =
        fnv1a64(bytes.data(), trailerOffset);

    if (computedIntegrity != storedIntegrity) {
        result.reason = "integrity-mismatch";
        return result;
    }

    std::size_t offset = 4U;

    std::uint16_t version = 0U;
    std::uint16_t flags = 0U;
    std::uint16_t stateCount = 0U;
    std::uint16_t functionCount = 0U;
    std::uint32_t codeBytes = 0U;
    std::uint32_t declaredTotalBytes = 0U;
    std::uint64_t declaredSchemaFingerprint = 0U;

    if (
        !readU16(bytes, offset, version) ||
        !readU16(bytes, offset, flags) ||
        !readU16(bytes, offset, stateCount) ||
        !readU16(bytes, offset, functionCount) ||
        !readU32(bytes, offset, codeBytes) ||
        !readU32(bytes, offset, declaredTotalBytes) ||
        !readU64(bytes, offset, declaredSchemaFingerprint)
    ) {
        result.reason = "header-truncated";
        return result;
    }

    if (version != kFormatVersion) {
        result.reason = "unsupported-version";
        return result;
    }

    if (flags != 0U) {
        result.reason = "header-flags-invalid";
        return result;
    }

    if (stateCount > kMaxStates) {
        result.reason = "state-count-limit-exceeded";
        return result;
    }

    if (functionCount > kMaxFunctions) {
        result.reason = "function-count-limit-exceeded";
        return result;
    }

    if (codeBytes > kMaxCodeBytes) {
        result.reason = "code-size-limit-exceeded";
        return result;
    }

    const std::uint64_t expectedTotal64 =
        static_cast<std::uint64_t>(kHeaderBytes) +
        static_cast<std::uint64_t>(stateCount) *
            static_cast<std::uint64_t>(kStateEntryBytes) +
        static_cast<std::uint64_t>(functionCount) *
            static_cast<std::uint64_t>(kFunctionEntryBytes) +
        static_cast<std::uint64_t>(codeBytes) +
        static_cast<std::uint64_t>(kTrailerBytes);

    if (
        expectedTotal64 >
        static_cast<std::uint64_t>(kMaxExecutableBytes)
    ) {
        result.reason = "declared-size-limit-exceeded";
        return result;
    }

    const std::size_t expectedTotal =
        static_cast<std::size_t>(expectedTotal64);

    if (
        static_cast<std::size_t>(declaredTotalBytes) !=
            expectedTotal ||
        bytes.size() != expectedTotal
    ) {
        result.reason = "declared-size-mismatch";
        return result;
    }

    ProgramImage program;
    program.states.reserve(stateCount);
    program.functions.reserve(functionCount);

    for (std::uint16_t i = 0U; i < stateCount; ++i) {
        std::uint16_t id = 0U;
        std::uint8_t type = 0U;
        std::uint8_t stateFlags = 0U;
        std::int64_t initial = 0;

        if (
            !readU16(bytes, offset, id) ||
            !readU8(bytes, offset, type) ||
            !readU8(bytes, offset, stateFlags) ||
            !readI64(bytes, offset, initial)
        ) {
            result.reason = "state-table-truncated";
            return result;
        }

        if (id != i) {
            result.reason = "state-id-not-dense";
            return result;
        }

        if (type != static_cast<std::uint8_t>(ValueType::I64)) {
            result.reason = "state-type-unknown";
            return result;
        }

        if ((stateFlags & ~0x01U) != 0U) {
            result.reason = "state-flags-invalid";
            return result;
        }

        StateDecl state;
        state.id = id;
        state.type = ValueType::I64;
        state.persistent = (stateFlags & 0x01U) != 0U;
        state.initialI64 = initial;
        program.states.push_back(state);
    }

    const std::uint64_t computedSchemaFingerprint =
        computePersistentSchemaFingerprint(program.states);

    if (
        computedSchemaFingerprint !=
        declaredSchemaFingerprint
    ) {
        result.reason = "schema-fingerprint-mismatch";
        return result;
    }

    program.schemaFingerprint = computedSchemaFingerprint;

    for (std::uint16_t i = 0U; i < functionCount; ++i) {
        std::uint16_t id = 0U;
        std::uint16_t functionFlags = 0U;
        std::uint32_t codeOffset = 0U;
        std::uint32_t codeLength = 0U;

        if (
            !readU16(bytes, offset, id) ||
            !readU16(bytes, offset, functionFlags) ||
            !readU32(bytes, offset, codeOffset) ||
            !readU32(bytes, offset, codeLength)
        ) {
            result.reason = "function-table-truncated";
            return result;
        }

        if (id != i) {
            result.reason = "function-id-not-dense";
            return result;
        }

        if (functionFlags != 0U) {
            result.reason = "function-flags-invalid";
            return result;
        }

        FunctionDecl function;
        function.id = id;
        function.codeOffset = codeOffset;
        function.codeLength = codeLength;
        program.functions.push_back(function);
    }

    const std::size_t codeStart = offset;
    const std::size_t codeLength =
        static_cast<std::size_t>(codeBytes);

    if (
        codeStart > trailerOffset ||
        codeLength != trailerOffset - codeStart
    ) {
        result.reason = "code-section-size-mismatch";
        return result;
    }

    program.code.assign(
        bytes.begin() + static_cast<std::ptrdiff_t>(codeStart),
        bytes.begin() + static_cast<std::ptrdiff_t>(trailerOffset)
    );

    for (std::size_t i = 0U; i < program.functions.size(); ++i) {
        const FunctionDecl& left = program.functions[i];

        const std::uint64_t leftBegin = left.codeOffset;
        const std::uint64_t leftEnd =
            leftBegin + left.codeLength;

        if (
            left.codeLength == 0U ||
            leftEnd >
                static_cast<std::uint64_t>(
                    program.code.size()
                )
        ) {
            result.reason = "function-code-range-invalid";
            return result;
        }

        for (
            std::size_t j = i + 1U;
            j < program.functions.size();
            ++j
        ) {
            const FunctionDecl& right = program.functions[j];

            const std::uint64_t rightBegin = right.codeOffset;
            const std::uint64_t rightEnd =
                rightBegin + right.codeLength;

            const bool overlaps =
                leftBegin < rightEnd &&
                rightBegin < leftEnd;

            if (overlaps) {
                result.reason = "function-ranges-overlap";
                return result;
            }
        }
    }

    for (FunctionDecl& function : program.functions) {
        std::string reason;

        if (
            !validateFunctionCode(
                program.code,
                program.states,
                function,
                reason
            )
        ) {
            result.reason = reason;
            return result;
        }
    }

    program.executableHash =
        fnv1a64(bytes.data(), bytes.size());
    program.encodedBytes = bytes.size();

    result.ok = true;
    result.program = std::move(program);
    result.reason = "ok";
    return result;
}

std::vector<std::uint8_t> readExecutableFile(
    const std::string& path
) {
    std::ifstream stream(path, std::ios::binary);

    if (!stream) {
        return {};
    }

    stream.seekg(0, std::ios::end);
    const std::streamoff length = stream.tellg();

    if (
        length <= 0 ||
        length >
            static_cast<std::streamoff>(
                kMaxExecutableBytes
            )
    ) {
        return {};
    }

    stream.seekg(0, std::ios::beg);

    std::vector<std::uint8_t> bytes(
        static_cast<std::size_t>(length)
    );

    stream.read(
        reinterpret_cast<char*>(bytes.data()),
        length
    );

    if (!stream) {
        return {};
    }

    return bytes;
}

}  // namespace codynex::lr0
