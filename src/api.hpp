#pragma once

#include "store.hpp"

#include <atomic>
#include <lapilli/http/server.hpp>

namespace hi {
struct QuestionnaireApi {
    LifeScriptData data;
    Invoke invoke;
    std::string admin_token;
};

class Api {
public:
    Api(Store& store, Invoke invoke, unsigned port, std::string allowed_hosts = {},
        std::optional<QuestionnaireApi> questionnaire = std::nullopt);
    lapilli::http::ServerResponse handle(const lapilli::http::ServerRequest& request);

private:
    lapilli::http::ServerResponse route(const lapilli::http::ServerRequest& request);
    lapilli::http::ServerResponse questionnaire_route(const lapilli::http::ServerRequest& request,
                                                     std::string_view ns, std::string_view id);
    bool allow_host(std::string_view authority) const;

    Store& store_;
    Invoke invoke_;
    unsigned port_;
    std::vector<std::string> allowed_hosts_;
    std::optional<QuestionnaireApi> questionnaire_;
    std::atomic_flag rendering_{};
    std::atomic_flag generating_questionnaire_{};
};
} // namespace hi
