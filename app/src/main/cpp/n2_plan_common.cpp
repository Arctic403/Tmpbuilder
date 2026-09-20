#include "n2_plan_common.h"

namespace codynex::n2 {

std::uint64_t hashRecipe(const std::vector<PrimitiveId>& steps) {
    std::uint64_t hash = 1469598103934665603ULL;

    for (const PrimitiveId id : steps) {
        hash ^= static_cast<std::uint64_t>(id);
        hash *= 1099511628211ULL;
    }

    hash ^= static_cast<std::uint64_t>(steps.size());
    hash *= 1099511628211ULL;
    return hash;
}

}  // namespace codynex::n2
