#include "feed.hpp"

#include <condition_variable>
#include <mutex>
#include <charconv>
#include <lapilli/http/client.hpp>
#include "augur/contract_runtime.hpp" /* augur-inject:import:51ea1e31 */
#include "../contracts/backoff.hpp" /* augur-inject:contract-predicate:a5f7389b */

namespace hi {
std::chrono::seconds feed_backoff(unsigned failures) { return ::augur::invoke_observed(::augur::ContractSpec{"C-8", "7785665d", "src/feed.cpp:9"}, hi::contracts::backoff_check, [&]() -> std::chrono::seconds { /* augur-inject:contract-wrap:7785665d */
    return std::chrono::seconds(std::min(60U, 1U << std::min(failures, 6U)));
}, failures); /* augur-inject:contract-end:7785665d */ }

void validate_feed_config(std::string_view url, std::string_view token) {
    constexpr std::string_view prefix = "http://127.0.0.1:";
    if (!url.starts_with(prefix) || token.empty() || token.size() > 2048 ||
        !std::ranges::all_of(token, [](unsigned char c) { return c >= 0x21 && c <= 0x7e; }))
        throw InputError("invalid Concordia feed configuration");
    url.remove_prefix(prefix.size());
    const auto slash = url.find('/');
    const auto port_text = url.substr(0, slash);
    unsigned port = 0;
    const auto [end, error] = std::from_chars(port_text.data(), port_text.data() + port_text.size(), port);
    if (slash == std::string_view::npos || error != std::errc{} || end != port_text.data() + port_text.size()
        || port == 0 || port > 65535) throw InputError("invalid Concordia feed endpoint");
    const auto path = url.substr(slash);
    if (!std::ranges::all_of(path, [](char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
            || c == '/' || c == '_' || c == '-' || c == '.';
    }) || path.starts_with("//")) throw InputError("invalid Concordia feed path");
}

std::string accept_feed_batch(const Json& batch, std::string_view cursor, const EventSink& sink) {
    const auto next = required_string(batch, "cursor");
    check_key(next);
    const auto& events = batch.at("events");
    if (!events.is_array() || events.size() > 128) throw InputError("invalid feed batch");
    for (const auto& event : events) {
        check_key(required_string(event, "type"));
        (void)integer(event, "active_session_count", 0, 0, 100000);
    }
    if (next != cursor) for (const auto& event : events) sink(event);
    return next;
}

void run_feed(std::stop_token stop, const std::string& url, const std::string& token, const EventSink& sink) {
    std::mutex mutex;
    std::condition_variable_any wait;
    std::atomic<bool> cancel{false};
    std::stop_callback on_stop(stop, [&] { cancel.store(true); });
    std::string cursor;
    unsigned failures = 0;
    while (!stop.stop_requested()) {
        auto delay = std::chrono::seconds(1);
        try {
            lapilli::http::ClientRequest request;
            request.url = url + (cursor.empty() ? "" : "?cursor=" + cursor);
            request.headers = {{"Authorization", "Bearer " + token}, {"Accept", "application/json"}};
            request.timeout = std::chrono::seconds(10);
            request.connect_timeout = std::chrono::seconds(2);
            request.max_body = max_body;
            request.redirect = lapilli::http::RedirectMode::error;
            request.cancel = &cancel;
            const auto response = lapilli::http::fetch(request);
            if (!response.ok()) throw InputError("feed refused request");
            cursor = accept_feed_batch(read_json(response.body), cursor, sink);
            failures = 0;
        } catch (const std::exception&) {
            if (stop.stop_requested()) return;
            delay = feed_backoff(failures);
            if (failures == 0 || failures == 6) error_log("Concordia feed unavailable; reconnecting");
            failures = std::min(7U, failures + 1);
        }
        std::unique_lock lock(mutex);
        wait.wait_for(lock, stop, delay, [] { return false; });
    }
}
} // namespace hi
