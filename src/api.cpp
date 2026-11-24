#include "api.hpp"

#include <charconv>
#include <cctype>
#include "augur/contract_runtime.hpp" /* augur-inject:import:052c49a5 */
#include "../contracts/health.hpp" /* augur-inject:contract-predicate:a6f94b88 */

namespace hi {
namespace {
using lapilli::http::ServerRequest;
using lapilli::http::ServerResponse;

ServerResponse response(int status, const Json& payload) {
    return ServerResponse::json(status, payload.dump());
}

ServerResponse found(const Json& payload) {
    return payload.is_null() ? response(404, {{"error", "not_found"}}) : response(200, payload);
}

std::string ns_from(std::string_view query) {
    if (query.empty()) return "shared";
    if (!query.starts_with("namespace=")) throw InputError("expected namespace query");
    query.remove_prefix(10);
    check_namespace(query);
    return std::string(query);
}

std::string lowercase(std::string_view value) {
    std::string lower(value);
    std::ranges::transform(lower, lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower;
}

std::int64_t decision_id(std::string_view value) {
    std::int64_t id = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), id);
    if (error != std::errc{} || end != value.data() + value.size() || id < 1) throw InputError("invalid decision id");
    return id;
}

bool hostname(std::string_view value) {
    return !value.empty() && !value.starts_with('.') && !value.ends_with('.')
        && !value.contains("..") && std::ranges::all_of(value, [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-';
        });
}

Json voice_policy(const Json& profile, const Json& pattern, const Json& decision) {
    return {{"speak", decision.at("speak")}, {"role", decision.at("role")}, {"operation", decision.at("operation")},
            {"stance", decision.at("stance")}, {"tone", decision.at("stressed").get<bool>() ? pattern.at("stress") : pattern.at("healthy")},
            {"egogram", profile.at("egogram")}, {"ask_situation", decision.at("situation_confidence").get<double>() < .5},
            {"ask_other", decision.at("other_confidence").get<double>() < .5}};
}
}

Json health() { return ::augur::invoke_observed(::augur::ContractSpec{"C-1", "874893b2", "src/api.cpp:56"}, hi::contracts::health_check, [&]() -> Json { /* augur-inject:contract-wrap:874893b2 */ return {{"status", "ok"}}; }); /* augur-inject:contract-end:874893b2 */ }

Api::Api(Store& store, Invoke invoke, unsigned port, std::string allowed_hosts)
    : store_(store), invoke_(std::move(invoke)), port_(port) {
    std::string_view values(allowed_hosts);
    while (!values.empty()) {
        const auto comma = values.find(',');
        auto part = values.substr(0, comma);
        const auto first = part.find_first_not_of(" \t");
        if (first != std::string_view::npos) {
            auto host = lowercase(part.substr(first, part.find_last_not_of(" \t") - first + 1));
            if (!hostname(host.starts_with('.') ? std::string_view(host).substr(1) : std::string_view(host)))
                throw InputError("invalid LUDIARS_ALLOWED_HOSTS entry");
            allowed_hosts_.push_back(std::move(host));
        }
        if (comma == std::string_view::npos) break;
        values.remove_prefix(comma + 1);
    }
}

bool Api::allow_host(std::string_view authority) const {
    if (authority.empty()) return false;
    const auto colon = authority.rfind(':');
    const auto host = lowercase(authority.substr(0, colon));
    if (!hostname(host)) return false;
    unsigned port = 0;
    if (colon != std::string_view::npos) {
        const auto number = authority.substr(colon + 1);
        const auto [end, error] = std::from_chars(number.data(), number.data() + number.size(), port);
        if (error != std::errc{} || end != number.data() + number.size() || port == 0 || port > 65535) return false;
    }
    if (host == "127.0.0.1" || host == "localhost") return port == 0 || port == port_;
    return std::ranges::any_of(allowed_hosts_, [&](const auto& allowed) {
        return host == allowed || (allowed.starts_with('.') && (host.ends_with(allowed) || host == allowed.substr(1)));
    });
}

