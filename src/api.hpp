#pragma once

#include "store.hpp"

#include <atomic>
#include <lapilli/http/server.hpp>

namespace hi {
class Api {
public:
    Api(Store& store, Invoke invoke, unsigned port, std::string allowed_hosts = {});
    lapilli::http::ServerResponse handle(const lapilli::http::ServerRequest& request);

private:
    lapilli::http::ServerResponse route(const lapilli::http::ServerRequest& request);
    bool allow_host(std::string_view authority) const;

    Store& store_;
    Invoke invoke_;
    unsigned port_;
    std::vector<std::string> allowed_hosts_;
    std::atomic_flag rendering_{};
};
} // namespace hi
