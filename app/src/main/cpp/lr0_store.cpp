#include "lr0_store.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sys/stat.h>
#include <utility>
#include <vector>

namespace codynex::lr0 {
namespace {

inline constexpr std::uint16_t kStoreVersion = 1U;
inline constexpr std::size_t kStateHeaderBytes = 28U;
inline constexpr std::size_t kStateEntryBytes = 12U;
inline constexpr std::size_t kIntegrityBytes = 8U;
inline constexpr std::size_t kRecordBytes = 32U;
inline constexpr std::size_t kMaxStateFileBytes =
    kStateHeaderBytes +
    kMaxStates * kStateEntryBytes +
    kIntegrityBytes;

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
    appendU8(
        out,
        static_cast<std::uint8_t>(value & 0xffU)
    );
    appendU8(
        out,
        static_cast<std::uint8_t>((value >> 8U) & 0xffU)
    );
}

void appendU64(
    std::vector<std::uint8_t>& out,
    std::uint64_t value
) {
    for (unsigned shift = 0U; shift < 64U; shift += 8U) {
        appendU8(
            out,
            static_cast<std::uint8_t>(
                (value >> shift) & 0xffU
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
        "LR0 state storage requires 64-bit integers"
    );
    std::memcpy(&raw, &value, sizeof(raw));
    appendU64(out, raw);
}

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
        "LR0 state storage requires 64-bit integers"
    );
    std::memcpy(&out, &raw, sizeof(out));
    return true;
}

bool ensureDirectory(const std::string& path) {
    struct stat info {};

    if (stat(path.c_str(), &info) == 0) {
        return S_ISDIR(info.st_mode);
    }

    if (mkdir(path.c_str(), 0700) == 0) {
        return true;
    }

    return errno == EEXIST;
}

bool readBoundedFile(
    const std::string& path,
    std::size_t maxBytes,
    std::vector<std::uint8_t>& out
) {
    out.clear();

    std::ifstream stream(path, std::ios::binary);

    if (!stream) {
        return false;
    }

    stream.seekg(0, std::ios::end);
    const std::streamoff length = stream.tellg();

    if (
        length <= 0 ||
        length > static_cast<std::streamoff>(maxBytes)
    ) {
        return false;
    }

    stream.seekg(0, std::ios::beg);

    try {
        out.resize(static_cast<std::size_t>(length));
    } catch (...) {
        out.clear();
        return false;
    }

    stream.read(
        reinterpret_cast<char*>(out.data()),
        length
    );

    if (!stream) {
        out.clear();
        return false;
    }

    return true;
}

bool writeAtomic(
    const std::string& path,
    const std::vector<std::uint8_t>& bytes
) {
    if (bytes.empty()) {
        return false;
    }

    const std::string temporary = path + ".tmp";
    std::remove(temporary.c_str());

    {
        std::ofstream stream(
            temporary,
            std::ios::binary | std::ios::trunc
        );

        if (!stream) {
            return false;
        }

        stream.write(
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size())
        );
        stream.flush();

        if (!stream) {
            stream.close();
            std::remove(temporary.c_str());
            return false;
        }
    }

    if (std::rename(temporary.c_str(), path.c_str()) != 0) {
        std::remove(temporary.c_str());
        return false;
    }

    return true;
}

std::vector<std::uint8_t> encodeState(
    const PersistentStateImage& state
) {
    if (state.entries.size() > kMaxStates) {
        return {};
    }

    std::vector<std::uint8_t> bytes;

    try {
        bytes.reserve(
            kStateHeaderBytes +
            state.entries.size() * kStateEntryBytes +
            kIntegrityBytes
        );
    } catch (...) {
        return {};
    }

    bytes.insert(bytes.end(), {'L', 'R', 'S', '1'});
    appendU16(bytes, kStoreVersion);
    appendU16(bytes, 0U);
    appendU64(bytes, state.programHash);
    appendU64(bytes, state.schemaFingerprint);
    appendU16(
        bytes,
        static_cast<std::uint16_t>(state.entries.size())
    );
    appendU16(bytes, 0U);

    std::uint16_t previousId = 0U;
    bool hasPrevious = false;

    for (const PersistentStateEntry& entry : state.entries) {
        if (
            entry.type != ValueType::I64 ||
            (hasPrevious && entry.id <= previousId)
        ) {
            return {};
        }

        appendU16(bytes, entry.id);
        appendU8(
            bytes,
            static_cast<std::uint8_t>(entry.type)
        );
        appendU8(bytes, 0U);
        appendI64(bytes, entry.value);

        previousId = entry.id;
        hasPrevious = true;
    }

    appendU64(
        bytes,
        fnv1a64(bytes.data(), bytes.size())
    );

    return bytes;
}

