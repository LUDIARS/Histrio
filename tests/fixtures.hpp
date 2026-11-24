#pragma once

#include "api.hpp"
#include "feed.hpp"
#include "rules.hpp"

#include <cstdio>
#include <random>

// CMake は data/ の絶対パスを渡す。静的解析 (Praesidium) は文字列の define を受け付けないので、未定義なら相対パスにする。
#ifndef HI_TEST_DATA
#define HI_TEST_DATA "data"
#endif

namespace hi::test {
inline void require(bool result, const char* message) {
    if (!result) throw std::runtime_error(message);
}

struct Temp {
    std::filesystem::path path;
    Temp() {
        auto& p = path;
        p = std::filesystem::temp_directory_path() / ("histrio-test-" + std::to_string(std::random_device{}())
                                                     + "-" + std::to_string(std::random_device{}()));
        if (!std::filesystem::create_directory(p)) throw std::runtime_error("cannot create isolated test directory");
    }
    ~Temp() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
        if (error) std::fputs("cannot clean test directory\n", stderr);
    }
};

inline Json seed() { return read_file(std::filesystem::path(HI_TEST_DATA) / "script-patterns.json"); }
inline Json roles() { return read_file(std::filesystem::path(HI_TEST_DATA) / "roles.json"); }

struct Fixture {
    Temp temp;
    Store store{temp.path / "local.sqlite", seed(), roles()};
};

inline lapilli::http::ServerResponse send(Api& api, std::string method, std::string target, const Json& body = nullptr) {
    lapilli::http::ServerRequest request;
    request.method = std::move(method);
    const auto query = target.find('?');
    request.path = target.substr(0, query);
    if (query != std::string::npos) request.query = target.substr(query + 1);
    request.headers = {{"Host", "127.0.0.1:12345"}};
    if (!body.is_null()) request.body = body.dump();
    return api.handle(request);
}

inline Json proposal() { return {{"messages", Json::array({{{"text", "新規提案です。もしこの条件ならどうですか？"}}})}}; }

void health_test();
void personas_test();
void import_test();
void render_test();
void rules_test();
void feed_test();
void bootstrap_contract_test();
void profile_test();
void situation_test();
void role_test();
void observe_test();
void learning_test();
void pattern_test();
void stance_test();
void corral_test();
void constructive_test();
void privacy_test();
void audit_test();
void versions_test();
void questionnaire_assignment_test();
void questionnaire_replay_test();
void questionnaire_scoring_test();
void questionnaire_invalid_test();
void questionnaire_privacy_test();
void questionnaire_followups_test();
} // namespace hi::test
