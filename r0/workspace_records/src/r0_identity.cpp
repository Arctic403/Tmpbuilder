#include "r0_identity.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace codynex::r0::workspace_records {
namespace {

bool validSha256(const std::string& value) {
    if (value.size() != 64U) {
        return false;
    }

    return std::all_of(
        value.begin(),
        value.end(),
        [](unsigned char ch) {
            return std::isxdigit(ch) != 0;
        }
    );
}

std::size_t evidenceBytesProxy(
    const std::vector<FileEvidence>& rows
) {
    std::size_t bytes =
        rows.size() * sizeof(const FileEvidence*);

    for (const FileEvidence& row : rows) {
        bytes += row.path.size();
        bytes += row.sha256.size();
    }

    return bytes;
}

bool uniquePaths(
    const std::vector<FileEvidence>& rows
) {
    std::set<std::string> paths;

    for (const FileEvidence& row : rows) {
        if (!paths.insert(row.path).second) {
            return false;
        }
    }

    return true;
}

Relation exactRelation(
    RelationKind kind,
    const FileEvidence& from,
    const FileEvidence& to
) {
    Relation relation;
    relation.kind = kind;
    relation.fromPath = from.path;
    relation.toPath = to.path;
    relation.evidence = EvidenceClass::ExactHash;
    relation.exact = true;
    relation.complete = true;
    relation.similarity = 100;
    relation.reason = "sha256-equality";
    return relation;
}

}  // namespace

IdentityResult correlateExactIdentity(
    const std::vector<FileEvidence>& before,
    const std::vector<FileEvidence>& after
) {
    IdentityResult result;
    result.complete = false;

    if (!uniquePaths(before) || !uniquePaths(after)) {
        result.reason = "duplicate-path-evidence";
        return result;
    }

    std::map<std::string, const FileEvidence*> beforeByPath;
    std::map<std::string, const FileEvidence*> afterByPath;

    for (const FileEvidence& row : before) {
        beforeByPath.emplace(row.path, &row);
    }

    for (const FileEvidence& row : after) {
        afterByPath.emplace(row.path, &row);
    }

    std::vector<const FileEvidence*> removed;
    std::vector<const FileEvidence*> added;

    for (const auto& [path, row] : beforeByPath) {
        if (afterByPath.find(path) == afterByPath.end()) {
            removed.push_back(row);
        }
    }

    for (const auto& [path, row] : afterByPath) {
        if (beforeByPath.find(path) == beforeByPath.end()) {
            added.push_back(row);
        }
    }

    result.metrics.removedCandidates = removed.size();
    result.metrics.addedCandidates = added.size();

    std::map<
        std::string,
        std::vector<const FileEvidence*>
    > removedByHash;

    std::map<
        std::string,
        std::vector<const FileEvidence*>
    > addedByHash;

    for (const FileEvidence* row : removed) {
        if (validSha256(row->sha256)) {
            removedByHash[row->sha256].push_back(row);
        }
    }

    for (const FileEvidence* row : added) {
        if (validSha256(row->sha256)) {
            addedByHash[row->sha256].push_back(row);
        }
    }

    std::set<std::string> usedRemoved;
    std::set<std::string> usedAdded;

    for (auto& [hash, sources] : removedByHash) {
        auto targetsIt = addedByHash.find(hash);
        ++result.metrics.exactLookups;

        if (targetsIt == addedByHash.end()) {
            continue;
        }

        auto& targets = targetsIt->second;

        std::sort(
            sources.begin(),
            sources.end(),
            [](const FileEvidence* left, const FileEvidence* right) {
                return left->path < right->path;
            }
        );

        std::sort(
            targets.begin(),
            targets.end(),
            [](const FileEvidence* left, const FileEvidence* right) {
                return left->path < right->path;
            }
        );

        const std::size_t pairCount =
            std::min(sources.size(), targets.size());

        for (std::size_t index = 0U; index < pairCount; ++index) {
            const FileEvidence& source = *sources[index];
            const FileEvidence& target = *targets[index];

            usedRemoved.insert(source.path);
            usedAdded.insert(target.path);

            result.relations.push_back(
                exactRelation(
                    RelationKind::Renamed,
                    source,
                    target
                )
            );
        }
    }

    std::map<
        std::string,
        std::vector<const FileEvidence*>
    > survivingByHash;

    for (const auto& [path, afterRow] : afterByPath) {
        const auto beforeIt = beforeByPath.find(path);

        if (beforeIt == beforeByPath.end()) {
            continue;
        }

        const FileEvidence* beforeRow = beforeIt->second;

        if (
            validSha256(afterRow->sha256) &&
            beforeRow->sha256 == afterRow->sha256
        ) {
            survivingByHash[afterRow->sha256].push_back(afterRow);
        }
    }

    for (auto& [hash, sources] : survivingByHash) {
        (void)hash;

        std::sort(
            sources.begin(),
            sources.end(),
            [](const FileEvidence* left, const FileEvidence* right) {
                return left->path < right->path;
            }
        );
    }

    for (const FileEvidence* target : added) {
        if (usedAdded.find(target->path) != usedAdded.end()) {
            continue;
        }

        if (!validSha256(target->sha256)) {
            continue;
        }

        ++result.metrics.exactLookups;

        const auto sourcesIt =
            survivingByHash.find(target->sha256);

        if (
            sourcesIt == survivingByHash.end() ||
            sourcesIt->second.empty()
        ) {
            continue;
        }

        const FileEvidence* source = sourcesIt->second.front();

        usedAdded.insert(target->path);

        result.relations.push_back(
            exactRelation(
                RelationKind::Copied,
                *source,
                *target
            )
        );
    }

    std::sort(
        result.relations.begin(),
        result.relations.end(),
        [](const Relation& left, const Relation& right) {
            if (left.toPath != right.toPath) {
                return left.toPath < right.toPath;
            }

            if (left.fromPath != right.fromPath) {
                return left.fromPath < right.fromPath;
            }

            return static_cast<std::uint8_t>(left.kind) <
                static_cast<std::uint8_t>(right.kind);
        }
    );

    result.metrics.generatedMachineryBytes =
        evidenceBytesProxy(before) +
        evidenceBytesProxy(after);

    result.complete = true;
    result.reason = "exact-lane-complete";
    return result;
}

}  // namespace codynex::r0::workspace_records
