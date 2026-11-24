#include "api.hpp"
#include "feed.hpp"
#include "rules.hpp"

#include <condition_variable>
#include <iostream>
#include <syncstream>
#include <thread>
#include <lapilli/process.hpp>

namespace {
std::string env(const char* key, std::string fallback = {}) {
    static const auto environment = lapilli::process::current_environment();
    return lapilli::process::env_get(environment, key).value_or(std::move(fallback));
}

std::filesystem::path utf8_path(const std::string& value) {
    return std::filesystem::path(std::u8string(value.begin(), value.end()));
}

std::filesystem::path state_path() {
#ifdef _WIN32
    const auto base = env("LOCALAPPDATA");
#else
    auto base = env("XDG_STATE_HOME");
    if (base.empty() && !env("HOME").empty()) base = env("HOME") + "/.local/state";
#endif
    const auto configured = env("HISTRIO_DB");
    if (!configured.empty()) return utf8_path(configured);
    if (base.empty()) throw hi::InputError("HISTRIO_DB or the platform state directory is required");
    return utf8_path(base) / "Histrio" / "histrio.sqlite";
}

std::int64_t seconds_now() {
    return std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()).time_since_epoch().count();
}

void apply_rules(hi::Store& store, hi::RulesEngine& rules, const hi::Json& event) {
    if (env("HISTRIO_RULES_DISABLED") == "1") return;
    const auto type = hi::field(event, "type");
    std::optional<int> active;
    if (event.contains("active_session_count")) active = hi::integer(event, "active_session_count", 0, 0, 100000);
    for (const auto& result : rules.pass(store.rules("cc"), type, seconds_now(), active)) {
        if (!result.at("fire").get<bool>()) continue;
        // Concordia's legacy-observe rules record the routing decision.
        std::osyncstream(std::clog) << "[histrio] rule fired " << hi::field(result, "rule_id") << ' '
                                   << hi::field(result, "channel") << '/' << hi::field(result, "intent") << '\n';
    }
}
int run(std::optional<std::filesystem::path> import_source, bool rerun_questionnaires = false) {
    try {
        const auto path = state_path();
        if (import_source) {
            std::error_code error;
            if (!std::filesystem::is_regular_file(*import_source, error) || error)
                throw hi::InputError("Concordia import needs a readable SQLite file");
            if (std::filesystem::exists(path) && std::filesystem::equivalent(path, *import_source))
                throw hi::InputError("Concordia import source and Histrio destination must be distinct");
        }
        const auto data = utf8_path(env("HISTRIO_DATA_DIR", "data"));
        auto questionnaire = hi::load_life_script(data / "life-script");
        if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
        hi::Store store(path, hi::read_file(data / "script-patterns.json"), hi::read_file(data / "roles.json"));
        if (import_source) {
            std::cout << store.import_legacy(*import_source).dump() << '\n';
            return 0;
        }
        auto questionnaire_client = hi::claude_client(hi::questionnaire_role);
        if (rerun_questionnaires) {
            const auto result = store.rerun_questionnaires(questionnaire, questionnaire_client);
            std::cout << result.dump() << '\n';
            return result.at("failed") == 0 ? 0 : 1;
        }
        const auto port_input = env("HISTRIO_PORT");
        static_assert(HI_CATALOG_PORT > 0 && HI_CATALOG_PORT <= 65535);
        const auto port = lapilli::http::resolve_port(port_input, HI_CATALOG_PORT, "HISTRIO_PORT");
        const auto feed_url = env("HISTRIO_CC_FEED_URL");
        const auto feed_token = env("HISTRIO_CC_FEED_TOKEN");
        if (!feed_url.empty() || !feed_token.empty()) hi::validate_feed_config(feed_url, feed_token);

        hi::Api api(store, hi::claude_client(), port, env("LUDIARS_ALLOWED_HOSTS"),
                    hi::QuestionnaireApi{std::move(questionnaire), std::move(questionnaire_client),
                                         env("HISTRIO_QUESTIONNAIRE_ADMIN_TOKEN")});
        lapilli::http::ServerOptions options;
        options.port = static_cast<std::uint16_t>(port);
        options.limits.body_bytes = hi::max_body;
        options.threads = 4;
        lapilli::http::Server server(options, lapilli::http::synchronous(
            [&](const auto& request) { return api.handle(request); }));
        server.listen();
        server.stop_on_signals();

        hi::RulesEngine rules;
        std::jthread ticker([&](std::stop_token stop) {
            std::mutex mutex;
            std::condition_variable_any wait;
            std::unique_lock lock(mutex);
            while (!stop.stop_requested()) {
                try { apply_rules(store, rules, hi::Json::object()); }
                catch (const std::exception&) { hi::error_log("tick pass failed"); }
                wait.wait_for(lock, stop, std::chrono::seconds(1), [] { return false; });
            }
        });
        std::jthread feed;
        if (!feed_url.empty()) feed = std::jthread([&](std::stop_token stop) {
            hi::run_feed(stop, feed_url, feed_token, [&](const auto& event) { apply_rules(store, rules, event); });
        });
        server.run();
        return 0;
    } catch (const hi::InputError& error) {
        hi::error_log(error.what());
    } catch (const std::exception&) {
        hi::error_log("startup or run failed");
    }
    return 1;
}
} // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    if (argc == 3 && std::wstring_view(argv[1]) == L"import-cc") return run(std::filesystem::path(argv[2]));
    if (argc == 2 && std::wstring_view(argv[1]) == L"rerun-questionnaires") return run(std::nullopt, true);
#else
int main(int argc, char** argv) {
    if (argc == 3 && std::string_view(argv[1]) == "import-cc") return run(std::filesystem::path(argv[2]));
    if (argc == 2 && std::string_view(argv[1]) == "rerun-questionnaires") return run(std::nullopt, true);
#endif
    if (argc == 1) return run(std::nullopt);
    hi::error_log("expected no arguments, import-cc <sqlite-file>, or rerun-questionnaires");
    return 1;
}
