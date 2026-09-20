#include "r0_identity.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

using codynex::r0::workspace_records::EvidenceClass;
using codynex::r0::workspace_records::FileEvidence;
using codynex::r0::workspace_records::IdentityResult;
using codynex::r0::workspace_records::Relation;
using codynex::r0::workspace_records::RelationKind;
using codynex::r0::workspace_records::correlateExactIdentity;

struct Summary {
    int runs = 0;
    int passed = 0;
    int falseExact = 0;
    int duplicateRenameConsumption = 0;
};

std::string sha(char value) {
    return std::string(64U, value);
}

FileEvidence file(
    const std::string& path,
    const std::string& digest,
    std::uint64_t size = 16U
) {
    FileEvidence row;
    row.path = path;
    row.size = size;
    row.sha256 = digest;
    return row;
}

bool hasRelation(
    const IdentityResult& result,
    RelationKind kind,
    const std::string& from,
    const std::string& to
) {
    for (const Relation& relation : result.relations) {
        if (
            relation.kind == kind &&
            relation.fromPath == from &&
            relation.toPath == to
        ) {
            return true;
        }
    }

    return false;
}

bool exactRelationsAreGrounded(
    const IdentityResult& result,
    const std::vector<FileEvidence>& before,
    const std::vector<FileEvidence>& after
) {
    for (const Relation& relation : result.relations) {
        if (!relation.exact) {
            continue;
        }

        const FileEvidence* source = nullptr;
        const FileEvidence* target = nullptr;

        for (const FileEvidence& row : before) {
            if (row.path == relation.fromPath) {
                source = &row;
                break;
            }
        }

        for (const FileEvidence& row : after) {
            if (row.path == relation.toPath) {
                target = &row;
                break;
            }
        }

        if (
            source == nullptr ||
            target == nullptr ||
            source->sha256 != target->sha256 ||
            relation.evidence != EvidenceClass::ExactHash ||
            relation.similarity != 100
        ) {
            return false;
        }
    }

    return true;
}

bool renameConsumptionUnique(const IdentityResult& result) {
    std::vector<std::string> sources;
    std::vector<std::string> targets;

    for (const Relation& relation : result.relations) {
        if (relation.kind != RelationKind::Renamed) {
            continue;
        }

        for (const std::string& source : sources) {
            if (source == relation.fromPath) {
                return false;
            }
        }

        for (const std::string& target : targets) {
            if (target == relation.toPath) {
                return false;
            }
        }

        sources.push_back(relation.fromPath);
        targets.push_back(relation.toPath);
    }

    return true;
}

void record(Summary& summary, bool pass) {
    ++summary.runs;

    if (pass) {
        ++summary.passed;
    }
}

}  // namespace

int main() {
    Summary summary;

    {
        const std::vector<FileEvidence> before = {
            file("old.txt", sha('a'))
        };

        const std::vector<FileEvidence> after = {
            file("new.txt", sha('a'))
        };

        const IdentityResult result =
            correlateExactIdentity(before, after);

        record(
            summary,
            result.complete &&
                result.relations.size() == 1U &&
                hasRelation(
                    result,
                    RelationKind::Renamed,
                    "old.txt",
                    "new.txt"
                ) &&
                exactRelationsAreGrounded(
                    result,
                    before,
                    after
                )
        );
    }

    {
        const std::vector<FileEvidence> before = {
            file("keep.txt", sha('b'))
        };

        const std::vector<FileEvidence> after = {
            file("keep.txt", sha('b')),
            file("copy.txt", sha('b'))
        };

        const IdentityResult result =
            correlateExactIdentity(before, after);

        record(
            summary,
            result.complete &&
                result.relations.size() == 1U &&
                hasRelation(
                    result,
                    RelationKind::Copied,
                    "keep.txt",
                    "copy.txt"
                ) &&
                exactRelationsAreGrounded(
                    result,
                    before,
                    after
                )
        );
    }

    {
        const std::vector<FileEvidence> before = {
            file("b-old.txt", sha('c')),
            file("a-old.txt", sha('c'))
        };

        const std::vector<FileEvidence> after = {
            file("d-new.txt", sha('c')),
            file("c-new.txt", sha('c'))
        };

        const IdentityResult result =
            correlateExactIdentity(before, after);

        const bool unique = renameConsumptionUnique(result);

        if (!unique) {
            ++summary.duplicateRenameConsumption;
        }

        record(
            summary,
            result.complete &&
                result.relations.size() == 2U &&
                hasRelation(
                    result,
                    RelationKind::Renamed,
                    "a-old.txt",
                    "c-new.txt"
                ) &&
                hasRelation(
                    result,
                    RelationKind::Renamed,
                    "b-old.txt",
                    "d-new.txt"
                ) &&
                unique
        );
    }

    {
        const std::vector<FileEvidence> before = {
            file("old.bin", "not-a-sha")
        };

        const std::vector<FileEvidence> after = {
            file("new.bin", "not-a-sha")
        };

        const IdentityResult result =
            correlateExactIdentity(before, after);

        record(
            summary,
            result.complete &&
                result.relations.empty()
        );
    }

    {
        const std::vector<FileEvidence> before = {
            file("duplicate.txt", sha('d')),
            file("duplicate.txt", sha('d'))
        };

        const std::vector<FileEvidence> after = {
            file("new.txt", sha('d'))
        };

        const IdentityResult result =
            correlateExactIdentity(before, after);

        record(
            summary,
            !result.complete &&
                result.reason == "duplicate-path-evidence" &&
                result.relations.empty()
        );
    }

    {
        const std::vector<FileEvidence> before = {
            file("old.txt", sha('e'))
        };

        const std::vector<FileEvidence> after = {
            file("new.txt", sha('f'))
        };

        const IdentityResult result =
            correlateExactIdentity(before, after);

        const bool grounded =
            exactRelationsAreGrounded(
                result,
                before,
                after
            );

        if (!grounded) {
            ++summary.falseExact;
        }

        record(
            summary,
            result.complete &&
                result.relations.empty() &&
                grounded
        );
    }

    const bool pass =
        summary.runs == 6 &&
        summary.passed == 6 &&
        summary.falseExact == 0 &&
        summary.duplicateRenameConsumption == 0;

    std::cout
        << "{"
        << "\"pass\":" << (pass ? "true" : "false") << ","
        << "\"scope\":\"r0.1-exact-identity-lane\","
        << "\"tests\":{"
        << "\"runs\":" << summary.runs << ","
        << "\"passed\":" << summary.passed
        << "},"
        << "\"falseExact\":" << summary.falseExact << ","
        << "\"duplicateRenameConsumption\":"
        << summary.duplicateRenameConsumption << ","
        << "\"riftOsMutationExpected\":0"
        << "}"
        << std::endl;

    return pass ? 0 : 1;
}
