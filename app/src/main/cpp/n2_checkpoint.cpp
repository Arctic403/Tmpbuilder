#include "n2_checkpoint.h"

#include <cstdio>
#include <fstream>
#include <limits>

namespace codynex::n2 {
namespace {

constexpr std::uint32_t kCheckpointVersion = 1U;
constexpr std::size_t kReplicaBytes = 10U;

std::uint64_t fnv64(
    const std::uint8_t* data,
    std::size_t size
) {
    std::uint64_t hash = 1469598103934665603ULL;

    for (std::size_t i = 0U; i < size; ++i) {
        hash ^= static_cast<std::uint64_t>(data[i]);
        hash *= 1099511628211ULL;
    }

    return hash;
}

std::uint64_t hashText(const char* text) {
    if (text == nullptr) {
        return 0U;
    }

    return fnv64(
        reinterpret_cast<const std::uint8_t*>(text),
        std::char_traits<char>::length(text)
    );
}

void appendU8(
    std::vector<std::uint8_t>& out,
    std::uint8_t value
) {
    out.push_back(value);
}

void appendU32(
    std::vector<std::uint8_t>& out,
    std::uint32_t value
) {
    for (int shift = 0; shift < 32; shift += 8) {
        out.push_back(
            static_cast<std::uint8_t>(
                (value >> static_cast<unsigned>(shift)) & 0xffU
            )
        );
    }
}

void appendI32(
    std::vector<std::uint8_t>& out,
    std::int32_t value
) {
    appendU32(
        out,
        static_cast<std::uint32_t>(value)
    );
}

void appendU64(
    std::vector<std::uint8_t>& out,
    std::uint64_t value
) {
    for (int shift = 0; shift < 64; shift += 8) {
        out.push_back(
            static_cast<std::uint8_t>(
                (value >> static_cast<unsigned>(shift)) & 0xffULL
            )
        );
    }
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

bool readU32(
    const std::vector<std::uint8_t>& data,
    std::size_t& offset,
    std::uint32_t& out
) {
    if (offset + 4U > data.size()) {
        return false;
    }

    out = 0U;

    for (int shift = 0; shift < 32; shift += 8) {
        out |=
            static_cast<std::uint32_t>(data[offset++])
            << static_cast<unsigned>(shift);
    }

    return true;
}

bool readI32(
    const std::vector<std::uint8_t>& data,
    std::size_t& offset,
    std::int32_t& out
) {
    std::uint32_t raw = 0U;

    if (!readU32(data, offset, raw)) {
        return false;
    }

    out = static_cast<std::int32_t>(raw);
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

    for (int shift = 0; shift < 64; shift += 8) {
        out |=
            static_cast<std::uint64_t>(data[offset++])
            << static_cast<unsigned>(shift);
    }

    return true;
}

}  // namespace

std::uint64_t checkpointSchemaHash() {
    return hashText(
        "N2CP:v1:"
        "width:u32;"
        "height:u32;"
        "count:u32;"
        "replica:{version:i32,data:u8,rank:i32,flags:u8};"
        "trailer:fnv64"
    );
}

std::vector<std::uint8_t> encodeCheckpoint(
    const Mesh& mesh,
    CheckpointLedger* ledger
) {
    std::vector<std::uint8_t> bytes;

    const std::size_t expected =
        4U +
        4U +
        8U +
        4U +
        4U +
        4U +
        mesh.size() * kReplicaBytes +
        8U;

    bytes.reserve(expected);

    bytes.push_back('N');
    bytes.push_back('2');
    bytes.push_back('C');
    bytes.push_back('P');

    appendU32(bytes, kCheckpointVersion);
    appendU64(bytes, checkpointSchemaHash());

    appendU32(
        bytes,
        static_cast<std::uint32_t>(mesh.width())
    );

    appendU32(
        bytes,
        static_cast<std::uint32_t>(mesh.height())
    );

    appendU32(
        bytes,
        static_cast<std::uint32_t>(mesh.size())
    );

    for (std::size_t i = 0U; i < mesh.size(); ++i) {
        const Replica& replica = mesh.cell(i);

        appendI32(bytes, replica.version);
        appendU8(bytes, replica.data);
        appendI32(bytes, replica.rank);

        std::uint8_t flags = 0U;

        if (replica.pinned) {
            flags |= 0x01U;
        }

        if (replica.conflict) {
            flags |= 0x02U;
        }

        appendU8(bytes, flags);
    }

    const std::uint64_t integrity =
        fnv64(bytes.data(), bytes.size());

    appendU64(bytes, integrity);

    if (ledger != nullptr) {
        ledger->checkpointBytes = bytes.size();
        ledger->substrateSerializedBytes = bytes.size();
        ledger->generatedMachinerySerializedBytes = 0U;
        ledger->authoritySerializedBytes = 0U;
        ledger->checkpointHash =
            fnv64(bytes.data(), bytes.size());
        ledger->schemaHash = checkpointSchemaHash();
    }

    return bytes;
}

DecodedCheckpoint decodeCheckpoint(
    const std::vector<std::uint8_t>& bytes
) {
    DecodedCheckpoint decoded;

    constexpr std::size_t kHeaderBytes =
        4U + 4U + 8U + 4U + 4U + 4U;

    constexpr std::size_t kTrailerBytes = 8U;

    if (bytes.size() < kHeaderBytes + kTrailerBytes) {
        decoded.reason = "checkpoint-too-small";
        return decoded;
    }

    if (
        bytes[0] != 'N' ||
        bytes[1] != '2' ||
        bytes[2] != 'C' ||
        bytes[3] != 'P'
    ) {
        decoded.reason = "checkpoint-magic-mismatch";
        return decoded;
    }

    const std::size_t trailerOffset =
        bytes.size() - kTrailerBytes;

    std::size_t trailerReader = trailerOffset;
    std::uint64_t storedIntegrity = 0U;

    if (
        !readU64(
            bytes,
            trailerReader,
            storedIntegrity
        )
    ) {
        decoded.reason = "checkpoint-trailer-invalid";
        return decoded;
    }

    const std::uint64_t computedIntegrity =
        fnv64(bytes.data(), trailerOffset);

    if (computedIntegrity != storedIntegrity) {
        decoded.reason = "checkpoint-integrity-failed";
        return decoded;
    }

    std::size_t offset = 4U;

    std::uint32_t version = 0U;
    std::uint64_t schema = 0U;
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::uint32_t count = 0U;

    if (
        !readU32(bytes, offset, version) ||
        !readU64(bytes, offset, schema) ||
        !readU32(bytes, offset, width) ||
        !readU32(bytes, offset, height) ||
        !readU32(bytes, offset, count)
    ) {
        decoded.reason = "checkpoint-header-invalid";
        return decoded;
    }

    if (version != kCheckpointVersion) {
        decoded.reason = "checkpoint-version-unsupported";
        return decoded;
    }

    if (schema != checkpointSchemaHash()) {
        decoded.reason = "checkpoint-schema-mismatch";
        return decoded;
    }

    if (
        width == 0U ||
        height == 0U ||
        width > 4096U ||
        height > 4096U
    ) {
        decoded.reason = "checkpoint-dimensions-invalid";
        return decoded;
    }

    const std::uint64_t product =
        static_cast<std::uint64_t>(width) *
        static_cast<std::uint64_t>(height);

    if (
        product != static_cast<std::uint64_t>(count) ||
        product >
            static_cast<std::uint64_t>(
                std::numeric_limits<std::uint32_t>::max()
            )
    ) {
        decoded.reason = "checkpoint-count-invalid";
        return decoded;
    }

    const std::size_t expectedSize =
        kHeaderBytes +
        static_cast<std::size_t>(count) * kReplicaBytes +
        kTrailerBytes;

    if (bytes.size() != expectedSize) {
        decoded.reason = "checkpoint-size-mismatch";
        return decoded;
    }

    decoded.replicas.reserve(count);

    for (std::uint32_t i = 0U; i < count; ++i) {
        Replica replica;
        std::uint8_t flags = 0U;

        if (
            !readI32(bytes, offset, replica.version) ||
            !readU8(bytes, offset, replica.data) ||
            !readI32(bytes, offset, replica.rank) ||
            !readU8(bytes, offset, flags)
        ) {
            decoded.reason = "checkpoint-replica-invalid";
            decoded.replicas.clear();
            return decoded;
        }

        replica.pinned = (flags & 0x01U) != 0U;
        replica.conflict = (flags & 0x02U) != 0U;

        decoded.replicas.push_back(replica);
    }

    if (offset != trailerOffset) {
        decoded.reason = "checkpoint-payload-mismatch";
        decoded.replicas.clear();
        return decoded;
    }

    decoded.ok = true;
    decoded.width = static_cast<int>(width);
    decoded.height = static_cast<int>(height);
    decoded.checkpointHash =
        fnv64(bytes.data(), bytes.size());
    decoded.reason = "ok";
    return decoded;
}

bool restoreCheckpoint(
    const DecodedCheckpoint& decoded,
    Mesh& mesh
) {
    if (!decoded.ok) {
        return false;
    }

    if (
        decoded.width != mesh.width() ||
        decoded.height != mesh.height() ||
        decoded.replicas.size() != mesh.size()
    ) {
        return false;
    }

    for (
        std::size_t i = 0U;
        i < decoded.replicas.size();
        ++i
    ) {
        mesh.cell(i) = decoded.replicas[i];
    }

    return true;
}

bool writeCheckpointFile(
    const std::string& path,
    const std::vector<std::uint8_t>& bytes
) {
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

std::vector<std::uint8_t> readCheckpointFile(
    const std::string& path
) {
    std::ifstream stream(
        path,
        std::ios::binary
    );

    if (!stream) {
        return {};
    }

    stream.seekg(0, std::ios::end);
    const std::streamoff length = stream.tellg();

    if (length <= 0) {
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

bool removeCheckpointFile(const std::string& path) {
    return std::remove(path.c_str()) == 0;
}

}  // namespace codynex::n2
