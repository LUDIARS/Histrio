#include "service.hpp"

#include <fstream>
#include <iostream>
#include <syncstream>

#include <lorica/byte_view.hpp>
#include <lorica/checked_size.hpp>

namespace hi {
namespace {
constexpr lorica::budget_limits document_limits{max_body * 4, 32768, 32769, 32};
}

Json read_json(std::string_view text, lorica::input_budget& budget) {
    const auto size = lorica::checked_multiply(text.size(), sizeof(char));
    const auto view = size ? lorica::byte_view::make(
        reinterpret_cast<const std::byte*>(text.data()), *size, max_body) : std::nullopt;
    if (!view || !view->subview(0, *size)) throw InputError("input exceeds byte limit");
    try {
        return lapilli::json::parse(text, budget);
    } catch (const lapilli::json::JsonError&) {
        throw InputError("invalid or oversized JSON");
    }
}

Json read_json(std::string_view text) {
    lorica::input_budget budget(document_limits);
    return read_json(text, budget);
}

Json read_file(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw InputError("cannot open JSON file");
    std::string text;
    std::array<char, 8192> chunk{};
    while (stream.read(chunk.data(), chunk.size()) || stream.gcount() > 0) {
        const auto size = static_cast<std::size_t>(stream.gcount());
        const auto total = lorica::checked_add(text.size(), size);
        if (!total || *total > max_body) throw InputError("JSON file exceeds byte limit");
        text.append(chunk.data(), size);
    }
    if (!stream.eof() || stream.bad()) throw InputError("cannot read JSON file");
    return read_json(text);
}

std::string field(const Json& object, std::string_view key, std::string fallback) {
    const auto it = object.find(std::string(key));
    if (it == object.end()) return fallback;
    if (!it->is_string()) throw InputError("expected a string");
    return it->get<std::string>();
}

std::string required_string(const Json& object, std::string_view key) {
    auto value = field(object, key);
    if (value.empty()) throw InputError("required string is empty");
    return value;
}

int integer(const Json& object, std::string_view key, int fallback, int minimum, int maximum) {
    const auto it = object.find(std::string(key));
    if (it == object.end()) return fallback;
    if (!it->is_number_integer()) throw InputError("expected an integer");
    const auto value = it->get<double>();
    if (value < minimum || value > maximum) throw InputError("integer out of range");
    return static_cast<int>(value);
}

void check_key(std::string_view key) {
    if (key.empty() || key.size() > 128 || !std::ranges::all_of(key, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
            || c == '_' || c == '-' || c == '.';
    })) throw InputError("invalid identifier");
}

void check_namespace(std::string_view name) {
    if (name != "cc" && name != "di" && name != "shared") throw InputError("invalid namespace");
}

std::uint64_t stable_hash(std::string_view text) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const auto byte : text) {
        hash ^= static_cast<unsigned char>(byte);
        hash *= 1099511628211ULL;
    }
    return hash;
}

void error_log(std::string_view message) {
    std::osyncstream(std::cerr) << "[histrio] error: " << message << '\n';
}
} // namespace hi
