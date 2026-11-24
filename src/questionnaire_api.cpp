#include "api.hpp"

#include <lapilli/crypto.hpp>
#include "augur/contract_runtime.hpp" /* augur-inject:import:92a508d0 */
#include "../contracts/questionnaire_request.hpp" /* augur-inject:contract-predicate:5d9d2eef */

namespace hi {
namespace {
bool valid_questionnaire_request(const Json& input) { return ::augur::invoke_observed(::augur::ContractSpec{"C-16", "011f30d7", "src/questionnaire_api.cpp:7"}, hi::contracts::questionnaire_request_check, [&]() -> bool { /* augur-inject:contract-wrap:011f30d7 */
    return input.is_object() && (input.empty() || (input.size() == 1 && input.contains("rerun")
        && input.at("rerun").is_boolean()));
}, input); /* augur-inject:contract-end:011f30d7 */ }

bool is_admin(const lapilli::http::ServerRequest& request, std::string_view token) {
    constexpr std::string_view prefix = "Bearer ";
    const auto header = request.header("Authorization").value_or("");
    return !token.empty() && header.starts_with(prefix)
        && lapilli::crypto::constant_time_equal(std::string_view(header).substr(prefix.size()), token);
}

lapilli::http::ServerResponse run_response(const Json& run, int status) {
    auto response = run.is_null()
        ? lapilli::http::ServerResponse::json(404, R"({"error":"not_found"})")
        : lapilli::http::ServerResponse::json(status, run.dump());
    response.headers.emplace_back("Cache-Control", "no-store");
    return response;
}
} // namespace

lapilli::http::ServerResponse Api::questionnaire_route(const lapilli::http::ServerRequest& request,
                                                       std::string_view ns, std::string_view id) {
    using lapilli::http::ServerResponse;
    if (request.method == "GET") {
        const auto admin = questionnaire_ && is_admin(request, questionnaire_->admin_token);
        return run_response(store_.questionnaire(ns, id, admin), 200);
    }
    const auto input = request.body.empty() ? Json::object() : read_json(request.body);
    if (!valid_questionnaire_request(input)) throw InputError("expected only an optional boolean rerun");
    if (!questionnaire_) return ServerResponse::json(503, R"({"error":"questionnaire_unavailable"})");
    if (generating_questionnaire_.test_and_set())
        return ServerResponse::json(503, R"({"error":"questionnaire_busy"})");
    struct Clear { std::atomic_flag& flag; ~Clear() { flag.clear(); } } clear{generating_questionnaire_};
    try {
        return run_response(store_.run_questionnaire(ns, id, input.value("rerun", false),
                                                    questionnaire_->data, questionnaire_->invoke), 201);
    } catch (const QuestionnaireError&) {
        return ServerResponse::json(502, R"({"error":"questionnaire_generation_failed"})");
    }
}
} // namespace hi
