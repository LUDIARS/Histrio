#include "fixtures.hpp"
#include "augur/contract_runtime.hpp" /* augur-inject:import:ef5ea754 */
#include "../contracts/questionnaire_replay.hpp" /* augur-inject:contract-predicate:1a0add84 */

namespace hi::test {
namespace {
constexpr auto route = "/v1/personas/speaker/questionnaire?namespace=di";
constexpr auto admin_token = "fixture-token";

LifeScriptData questionnaire_data() {
    return load_life_script(std::filesystem::path(HI_TEST_DATA) / "life-script");
}

Json sample_answers() {
    return read_file("tests/fixtures/life-script/life-script-answers.example.json");
}

Json sample_score() {
    return read_file("tests/fixtures/life-script/feature-card.example.json").at("questionnaire").at("lifeScript");
}

void put_speaker(Store& store) {
    store.put_persona("di", {{"id", "speaker"}, {"description", "架空の編集者"}, {"script_pattern_id", "after"}});
}

lapilli::http::ServerResponse get_run(Api& api, std::string_view token) {
    lapilli::http::ServerRequest request;
    request.method = "GET";
    request.path = "/v1/personas/speaker/questionnaire";
    request.query = "namespace=di";
    request.headers = {{"Host", "127.0.0.1:12345"}, {"Authorization", "Bearer " + std::string(token)}};
    return api.handle(request);
}

struct FakeClaude {
    Json answers = sample_answers();
    std::string reaction = "まず見落としがないか読み直します。";
    int sections = 0;
    int followups = 0;

