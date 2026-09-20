#include "cxe_builder.h"

#include <cstring>
#include <fstream>

namespace codynex::lr0::hosttest {
namespace {

void appendU8(
    std::vector<std::uint8_t>& out,
    std::uint8_t value
) {
    out.push_back(value);
}

void appendU16(
    std::vector<std::uint8_t>& out,
    std::uint16_t value
) {
    out.push_back(
        static_cast<std::uint8_t>(value & 0xffU)
    );
    out.push_back(
        static_cast<std::uint8_t>(
            (value >> 8U) & 0xffU
        )
    );
}

void appendU32(
    std::vector<std::uint8_t>& out,
    std::uint32_t value
) {
    for (unsigned shift = 0U; shift < 32U; shift += 8U) {
        out.push_back(
            static_cast<std::uint8_t>(
                (value >> shift) & 0xffU
            )
        );
    }
}

void appendU64(
    std::vector<std::uint8_t>& out,
    std::uint64_t value
) {
    for (unsigned shift = 0U; shift < 64U; shift += 8U) {
        out.push_back(
            static_cast<std::uint8_t>(
                (value >> shift) & 0xffULL
            )
        );
    }
}

void appendI64(
    std::vector<std::uint8_t>& out,
    std::int64_t value
) {
    std::uint64_t raw = 0U;

    static_assert(
        sizeof(raw) == sizeof(value),
        "CXE1 requires 64-bit integer storage"
    );

    std::memcpy(&raw, &value, sizeof(raw));
    appendU64(out, raw);
}

void writeU64At(
    std::vector<std::uint8_t>& out,
    std::size_t offset,
    std::uint64_t value
) {
    if (offset + 8U > out.size()) {
        return;
    }

    for (unsigned shift = 0U; shift < 64U; shift += 8U) {
        out[offset++] =
            static_cast<std::uint8_t>(
                (value >> shift) & 0xffULL
            );
    }
}

}  // namespace

FunctionCode& FunctionCode::constI64(
    std::int64_t value
) {
    appendU8(
        bytes_,
        static_cast<std::uint8_t>(Opcode::ConstI64)
    );
    appendI64(bytes_, value);
    return *this;
}

FunctionCode& FunctionCode::loadState(
    std::uint16_t stateId
) {
    appendU8(
        bytes_,
        static_cast<std::uint8_t>(Opcode::LoadState)
    );
    appendU16(bytes_, stateId);
    return *this;
}

FunctionCode& FunctionCode::storeState(
    std::uint16_t stateId
) {
    appendU8(
        bytes_,
        static_cast<std::uint8_t>(Opcode::StoreState)
    );
    appendU16(bytes_, stateId);
    return *this;
}

FunctionCode& FunctionCode::addI64() {
    appendU8(
        bytes_,
        static_cast<std::uint8_t>(Opcode::AddI64)
    );
    return *this;
}

FunctionCode& FunctionCode::returnValue() {
    appendU8(
        bytes_,
        static_cast<std::uint8_t>(Opcode::Return)
    );
    return *this;
}

const std::vector<std::uint8_t>&
FunctionCode::bytes() const {
    return bytes_;
}

std::vector<std::uint8_t> buildExecutable(
    const std::vector<HostStateDecl>& states,
    const std::vector<FunctionCode>& functions
) {
    if (
        states.size() > kMaxStates ||
        functions.size() > kMaxFunctions
    ) {
        return {};
    }

    std::size_t codeBytes = 0U;

    for (const FunctionCode& function : functions) {
        codeBytes += function.bytes().size();
    }

    if (codeBytes > kMaxCodeBytes) {
        return {};
    }

    std::vector<StateDecl> schemaStates;
    schemaStates.reserve(states.size());

    for (
        std::size_t i = 0U;
        i < states.size();
        ++i
    ) {
        StateDecl state;
        state.id = static_cast<std::uint16_t>(i);
        state.type = ValueType::I64;
        state.persistent = states[i].persistent;
        state.initialI64 = states[i].initialI64;
        schemaStates.push_back(state);
    }

    constexpr std::size_t kHeaderBytes = 28U;
    constexpr std::size_t kStateEntryBytes = 12U;
    constexpr std::size_t kFunctionEntryBytes = 12U;
    constexpr std::size_t kTrailerBytes = 8U;

    const std::size_t totalBytes =
        kHeaderBytes +
        states.size() * kStateEntryBytes +
        functions.size() * kFunctionEntryBytes +
        codeBytes +
        kTrailerBytes;

    if (
        totalBytes > kMaxExecutableBytes ||
        totalBytes >
            static_cast<std::size_t>(
                UINT32_MAX
            )
    ) {
        return {};
    }

    std::vector<std::uint8_t> out;
    out.reserve(totalBytes);

    out.push_back('C');
    out.push_back('X');
    out.push_back('E');
    out.push_back('1');

    appendU16(out, 1U);
    appendU16(out, 0U);
    appendU16(
        out,
        static_cast<std::uint16_t>(states.size())
    );
    appendU16(
        out,
        static_cast<std::uint16_t>(functions.size())
    );
    appendU32(
        out,
        static_cast<std::uint32_t>(codeBytes)
    );
    appendU32(
        out,
        static_cast<std::uint32_t>(totalBytes)
    );
    appendU64(
        out,
        computePersistentSchemaFingerprint(schemaStates)
    );

    for (
        std::size_t i = 0U;
        i < states.size();
        ++i
    ) {
        appendU16(
            out,
            static_cast<std::uint16_t>(i)
        );
        appendU8(
            out,
            static_cast<std::uint8_t>(ValueType::I64)
        );
        appendU8(
            out,
            states[i].persistent ? 0x01U : 0x00U
        );
        appendI64(out, states[i].initialI64);
    }

    std::uint32_t codeOffset = 0U;

    for (
        std::size_t i = 0U;
        i < functions.size();
        ++i
    ) {
        const std::size_t length =
            functions[i].bytes().size();

        if (length > static_cast<std::size_t>(UINT32_MAX)) {
            return {};
        }

        appendU16(
            out,
            static_cast<std::uint16_t>(i)
        );
        appendU16(out, 0U);
        appendU32(out, codeOffset);
        appendU32(
            out,
            static_cast<std::uint32_t>(length)
        );

        codeOffset +=
            static_cast<std::uint32_t>(length);
    }

    for (const FunctionCode& function : functions) {
        out.insert(
            out.end(),
            function.bytes().begin(),
            function.bytes().end()
        );
    }

    const std::uint64_t integrity =
        fnv1a64(out.data(), out.size());

    appendU64(out, integrity);
    return out;
}

void refreshIntegrity(
    std::vector<std::uint8_t>& bytes
) {
    if (bytes.size() < 8U) {
        return;
    }

    const std::size_t trailerOffset =
        bytes.size() - 8U;

    const std::uint64_t integrity =
        fnv1a64(bytes.data(), trailerOffset);

    writeU64At(bytes, trailerOffset, integrity);
}

bool writeBinaryFile(
    const std::string& path,
    const std::vector<std::uint8_t>& bytes
) {
    if (bytes.empty()) {
        return false;
    }

    std::ofstream stream(
        path,
        std::ios::binary |
        std::ios::trunc
    );

    if (!stream) {
        return false;
    }

    stream.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size())
    );

    return stream.good();
}

}  // namespace codynex::lr0::hosttest
