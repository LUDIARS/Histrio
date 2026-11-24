#pragma once

#include "service.hpp"

#include <stop_token>

namespace hi {
using EventSink = std::function<void(const Json&)>;
void validate_feed_config(std::string_view url, std::string_view token);
std::string accept_feed_batch(const Json& batch, std::string_view cursor, const EventSink& sink);
void run_feed(std::stop_token stop, const std::string& url, const std::string& token, const EventSink& sink);
} // namespace hi
