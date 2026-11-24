#include "service.hpp"

#include <set>

namespace hi {
namespace {
void review_vector(const Json& vector) {
    if (!vector.is_array() || vector.size() != Vector{}.size())
        throw InputError("a vector requires five axes");
    for (const auto& axis : vector) {
        if (!axis.is_number() || std::abs(axis.get<double>()) > 1 || !std::isfinite(axis.get<double>()))
            throw InputError("invalid vector coefficient");
    }
}
}

Json review_patterns(const Json& document) {
    check_key(required_string(document, "version"));
    const auto& patterns = document.at("patterns");
    if (!patterns.is_array() || patterns.empty() || patterns.size() > 256)
        throw InputError("expected a nonempty patterns array");
    std::set<std::string> ids;
    for (const auto& pattern : patterns) {
        const auto id = required_string(pattern, "id");
        check_key(id);
        if (!ids.insert(id).second) throw InputError("duplicate script pattern");
        for (const auto* key : {"label", "healthy", "stress"}) {
            if (required_string(pattern, key).size() > 1200) throw InputError("pattern text too long");
        }
        if (operation_for(required_string(pattern, "stress_quadrant")).empty())
            throw InputError("unknown stress quadrant");
        review_vector(pattern.at("base"));
        const auto& reactions = pattern.at("reactions");
        if (!reactions.is_object()) throw InputError("expected a reaction table");
        for (const auto& [key, value] : reactions.items()) {
            if (key != "proposal" && key != "agreement" && key != "risk" && key != "success"
                && key != "precedent" && key != "condition") throw InputError("unknown reaction feature");
            review_vector(value);
        }
    }
    return document;
}

Json review_roles(const Json& matrix) {
    const auto& vectors = matrix.at("stance");
    if (!vectors.is_object() || !vectors.contains("observe") || vectors.size() > 64)
        throw InputError("role matrix requires an observe role");
    for (const auto& [role, vector] : vectors.items()) {
        check_key(role);
        review_vector(vector);
    }
    for (const auto* group : {"situation", "operation"}) {
        const auto& rows = matrix.at(group);
        if (!rows.is_object()) throw InputError("expected role matrix rows");
        for (const auto& [name, row] : rows.items()) {
            check_key(name);
            if (!row.is_object()) throw InputError("expected role matrix row");
            for (const auto& [role, value] : row.items()) {
                if (!vectors.contains(role) || !value.is_number() || !std::isfinite(value.get<double>())
                    || std::abs(value.get<double>()) > 10) throw InputError("invalid role weight");
            }
        }
    }
    return matrix;
}
} // namespace hi
