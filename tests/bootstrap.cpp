#include "fixtures.hpp"

namespace hi::test {
void health_test() {
    Fixture fixture;
    Api api(fixture.store, {}, 12345, ".allowed.test");
    const auto health_reply = send(api, "GET", "/health");
    require(health_reply.status == 200 && read_json(health_reply.body).at("status") == "ok", "health");
    lapilli::http::ServerRequest request;
    request.method = "GET";
    request.path = "/health";
    request.headers = {{"Host", "evilallowed.test"}};
    require(api.handle(request).status == 403, "host suffix boundary");
    request.headers = {{"Host", "sub.allowed.test"}};
    require(api.handle(request).status == 200, "configured host");
    request.headers.push_back({"Origin", "https://sub.allowed.test"});
    require(api.handle(request).status == 403, "loopback API forbids cross-origin interaction");
}

void personas_test() {
    Fixture fixture;
    Api api(fixture.store, {}, 12345);
    const Json persona{{"id", "alice"}, {"name", "Shared"}, {"script_pattern_id", "until"}};
    require(send(api, "POST", "/v1/personas", persona).status == 200, "put shared persona");
    require(send(api, "POST", "/v1/personas?namespace=cc", {{"id", "alice"}, {"name", "Cc"}}).status == 200, "put scoped persona");
    const auto cc = read_json(send(api, "GET", "/v1/personas?namespace=cc").body);
    require(cc.size() == 1 && cc.at(0).at("name") == "Cc", "local persona shadows shared");
    const auto di = send(api, "GET", "/v1/personas/alice/profile?namespace=di");
    require(di.status == 200 && read_json(di.body).at("script_pattern_id") == "until", "Discutere profile contract");
    require(send(api, "GET", "/v1/personas?namespace=third").status == 400, "unknown namespace");
}

void import_test() {
    Fixture fixture;
    const auto path = fixture.temp.path / "legacy.sqlite";
    {
        Database legacy(path);
        legacy.exec(R"sql(
CREATE TABLE personas(id,name,description,traits,speech_style,skill_template,learned_notes,display_name,
                      generated,origin_session_id,created_at,updated_at);
CREATE TABLE persona_feedback_log(id,persona_id,session_id,ts,kind,delta,detail);
INSERT INTO personas VALUES('old','Old','','[]','','','["note"]','',0,NULL,1,2);
INSERT INTO persona_feedback_log VALUES(1,'old',NULL,3,'boyaki','note',NULL);
)sql");
    }
    require(fixture.store.import_legacy(path) == Json{{"personas", 1}, {"feedback", 1}}, "legacy data imported");
    require(fixture.store.import_legacy(path) == Json{{"personas", 0}, {"feedback", 0}}, "import is idempotent");
    require(fixture.store.profile("cc", "old").contains("script_pattern_id"), "legacy persona gained a script");
    require(fixture.store.profile("di", "old").is_null(), "legacy namespace is isolated");
}

void render_test() {
    Fixture fixture;
    fixture.store.put_persona("di", {{"id", "speaker"}});
    int calls = 0;
    Api api(fixture.store, [&](std::string_view prompt) {
        ++calls;
        require(prompt.contains("会話資料"), "context in prompt");
        return R"({"text":"発話です。","situation":"casual","other":"I+U+"})";
    }, 12345);
    const Json input{{"persona_id", "speaker"}, {"intent", "reply"},
                     {"context", {{"messages", Json::array({{{"text", "例です"}}})}}}};
    const auto reply = send(api, "POST", "/v1/render?namespace=di", input);
    require(reply.status == 200 && read_json(reply.body).at("text") == "発話です。", "rendered utterance");
    require(calls == 1 && fixture.store.audit("di", 1).is_null(), "one call and render is stateless");
    Api unavailable(fixture.store, {}, 12345);
    const auto failure = send(unavailable, "POST", "/v1/render?namespace=di", input);
    require(failure.status == 503 && read_json(failure.body).at("skipped") == true, "missing CLI reports a skip");
}

void rules_test() {
    Fixture fixture;
    Api api(fixture.store, {}, 12345);
    const Json event{{"id", "event"}, {"trigger_type", "event"}, {"event_kind", "chat.posted"}, {"cooldown_sec", 10},
                     {"target", "consultation"}, {"conditions", Json::array({{{"type", "any_active_session"}}})}};
    const Json tick{{"id", "tick"}, {"trigger_type", "tick"}, {"tick_sec", 5}};
    const Json rules{{"rules", Json::array({event, tick})}};
    require(send(api, "PUT", "/v1/rules?namespace=cc", rules).status == 200, "valid rules");
    require(send(api, "PUT", "/v1/rules?namespace=cc", {{"rules", Json::array({event, event})}}).status == 400, "duplicate id");
    require(fixture.store.rules("cc") == rules, "failed review keeps accepted rules");
    RulesEngine engine;
    require(!engine.pass(rules, "chat.posted", 1, 0).at(0).at("fire").get<bool>(), "no active session");
    require(engine.pass(rules, "chat.posted", 10, 1).empty(), "cooldown");
    require(engine.pass(rules, "", 10).at(0).at("rule_id") == "tick", "tick is independent");
    require(engine.pass(rules, "chat.posted", 11).at(0).at("intent") == "consult", "event resumes after cooldown");
    require(engine.pass(rules, "", 14).empty() && engine.pass(rules, "", 15).size() == 1, "tick cadence");
}

void feed_test() {
    validate_feed_config("http://127.0.0.1:12345/events", "test-token");
    bool rejected = false;
    try { validate_feed_config("http://localhost.attacker.test:12345/events", "test-token"); }
    catch (const InputError&) { rejected = true; }
    require(rejected, "feed token remains on the configured loopback adapter");
    require(feed_backoff(0).count() == 1 && feed_backoff(5).count() == 32 && feed_backoff(200).count() == 60, "bounded backoff");
    int received = 0;
    const Json batch{{"cursor", "one"}, {"events", Json::array({{{"type", "chat.posted"}}})}};
    const auto callback = [&](const Json&) { ++received; };
    require(accept_feed_batch(batch, "", callback) == "one", "advance acknowledged cursor");
    accept_feed_batch(batch, "one", callback);
    require(received == 1, "ignore repeated cursor");
}

void bootstrap_contract_test() {
    Fixture fixture;
    Api api(fixture.store, {}, 12345);
    lapilli::http::ServerRequest req;
    req.method = "POST";
    req.path = "/v1/personas";
    req.headers = {{"Host", "localhost:12345"}};
    req.body = std::string(max_body + 1, 'x');
    require(api.handle(req).status == 400, "oversized JSON rejection");
    req.body = std::string(80, '[') + "0" + std::string(80, ']');
    require(api.handle(req).status == 400, "overdeep JSON rejection");
    require(fixture.store.list_personas("shared").empty(), "rejected requests did not write");
}
} // namespace hi::test