ServerResponse Api::handle(const ServerRequest& request) {
    if (!allow_host(request.header("Host").value_or("")) || request.header("Origin"))
        return response(403, {{"error", "forbidden_origin_or_host"}});
    try {
        return route(request);
    } catch (const Conflict& error) {
        return response(409, {{"error", error.what()}});
    } catch (const InputError& error) {
        return response(400, {{"error", error.what()}});
    } catch (const Json::exception&) {
        return response(400, {{"error", "invalid_document"}});
    } catch (const std::exception&) {
        error_log("request failed");
        return response(500, {{"error", "internal_error"}});
    }
}

ServerResponse Api::route(const ServerRequest& request) {
    const auto path = std::string_view(request.path);
    const auto& method = request.method;
    if (path == "/health" && method == "GET") return response(200, health());
    const auto ns = ns_from(request.query);
    const auto body = [&] {
        auto value = read_json(request.body);
        if (!value.is_object()) throw InputError("expected an object request");
        return value;
    };
    if (path == "/v1/personas") {
        if (method == "GET") return response(200, store_.list_personas(ns));
        if (method == "POST") return response(200, store_.put_persona(ns, body()));
    }
    if (path == "/v1/script-patterns") {
        if (method == "GET") return response(200, store_.patterns(ns));
        if (method == "PUT") return response(200, store_.put_patterns(ns, body()));
    }
    if (path == "/v1/rules") {
        if (method == "GET") return response(200, store_.rules(ns));
        if (method == "PUT") return response(200, store_.put_rules(ns, body()));
    }
    if (path == "/v1/render" && method == "POST") {
        const auto input = body();
        const auto id = required_string(input, "persona_id");
        const auto snapshot = store_.render_snapshot(ns, id);
        if (snapshot.is_null()) return found(snapshot);
        if (rendering_.test_and_set()) return response(503, {{"error", "renderer_busy"}});
        struct Clear { std::atomic_flag& flag; ~Clear() { flag.clear(); } } clear{rendering_};
        Json policy;
        if (input.contains("decision_id")) {
            const auto& key = input.at("decision_id");
            if (!key.is_number_integer()) throw InputError("expected an integer decision id");
            const auto recorded = store_.audit(ns, decision_id(key.dump()));
            if (recorded.is_null() || field(recorded, "persona_id") != id) return found(nullptr);
            policy = voice_policy(recorded.at("reason").at("profile"), recorded.at("reason").at("pattern"), recorded);
        } else {
            const auto context = input.value("context", Json::object());
            auto decision = decision_record(snapshot.at("persona"), snapshot.at("profile"), snapshot.at("pattern"),
                                            context, {{"matrix", snapshot.at("matrix")}}, 0);
            policy = voice_policy(snapshot.at("profile"), snapshot.at("pattern"), decision);
        }
        const auto rendered = render(snapshot.at("persona"), input, policy, invoke_);
        return response(field(rendered, "reason") == "render_unavailable" ? 503 : 200, rendered);
    }
    if (path.starts_with("/v1/personas/")) {
        auto rest = path.substr(13);
        const auto slash = rest.find('/');
        const auto id = rest.substr(0, slash);
        if (slash != std::string_view::npos) {
            const auto leaf = rest.substr(slash + 1);
            if (leaf == "profile" && method == "GET") return found(store_.profile(ns, id));
            if (leaf == "learning" && method == "GET") return found(store_.learning(ns, id));
            if (leaf == "decide" && method == "POST") return found(store_.decide(ns, id, body()));
        }
    }
    if (path.starts_with("/v1/decisions/")) {
        const auto rest = path.substr(14);
        const auto slash = rest.find('/');
        if (slash == std::string_view::npos && method == "GET") return found(store_.audit(ns, decision_id(rest)));
        if (slash != std::string_view::npos && rest.substr(slash) == "/feedback" && method == "POST") {
            const auto day = std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now()).time_since_epoch().count();
            return found(store_.feedback(ns, decision_id(rest.substr(0, slash)), body(), day));
        }
    }
    return response(404, {{"error", "not_found"}});
}
} // namespace hi
