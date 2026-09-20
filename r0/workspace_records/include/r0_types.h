#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace codynex::r0::workspace_records {

enum class EvidenceClass : std::uint8_t {
    ExactHash = 1U,
    HeuristicText = 2U,
    MetadataOnly = 3U,
    Ambiguous = 4U,
    Missing = 5U
};

enum class RelationKind : std::uint8_t {
    Unchanged = 1U,
    Renamed = 2U,
    Copied = 3U,
    Rewritten = 4U,
    Added = 5U,
    Deleted = 6U,
    Modified = 7U,
    Unknown = 8U
};

struct FileEvidence {
    std::string path;
    std::uint64_t size = 0U;
    std::string sha256;
    std::optional<std::string> text;
};

struct Relation {
    RelationKind kind = RelationKind::Unknown;
    std::string fromPath;
    std::string toPath;
    EvidenceClass evidence = EvidenceClass::Missing;
    bool exact = false;
    bool complete = false;
    int similarity = -1;
    std::string reason;
};

struct IdentityMetrics {
    std::uint64_t exactLookups = 0U;
    std::uint64_t heuristicComparisons = 0U;
    std::size_t removedCandidates = 0U;
    std::size_t addedCandidates = 0U;
    std::size_t generatedMachineryBytes = 0U;
    std::size_t scratchBytes = 0U;
    bool candidateBoundReached = false;
    bool comparisonBoundReached = false;
};

struct IdentityResult {
    std::vector<Relation> relations;
    IdentityMetrics metrics;
    bool complete = true;
    std::string reason;
};

enum class DiffStrategy : std::uint8_t {
    Exact = 1U,
    Anchored = 2U,
    ReplacementFallback = 3U,
    MetadataOnly = 4U,
    Unknown = 5U
};

enum class EditKind : std::uint8_t {
    Equal = 1U,
    Delete = 2U,
    Insert = 3U
};

struct Edit {
    EditKind kind = EditKind::Equal;
    std::string line;
};

struct DiffMetrics {
    std::uint64_t exactMatrixCells = 0U;
    std::uint32_t maxRecursionDepth = 0U;
    std::size_t representedChangedLines = 0U;
    std::size_t renderedChars = 0U;
    std::size_t generatedMachineryBytes = 0U;
    std::size_t scratchBytes = 0U;
};

struct DiffResult {
    std::vector<Edit> edits;
    DiffStrategy strategy = DiffStrategy::Unknown;
    DiffMetrics metrics;
    bool complete = true;
    bool truncated = false;
    std::string reason;
};

struct ReconstructionLedger {
    std::size_t retainedEvidenceBytes = 0U;
    std::size_t retainedGeneratedMachineryBytes = 0U;
    std::uint64_t evidenceSignatureBefore = 0U;
    std::uint64_t evidenceSignatureAfter = 0U;
    bool evidenceMutated = false;
};

}  // namespace codynex::r0::workspace_records