    std::string operator()(std::string_view prompt) {
        require(prompt.contains("架空の編集者"), "prompt uses the persona store description");
        const auto content = read_json(prompt.substr(prompt.rfind('\n') + 1));
        if (content.contains("ask")) {
            ++followups;
            return Json{{"answer", reaction}}.dump();
        }
        ++sections;
        auto response = Json::object();
        const auto& items = content.at("items");
        for (const auto& item : items) {
            require(!item.contains("key") && !item.contains("reverse") && !content.contains("title"),
                    "prompt omits scoring metadata and section title");
            const auto id = field(item, "id");
            const auto& source = answers.at(item.contains("choices") ? "forcedChoice" : "answers");
            if (source.contains(id)) response[id] = source.at(id);
            if (!item.contains("choices")) continue;
            for (const auto& choice : item.at("choices"))
                require(!choice.contains("key"), "forced choice prompt omits driver");
        }
        return response.dump();
    }
};

bool same_questionnaire_profile(const Json& first, const Json& second) { return ::augur::invoke_observed(::augur::ContractSpec{"C-13", "151f3322", "tests/questionnaire.cpp:63"}, hi::contracts::questionnaire_replay_check, [&]() -> bool { /* augur-inject:contract-wrap:151f3322 */
    for (const auto key : {"script_pattern_id", "primary_driver", "secondary_driver", "basic_quadrant", "position"}) {
        if (first.at(key) != second.at(key)) return false;
    }
    return true;
}, first, second); /* augur-inject:contract-end:151f3322 */ }
} // namespace

void questionnaire_assignment_test() {
    Fixture fixture;
    put_speaker(fixture.store);
    FakeClaude fake;
    Api api(fixture.store, {}, 12345, {}, QuestionnaireApi{questionnaire_data(), std::ref(fake), admin_token});
    const auto first = send(api, "POST", route);
    require(first.status == 201, "questionnaire creates a run");
    const auto run = read_json(first.body);
    require(fake.sections == 4 && fake.followups == 5, "four section calls and at most five followups");
    require(!run.contains("answers") && run.at("role") == "haiku", "creation omits raw answers and records role");
    const auto profile = fixture.store.profile("di", "speaker");
    require(profile.at("script_pattern_id") == "after" && profile.at("questionnaire_script_pattern_id") == "until"
            && profile.at("source") == "registered", "a registered script pattern is kept over the questionnaire");
    require(profile.at("primary_driver") == "perfect"
            && profile.at("secondary_driver") == "strong" && profile.at("confidence") == "high",
            "dominance drives assignment");
    require(profile.at("basic_quadrant") == "border" && profile.at("position").at("other") == "positive",
            "position boundary is retained");
    require(read_json(get_run(api, "wrong").body) == run, "public and wrong-token GET omit answers");
    const auto saved = read_json(get_run(api, admin_token).body).at("answers");
    const auto sample = sample_answers();
    require(nlohmann::json(saved.at("answers")) == nlohmann::json(sample.at("answers"))
            && nlohmann::json(saved.at("forcedChoice")) == nlohmann::json(sample.at("forcedChoice")),
            "admin GET includes persisted answers");
    require(send(api, "POST", route, Json::object()).status == 409 && fake.sections == 4,
            "existing run requires explicit rerun");
    fixture.store.put_persona("di", {{"id", "speaker"}, {"speech_style", "簡潔"}});
    require(fixture.store.profile("di", "speaker").at("questionnaire_run_id") == run.at("run_id"),
            "persona update preserves questionnaire assignment");
    for (auto& value : fake.answers.at("answers")) value = 2;
    const auto rerun = send(api, "POST", route, {{"rerun", true}});
    require(rerun.status == 201, "explicit rerun succeeds");
    const auto fallback = fixture.store.profile("di", "speaker");
    require(fallback.at("script_pattern_id") == "after" && fallback.at("source") == "seed"
            && fallback.at("questionnaire_run_id") != run.at("run_id"), "flat response selects seed");
    Database db(fixture.temp.path / "local.sqlite", true);
    auto rows = db.query("SELECT COUNT(*) FROM persona_questionnaire_runs");
    require(rows.step() && rows.number(0) == 2, "rerun preserves the previous run");

    fixture.store.put_persona("di", {{"id", "blank"}});
    require(send(api, "POST", "/v1/personas/blank/questionnaire?namespace=di").status == 400
            && fake.sections == 8 && fixture.store.profile("di", "blank").at("source") == "seed",
            "personas with no description retain the seed without model calls");

    fake.answers = sample_answers();
    fixture.store.put_persona("di", {{"id", "free"}, {"description", "型を指定しない架空の編集者"}});
    require(send(api, "POST", "/v1/personas/free/questionnaire?namespace=di").status == 201, "unregistered persona runs");
    const auto free = fixture.store.profile("di", "free");
    require(free.at("script_pattern_id") == "until" && free.at("source") == "questionnaire"
            && !free.contains("questionnaire_script_pattern_id"), "without a registered pattern the questionnaire decides");
}

void questionnaire_replay_test() {
    Fixture fixture;
    put_speaker(fixture.store);
    FakeClaude fake;
    const auto data = questionnaire_data();
    fixture.store.run_questionnaire("di", "speaker", false, data, std::ref(fake));
    Store reopened(fixture.temp.path / "local.sqlite", seed(), roles());
    const auto saved = reopened.questionnaire("di", "speaker", true);
    const auto persona = reopened.persona("di", "speaker");
    const auto replay = keep_registered_pattern(persona, questionnaire_assignment(derive_profile(persona),
        score_life_script(data.questionnaire, saved.at("answers"))));
    require(same_questionnaire_profile(replay, reopened.profile("di", "speaker")),
            "replay from saved answers reproduces the persisted profile");
}

void questionnaire_scoring_test() {
    const auto data = questionnaire_data();
    auto answers = sample_answers();
    const auto result = score_life_script(data.questionnaire, answers);
    require(nlohmann::json(result) == nlohmann::json(sample_score()), "all fields match Discutere's feature card");
    answers.at("answers").erase("D-BP-1");
    require(!score_life_script(data.questionnaire, answers).at("drivers").at("scores").at("be_perfect").is_null(),
            "three out of four items suffice");
    answers.at("answers").erase("D-BP-2");
    require(score_life_script(data.questionnaire, answers).at("drivers").at("scores").at("be_perfect").is_null(),
            "two out of four items are insufficient");
    Temp temp;
    const auto directory = std::filesystem::path(HI_TEST_DATA) / "life-script";
    std::filesystem::copy_file(directory / "life-script-followups.ja.json", temp.path / "life-script-questionnaire.ja.json");
    std::filesystem::copy_file(directory / "life-script-followups.ja.json", temp.path / "life-script-followups.ja.json");
    bool rejected = false;
    try { load_life_script(temp.path); } catch (const InputError&) { rejected = true; }
    require(rejected, "startup loader rejects a mismatched questionnaire revision");
}

void questionnaire_invalid_test() {
    Fixture fixture;
    put_speaker(fixture.store);
    for (const auto body : {"[]", R"({"D-BP-1":5})", R"({"FC-1":"b"})", R"({"D-BP-1":1.25})"}) {
        int calls = 0;
        Api api(fixture.store, {}, 12345, {}, QuestionnaireApi{questionnaire_data(),
            [&](std::string_view) { ++calls; return std::string(body); }, ""});
        require(send(api, "POST", route).status == 502 && calls == 1, "bad model output fails without retries");
        require(send(api, "GET", route).status == 404 && fixture.store.profile("di", "speaker").at("source") == "seed",
                "invalid outputs persist neither a run nor a profile update");
    }
    FakeClaude fake;
    fixture.store.run_questionnaire("di", "speaker", false, questionnaire_data(), std::ref(fake));
    const auto original = fixture.store.questionnaire("di", "speaker", false);
    Api api(fixture.store, {}, 12345, {}, QuestionnaireApi{questionnaire_data(), [&](std::string_view prompt) {
        if (prompt.contains("\"ask\":")) return std::string(R"({"answer":7})");
        return fake(prompt);
    }, ""});
    require(send(api, "POST", route, {{"rerun", true}}).status == 502
            && fixture.store.questionnaire("di", "speaker", false) == original,
            "invalid followup keeps the previous run");
}

void questionnaire_privacy_test() {
    Fixture fixture;
    put_speaker(fixture.store);
    FakeClaude fake;
    Api api(fixture.store, {}, 12345, {}, QuestionnaireApi{questionnaire_data(), std::ref(fake), admin_token});
    for (const auto key : {"answers", "forcedChoice", "score", "reaction_examples", "lifeScript"}) {
        require(send(api, "POST", route, {{key, Json::object()}}).status == 400,
                "questionnaire route rejects supplied data");
    }
    require(send(api, "POST", route, {{"rerun", 1}}).status == 400, "rerun must be a boolean");
    require(send(api, "POST", "/v1/personas?namespace=di",
        {{"id", "pack"}, {"speakerKey", "ext:example:0123456789abcdef"}, {"description", "External"}}).status == 400,
        "proxy persona packs are outside the virtual persona store");
    require(send(api, "POST", "/v1/personas?namespace=di",
        {{"id", "speaker"}, {"answers", sample_answers()}, {"score", sample_score()}}).status == 200,
        "legacy persona field projection remains compatible");
    require(!fixture.store.persona("di", "speaker").contains("answers")
            && !fixture.store.persona("di", "speaker").contains("score")
            && send(api, "GET", route).status == 404 && fake.sections == 0, "human results are never imported");
}

void questionnaire_followups_test() {
    Fixture fixture;
    put_speaker(fixture.store);
    FakeClaude fake;
    const auto data = questionnaire_data();
    const auto topics = plan_life_script_followups(data, sample_score()).at("topics");
    const std::array kinds{"forced_choice_mismatch", "dominant_driver", "dominant_driver",
                          "dominant_process", "correspondence_mismatch"};
    require(topics.size() == kinds.size(), "planner applies the topic cap");
    for (std::size_t i = 0; i < kinds.size(); ++i)
        require(topics.at(i).at("kind") == kinds[i], "planner follows upstream priority");
    const auto first = fixture.store.run_questionnaire("di", "speaker", false, data, std::ref(fake));
    const auto recorded = fixture.store.decide("di", "speaker", proposal());
    fake.reaction = "言い方を変えてから出します。";
    const auto second = fixture.store.run_questionnaire("di", "speaker", true, data, std::ref(fake));
    require(first.at("score") == second.at("score")
            && first.at("assignment").at("script_pattern_id") == second.at("assignment").at("script_pattern_id"),
            "followup answers never affect score or assignment");
    const auto old = fixture.store.questionnaire_examples(first.at("assignment"));
    require(old.at(0).at("answer") != fake.reaction, "historical profile keeps its own reaction examples");
    std::vector<std::string> prompts;
    Api api(fixture.store, [&](std::string_view prompt) {
        prompts.emplace_back(prompt);
        return R"({"text":"発話です。"})";
    }, 12345);
    Json input{{"persona_id", "speaker"}, {"intent", "reply"},
               {"context", {{"messages", Json::array({{{"text", "例です"}}})}}}};
    require(send(api, "POST", "/v1/render?namespace=di", input).status == 200, "render with latest profile");
    input["decision_id"] = recorded.at("decision_id");
    require(send(api, "POST", "/v1/render?namespace=di", input).status == 200, "render with historical decision");
    require(prompts.size() == 2 && prompts.at(0).contains("この人ならこう言う")
            && prompts.at(0).contains(fake.reaction) && prompts.at(1).contains(field(old.at(0), "answer"))
            && !prompts.at(1).contains(fake.reaction), "render uses the examples for the selected run");
}

void questionnaire_reply_json_test() {
    // 実際の claude (haiku) の返事: ```json の囲み + 説明文 + 設問の並び
    const auto fenced = read_reply_json("```json\n{\"items\":[{\"id\":\"D-BP-1\",\"value\":4},"
                                        "{\"id\":\"D-BS-1\",\"value\":3}]}\n```\n\n尺度は1〜4として解釈しました。");
    require(fenced == Json{{"D-BP-1", 4}, {"D-BS-1", 3}}, "fenced items reply becomes an id map");
    require(read_reply_json(R"(前置き {"FC-1":"a"} 後書き)") == Json{{"FC-1", "a"}}, "surrounding prose is dropped");
    require(read_reply_json(R"({"answer":"そうですね"})") == Json{{"answer", "そうですね"}}, "plain object is kept");
    const auto rejects = [](std::string_view reply) {
        try { (void)read_reply_json(reply); } catch (const std::exception&) { return true; }
        return false;
    };
    require(rejects("[]"), "array reply");
    require(rejects("```json\n{\"D-BP-1\":3}"), "unterminated fence");
    require(rejects("答えられません"), "no JSON object");
    require(rejects(R"({"items":[{"value":3}]})"), "items without id");
    // 値の範囲の検査は read_reply_json ではなく valid_section_answers が行う (5 はここでは通る)
    require(read_reply_json(R"({"D-BP-1":5})") == Json{{"D-BP-1", 5}}, "values are not validated here");
}
} // namespace hi::test