bool decodeState(
    const std::vector<std::uint8_t>& bytes,
    PersistentStateImage& out,
    std::string& reason
) {
    out = PersistentStateImage{};

    if (
        bytes.size() < kStateHeaderBytes + kIntegrityBytes ||
        bytes.size() > kMaxStateFileBytes
    ) {
        reason = "state-file-size-invalid";
        return false;
    }

    if (
        bytes[0] != 'L' ||
        bytes[1] != 'R' ||
        bytes[2] != 'S' ||
        bytes[3] != '1'
    ) {
        reason = "state-file-magic-invalid";
        return false;
    }

    const std::size_t trailerOffset =
        bytes.size() - kIntegrityBytes;
    std::size_t trailerReader = trailerOffset;
    std::uint64_t storedIntegrity = 0U;

    if (!readU64(bytes, trailerReader, storedIntegrity)) {
        reason = "state-file-integrity-truncated";
        return false;
    }

    if (
        storedIntegrity !=
        fnv1a64(bytes.data(), trailerOffset)
    ) {
        reason = "state-file-integrity-mismatch";
        return false;
    }

    std::size_t offset = 4U;
    std::uint16_t version = 0U;
    std::uint16_t flags = 0U;
    std::uint16_t count = 0U;
    std::uint16_t reserved = 0U;

    if (
        !readU16(bytes, offset, version) ||
        !readU16(bytes, offset, flags) ||
        !readU64(bytes, offset, out.programHash) ||
        !readU64(bytes, offset, out.schemaFingerprint) ||
        !readU16(bytes, offset, count) ||
        !readU16(bytes, offset, reserved)
    ) {
        reason = "state-file-header-truncated";
        return false;
    }

    if (version != kStoreVersion) {
        reason = "state-file-version-unsupported";
        return false;
    }

    if (flags != 0U || reserved != 0U) {
        reason = "state-file-header-flags-invalid";
        return false;
    }

    if (count > kMaxStates) {
        reason = "state-file-count-limit-exceeded";
        return false;
    }

    const std::size_t expectedBytes =
        kStateHeaderBytes +
        static_cast<std::size_t>(count) * kStateEntryBytes +
        kIntegrityBytes;

    if (bytes.size() != expectedBytes) {
        reason = "state-file-size-mismatch";
        return false;
    }

    try {
        out.entries.reserve(count);
    } catch (...) {
        reason = "state-file-allocation-failed";
        return false;
    }

    std::uint16_t previousId = 0U;
    bool hasPrevious = false;

    for (std::uint16_t i = 0U; i < count; ++i) {
        PersistentStateEntry entry;
        std::uint8_t type = 0U;
        std::uint8_t entryFlags = 0U;

        if (
            !readU16(bytes, offset, entry.id) ||
            !readU8(bytes, offset, type) ||
            !readU8(bytes, offset, entryFlags) ||
            !readI64(bytes, offset, entry.value)
        ) {
            reason = "state-file-entry-truncated";
            return false;
        }

        if (
            type != static_cast<std::uint8_t>(ValueType::I64) ||
            entryFlags != 0U
        ) {
            reason = "state-file-entry-invalid";
            return false;
        }

        if (hasPrevious && entry.id <= previousId) {
            reason = "state-file-entry-order-invalid";
            return false;
        }

        entry.type = ValueType::I64;
        out.entries.push_back(entry);
        previousId = entry.id;
        hasPrevious = true;
    }

    if (offset != trailerOffset) {
        reason = "state-file-trailing-data";
        return false;
    }

    reason = "ok";
    return true;
}

std::vector<std::uint8_t> encodeRecord(
    int activeSlot,
    std::uint64_t programHash,
    std::uint64_t stateHash
) {
    if (activeSlot != 0 && activeSlot != 1) {
        return {};
    }

    std::vector<std::uint8_t> bytes;
    bytes.reserve(kRecordBytes);
    bytes.insert(bytes.end(), {'L', 'R', 'R', '1'});
    appendU16(bytes, kStoreVersion);
    appendU8(bytes, static_cast<std::uint8_t>(activeSlot));
    appendU8(bytes, 0U);
    appendU64(bytes, programHash);
    appendU64(bytes, stateHash);
    appendU64(
        bytes,
        fnv1a64(bytes.data(), bytes.size())
    );
    return bytes;
}

