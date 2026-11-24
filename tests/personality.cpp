#include "fixtures.hpp"

namespace hi::test {
void profile_test() {
    Json persona{{"id", "alice"}, {"seed", "fixed"}, {"signals", {{"edit_count", 10}}}};
    const auto first = derive_profile(persona);
    persona["id"] = "bob";
    require(first == derive_profile(persona), "seed and signals drive the full profile");
    require(first.at("primary_driver") != first.at("secondary_driver"), "two distinct drivers");
}

void situation_test() {
    const auto messages = Json::array({{{"text", "🎉 マージ完了です"}}, {{"text", "おめでとう！"}}});
    const auto first = classify(messages, "pr.merged");
    require(first.situation == "celebration" && first.other == classify(messages, "pr.merged").other, "deterministic classifier");
    require(classify(Json::array(), "ask.answered").situation == "convergence", "event features contribute");
}

void role_test() {
    const auto matrix = roles();
    const auto role = choose_role("alice", "conflict", "constructive-objection", {}, matrix, Json::object());
    require(role == "critic", "operation drives role");
    require(role == choose_role("alice", "conflict", "constructive-objection", {}, matrix, Json::object()), "stable role");
}

void observe_test() {
    require(choose_role("alice", "conflict", "get-nowhere-with", {}, roles(), Json::object()) == "observe", "observe wins");
    const auto result = render({{"name", "Test"}}, {{"intent", "reply"}}, {{"speak", false}},
        [](auto) -> std::string { throw std::runtime_error("unexpected call"); });
    require(result.at("reason") == "observe", "observe does not invoke the model");
}

void learning_test() {
    Fixture fixture;
    fixture.store.put_persona("cc", {{"id", "alice"}, {"script_pattern_id", "until"}});
    const auto record = fixture.store.decide("cc", "alice", proposal());
    const auto id = record.at("decision_id").get<std::int64_t>();
    for (int i = 0; i < 5; ++i)
        fixture.store.feedback("cc", id, {{"id", std::to_string(i)}, {"kind", "like"}}, 1);
    require(!fixture.store.feedback("cc", id, {{"id", "0"}, {"kind", "like"}}, 1).at("applied").get<bool>(), "feedback retry");
    for (const auto& update : fixture.store.audit("cc", id).at("learning_history").get<Json>())
        require(update.at("after").get<double>() <= .1, "daily cap");
    const auto learned = fixture.store.decide("cc", "alice", proposal());
    require(!learned.at("reason").at("reaction_learning").empty(), "reaction learning returns to decisions");
    require(!learned.at("reason").at("weights").empty(), "role learning returns to decisions");
    Api api(fixture.store, {}, 12345);
    const auto current = send(api, "GET", "/v1/personas/alice/learning?namespace=cc");
    require(current.status == 200 && read_json(current.body).at("weights").contains("reaction"), "current weights API");
}

void pattern_test() {
    Fixture fixture;
    const auto catalog = fixture.store.patterns("di");
    require(catalog.size() == 6, "six initial patterns");
    const std::map<std::string, std::string> labels{{"until", "〜するまでは"}, {"after", "〜した後で"},
        {"never", "決して"}, {"always", "いつも"}, {"almost", "もう少しで"}, {"open_ended", "結末のない"}};
    for (const auto& pattern : catalog) require(field(pattern, "label") == labels.at(field(pattern, "id")), "Discutere label parity");
    const Json persona{{"id", "alice"}, {"seed", "repeatable"}};
    fixture.store.put_persona("di", persona);
    require(fixture.store.profile("di", "alice").at("script_pattern_id") == derive_profile(persona).at("script_pattern_id"), "single script");
}

void stance_test() {
    const auto pattern = seed().at("patterns").at(0).get<Json>();
    auto profile = derive_profile({{"id", "alice"}});
    const auto input = proposal().at("messages").get<Json>();
    const auto features = classify(input, "").features;
    const auto first = compute_stance(pattern, profile, features, false);
    profile["egogram"]["CP"] = 100;
    require(first == compute_stance(pattern, profile, features, false), "egogram cannot influence stance");
    require(first.at(1) < compute_stance(pattern, profile, Json::object(), false).at(1), "reaction dominates tendency");
    const auto twice = Json::array({input.at(0), input.at(0)});
    require(merge_features(features, features) == classify(twice, "").features, "incremental features equal full recomputation");
}

void corral_test() {
    const auto pattern = seed().at("patterns").at(3).get<Json>();
    const auto profile = derive_profile({{"id", "alice"}});
    const auto state = classify(Json::array({{{"text", "反対です。違う。"}}}), "");
    const auto first = corral(profile, pattern, state, {1, 0, 0, 0, 0}, 0);
    require(first.operation == "constructive-objection", "aggressive quadrant maps to constructive objection");
    const auto neutral = classify(Json::array({{{"text", "ありがとう"}}}), "");
    require(corral(profile, pattern, neutral, {}, 0).self == "I+U+", "normal quadrant");
}

void constructive_test() {
    const Json request{{"intent", "reply"}};
    const Json decision{{"operation", "constructive-objection"}};
    const auto reply = render({{"name", "Test"}}, request, decision,
        [](auto) { return R"({"objection":"evidence","text":"I-U- お前は無能"})"; });
    require(reply.at("skipped") == false && field(reply, "text").contains("根拠"), "only controlled constructive text leaves renderer");
    const auto rejected = render({{"name", "Test"}}, request, decision, [](auto) { return R"({"text":"Label"})"; });
    require(rejected.at("skipped") == true, "freeform constructive text rejected");
}

void privacy_test() {
    Fixture fixture;
    fixture.store.put_persona("cc", {{"id", "alice"}});
    const auto decision = fixture.store.decide("cc", "alice", {{"messages", Json::array({
        {{"speaker", "PersonNeverPersist"}, {"text", "SecretMessageIdentifier すみません"}}})}});
    const auto audit = fixture.store.audit("cc", decision.at("decision_id").get<std::int64_t>());
    require(audit.contains("other_quadrant"), "conversation moment is auditable");
    require(!audit.dump().contains("PersonNeverPersist") && !audit.dump().contains("SecretMessageIdentifier"), "no private label or raw message saved");
    require(!fixture.store.profile("cc", "alice").contains("other_quadrant"), "no counterpart attribute");
}

void audit_test() {
    Fixture fixture;
    fixture.store.put_persona("cc", {{"id", "alice"}, {"script_pattern_id", "until"}});
    const auto decision = fixture.store.decide("cc", "alice", proposal());
    const auto saved = fixture.store.audit("cc", decision.at("decision_id").get<std::int64_t>());
    const auto& reason = saved.at("reason");
    const auto stance = compute_stance(reason.at("pattern"), reason.at("profile"), saved.at("features"), saved.at("stressed"));
    require(saved.at("stance") == Json(stance), "record replays stance");
    require(choose_role("alice", field(saved, "situation"), field(saved, "operation"), stance, reason.at("matrix"),
                        reason.at("weights")) == field(saved, "role"), "record replays chosen role");
    const Classification state{saved.at("features"), field(saved, "situation"), saved.at("situation_confidence"),
                               field(saved, "other_quadrant"), saved.at("other_confidence"), reason.at("majority"),
                               reason.at("stress_trigger")};
    const auto replayed = corral(reason.at("profile"), reason.at("pattern"), state, stance, reason.at("prior_stress_turns"));
    require(replayed.self == field(saved, "self_quadrant") && replayed.operation == field(saved, "operation"),
            "record replays quadrant and operation");
}

void versions_test() {
    Fixture fixture;
    fixture.store.put_persona("di", {{"id", "speaker"}, {"script_pattern_id", "until"}});
    const auto old = fixture.store.decide("di", "speaker", proposal());
    const auto old_version = old.at("script_pattern_version").get<std::string>();
    auto document = seed();
    document["version"] = "di-review-2";
    fixture.store.put_patterns("di", document);
    require(fixture.store.profile("di", "speaker").at("script_pattern_version") == "di-review-2", "new catalog version active");
    document["patterns"][0]["healthy"] = "changed";
    bool rejected = false;
    try { fixture.store.put_patterns("di", document); } catch (const Conflict&) { rejected = true; }
    require(rejected, "version content immutable");
    require(field(fixture.store.audit("di", old.at("decision_id").get<std::int64_t>()), "script_pattern_version") == old_version,
            "old decisions retain their catalog version");
}
} // namespace hi::test