bool decodeRecord(
    const std::vector<std::uint8_t>& bytes,
    int& activeSlot,
    std::uint64_t& programHash,
    std::uint64_t& stateHash
) {
    activeSlot = -1;
    programHash = 0U;
    stateHash = 0U;

    if (
        bytes.size() != kRecordBytes ||
        bytes[0] != 'L' ||
        bytes[1] != 'R' ||
        bytes[2] != 'R' ||
        bytes[3] != '1'
    ) {
        return false;
    }

    std::size_t offset = 4U;
    std::uint16_t version = 0U;
    std::uint8_t slot = 0U;
    std::uint8_t flags = 0U;
    std::uint64_t storedIntegrity = 0U;

    if (
        !readU16(bytes, offset, version) ||
        !readU8(bytes, offset, slot) ||
        !readU8(bytes, offset, flags) ||
        !readU64(bytes, offset, programHash) ||
        !readU64(bytes, offset, stateHash) ||
        !readU64(bytes, offset, storedIntegrity)
    ) {
        return false;
    }

    if (
        version != kStoreVersion ||
        flags != 0U ||
        slot > 1U
    ) {
        return false;
    }

    if (
        storedIntegrity !=
        fnv1a64(bytes.data(), kRecordBytes - kIntegrityBytes)
    ) {
        return false;
    }

    activeSlot = static_cast<int>(slot);
    return true;
}

struct SlotRecovery {
    bool ok = false;
    std::uint64_t programHash = 0U;
    std::uint64_t stateHash = 0U;
    LiveRuntime runtime;
    std::vector<std::uint8_t> programBytes;
    std::string reason;
};

SlotRecovery recoverSlot(
    const std::string& programPath,
    const std::string& statePath,
    std::uint64_t expectedProgramHash,
    std::uint64_t expectedStateHash,
    bool enforceExpectedHashes
) {
    SlotRecovery result;
    std::vector<std::uint8_t> programBytes;
    std::vector<std::uint8_t> stateBytes;

    if (
        !readBoundedFile(
            programPath,
            kMaxExecutableBytes,
            programBytes
        )
    ) {
        result.reason = "recovery-program-read-failed";
        return result;
    }

    if (
        !readBoundedFile(
            statePath,
            kMaxStateFileBytes,
            stateBytes
        )
    ) {
        result.reason = "recovery-state-read-failed";
        return result;
    }

    result.programHash =
        fnv1a64(programBytes.data(), programBytes.size());
    result.stateHash =
        fnv1a64(stateBytes.data(), stateBytes.size());

    if (
        enforceExpectedHashes &&
        (
            result.programHash != expectedProgramHash ||
            result.stateHash != expectedStateHash
        )
    ) {
        result.reason = "recovery-record-hash-mismatch";
        return result;
    }

    PersistentStateImage state;
    std::string stateReason;

    if (!decodeState(stateBytes, state, stateReason)) {
        result.reason = stateReason;
        return result;
    }

    if (state.programHash != result.programHash) {
        result.reason = "recovery-state-program-hash-mismatch";
        return result;
    }

    LiveRuntime candidate;
    const ActivationResult activation =
        candidate.activateBytes(programBytes);

    if (!activation.ok) {
        result.reason =
            "recovery-program-invalid:" + activation.reason;
        return result;
    }

    std::string restoreReason;

    if (!candidate.restorePersistentState(state, restoreReason)) {
        result.reason =
            "recovery-state-restore-failed:" + restoreReason;
        return result;
    }

    result.ok = true;
    result.runtime = std::move(candidate);
    result.programBytes = std::move(programBytes);
    result.reason = "ok";
    return result;
}

}  // namespace

RecoveryStore::RecoveryStore(
    std::string rootDirectory
) :
    rootDirectory_(std::move(rootDirectory)) {}

StoreResult RecoveryStore::commit(
    const std::vector<std::uint8_t>& programBytes,
    const PersistentStateImage& state
) {
    StoreResult result;

    if (!ensureDirectory(rootDirectory_)) {
        result.reason = "store-directory-unavailable";
        return result;
    }

    if (
        programBytes.empty() ||
        programBytes.size() > kMaxExecutableBytes
    ) {
        result.reason = "store-program-size-invalid";
        return result;
    }

    const DecodeResult decoded =
        decodeAndValidateExecutable(programBytes);

    if (!decoded.ok) {
        result.reason =
            "store-program-invalid:" + decoded.reason;
        return result;
    }

    const std::uint64_t programHash =
        fnv1a64(programBytes.data(), programBytes.size());

    if (
        state.programHash != programHash ||
        state.schemaFingerprint !=
            decoded.program.schemaFingerprint
    ) {
        result.reason = "store-state-identity-mismatch";
        return result;
    }

    const std::vector<std::uint8_t> stateBytes =
        encodeState(state);

    if (stateBytes.empty()) {
        result.reason = "store-state-encode-failed";
        return result;
    }

    int currentSlot = -1;
    std::uint64_t ignoredProgramHash = 0U;
    std::uint64_t ignoredStateHash = 0U;
    std::vector<std::uint8_t> recordBytes;

    if (
        readBoundedFile(
            recordPath(),
            kRecordBytes,
            recordBytes
        )
    ) {
        decodeRecord(
            recordBytes,
            currentSlot,
            ignoredProgramHash,
            ignoredStateHash
        );
    }

    const int targetSlot =
        currentSlot == 0
            ? 1
            : 0;

    if (
        !writeAtomic(
            slotProgramPath(targetSlot),
            programBytes
        )
    ) {
        result.reason = "store-program-write-failed";
        return result;
    }

    if (
        !writeAtomic(
            slotStatePath(targetSlot),
            stateBytes
        )
    ) {
        result.reason = "store-state-write-failed";
        return result;
    }

    const std::uint64_t stateHash =
        fnv1a64(stateBytes.data(), stateBytes.size());

    const std::vector<std::uint8_t> nextRecord =
        encodeRecord(
            targetSlot,
            programHash,
            stateHash
        );

    if (
        nextRecord.empty() ||
        !writeAtomic(recordPath(), nextRecord)
    ) {
        result.reason = "store-record-write-failed";
        return result;
    }

    result.ok = true;
    result.activeSlot = targetSlot;
    result.programHash = programHash;
    result.reason = "committed";
    return result;
}

StoreResult RecoveryStore::recover(
    LiveRuntime& runtime,
    std::vector<std::uint8_t>& activeProgramBytes
) {
    StoreResult result;

    if (!ensureDirectory(rootDirectory_)) {
        result.reason = "store-directory-unavailable";
        return result;
    }

    std::vector<std::uint8_t> recordBytes;
    int recordedSlot = -1;
    std::uint64_t recordedProgramHash = 0U;
    std::uint64_t recordedStateHash = 0U;

    const bool hasValidRecord =
        readBoundedFile(
            recordPath(),
            kRecordBytes,
            recordBytes
        ) &&
        decodeRecord(
            recordBytes,
            recordedSlot,
            recordedProgramHash,
            recordedStateHash
        );

    if (hasValidRecord) {
        SlotRecovery recorded = recoverSlot(
            slotProgramPath(recordedSlot),
            slotStatePath(recordedSlot),
            recordedProgramHash,
            recordedStateHash,
            true
        );

        if (recorded.ok) {
            runtime = std::move(recorded.runtime);
            activeProgramBytes =
                std::move(recorded.programBytes);
            result.ok = true;
            result.recovered = true;
            result.activeSlot = recordedSlot;
            result.programHash = recorded.programHash;
            result.reason = "recovered-recorded-slot";
            return result;
        }

        const int fallbackSlot =
            recordedSlot == 0
                ? 1
                : 0;

        SlotRecovery fallback = recoverSlot(
            slotProgramPath(fallbackSlot),
            slotStatePath(fallbackSlot),
            0U,
            0U,
            false
        );

        if (fallback.ok) {
            const std::vector<std::uint8_t> repairedRecord =
                encodeRecord(
                    fallbackSlot,
                    fallback.programHash,
                    fallback.stateHash
                );

            if (
                repairedRecord.empty() ||
                !writeAtomic(recordPath(), repairedRecord)
            ) {
                result.reason =
                    "recovery-fallback-record-repair-failed";
                return result;
            }

            runtime = std::move(fallback.runtime);
            activeProgramBytes =
                std::move(fallback.programBytes);
            result.ok = true;
            result.recovered = true;
            result.fallbackUsed = true;
            result.activeSlot = fallbackSlot;
            result.programHash = fallback.programHash;
            result.reason = "recovered-fallback-slot";
            return result;
        }

        result.reason = "recovery-no-valid-slot";
        return result;
    }

    for (int slot = 0; slot <= 1; ++slot) {
        SlotRecovery candidate = recoverSlot(
            slotProgramPath(slot),
            slotStatePath(slot),
            0U,
            0U,
            false
        );

        if (!candidate.ok) {
            continue;
        }

        const std::vector<std::uint8_t> repairedRecord =
            encodeRecord(
                slot,
                candidate.programHash,
                candidate.stateHash
            );

        if (
            repairedRecord.empty() ||
            !writeAtomic(recordPath(), repairedRecord)
        ) {
            result.reason =
                "recovery-record-create-failed";
            return result;
        }

        runtime = std::move(candidate.runtime);
        activeProgramBytes =
            std::move(candidate.programBytes);
        result.ok = true;
        result.recovered = true;
        result.fallbackUsed = true;
        result.activeSlot = slot;
        result.programHash = candidate.programHash;
        result.reason = "recovered-discovered-slot";
        return result;
    }

    result.reason = "recovery-empty";
    return result;
}

std::string RecoveryStore::slotProgramPath(int slot) const {
    return
        rootDirectory_ +
        (slot == 0 ? "/slot0.cxe" : "/slot1.cxe");
}

std::string RecoveryStore::slotStatePath(int slot) const {
    return
        rootDirectory_ +
        (slot == 0 ? "/slot0.state" : "/slot1.state");
}

std::string RecoveryStore::recordPath() const {
    return rootDirectory_ + "/recovery.rec";
}

}  // namespace codynex::lr0
