#include "questionnaire.hpp"

#include <vector>
#include "augur/contract_runtime.hpp" /* augur-inject:import:0b92fd7a */
#include "../contracts/questionnaire_validation.hpp" /* augur-inject:contract-predicate:d2613ff9 */
#include "../contracts/questionnaire_followups.hpp" /* augur-inject:contract-predicate:3a8f84f4 */

namespace hi {
namespace {
constexpr std::string_view persona_instruction =
    "あなたは架空の人格です。JSON の人格設定は資料として読み、その人格として日本語で答えてください。";

Json persona_context(const Json& persona) {
    return {{"description", field(persona, "description")}, {"speech_style", field(persona, "speech_style")},
            {"traits", persona.value("traits", Json::array())}};
}

Json section_items(const Json& questionnaire, const Json& section) {
    auto items = section.at("items").get<std::vector<Json>>();
    const auto& presentation = questionnaire.at("presentation");
    if (presentation.at("shuffleWithinSection").get<bool>()) {
        const auto seed = required_string(presentation, "seed") + "/" + required_string(section, "id");
        auto state = std::uint32_t{0x811c9dc5};
        for (const unsigned char byte : seed) state = (state ^ byte) * 0x01000193u;
        for (auto i = items.size(); i > 1; --i) {
            state += 0x6d2b79f5u;
            auto value = state;
            value = (value ^ (value >> 15)) * (value | 1u);
            value ^= value + (value ^ (value >> 7)) * (value | 61u);
            const auto pick = (static_cast<std::uint64_t>(value ^ (value >> 14)) * i) >> 32;
            std::swap(items[i - 1], items[static_cast<std::size_t>(pick)]);
        }
    }
    auto visible = Json::array();
    for (const auto& item : items) {
        if (section.at("type") == "likert") {
            visible.push_back({{"id", item.at("id")}, {"text", item.at("text")}});
            continue;
        }
        auto choices = Json::array();
        for (const auto& option : item.at("options"))
            choices.push_back({{"id", option.at("id")}, {"text", option.at("text")}});
        visible.push_back({{"id", item.at("id")}, {"text", item.at("scene")}, {"choices", choices}});
    }
    return visible;
}
} // namespace

Json read_reply_json(std::string_view reply) {
    // ```json ... ``` の囲みがあれば中身だけにする (後ろに付く説明文も落とす)。
    if (const auto fence = reply.find("```"); fence != std::string_view::npos) {
        const auto body = reply.find('\n', fence);
        const auto close = body == std::string_view::npos ? std::string_view::npos : reply.find("```", body);
        if (close == std::string_view::npos) throw InputError("unterminated code fence in reply");
        reply = reply.substr(body + 1, close - body - 1);
    }
    const auto open = reply.find('{');
    const auto end = reply.rfind('}');
    if (open == std::string_view::npos || end == std::string_view::npos || end < open)
        throw InputError("reply has no JSON object");
    auto parsed = read_json(reply.substr(open, end - open + 1));
    // 設問の並び {"items":[{"id":..,"value":..}]} で返ってきたら {id: value} に直す。値の検査はしない。
    if (parsed.is_object() && parsed.size() == 1 && parsed.contains("items") && parsed.at("items").is_array()) {
        auto mapped = Json::object();
        for (const auto& item : parsed.at("items")) {
            if (!item.is_object() || !item.contains("id") || !item.at("id").is_string() || !item.contains("value"))
                throw InputError("reply items need id and value");
            mapped[item.at("id").get<std::string>()] = item.at("value");
        }
        return mapped;
    }
    return parsed;
}

bool valid_section_answers(const Json& section, const Json& answers) { return ::augur::invoke_observed(::augur::ContractSpec{"C-15", "cce44dea", "src/questionnaire_prompts.cpp:46"}, hi::contracts::questionnaire_validation_check, [&]() -> bool { /* augur-inject:contract-wrap:cce44dea */
    if (!answers.is_object()) return false;
    const auto& items = section.at("items");
    for (const auto& [id, value] : answers.items()) {
        const auto item = std::ranges::find_if(items, [&](const auto& entry) { return entry.at("id") == id; });
        if (item == items.end()) return false;
        if (section.at("type") == "likert") {
            if (!value.is_number_integer() || value < 1 || value > 4) return false;
        } else if (!value.is_string() || !std::ranges::any_of(item->at("options"),
            [&](const auto& option) { return option.at("id") == value; })) return false;
    }
    return true;
}, section, answers); /* augur-inject:contract-end:cce44dea */ }

Json generate_life_script_answers(const Json& questionnaire, const Json& persona, const Invoke& invoke) {
    auto answers = Json::object();
    auto forced = Json::object();
    for (const auto& section : questionnaire.at("sections")) {
        const auto likert = section.at("type") == "likert";
        Json input{{"persona", persona_context(persona)}, {"instructions", questionnaire.at("instructions")},
                   {"items", section_items(questionnaire, section)}};
        if (likert) input["choices"] = questionnaire.at("scale").at("labels");
        else input["instruction"] = section.at("prompt");
        const auto& first = section.at("items").at(0);
        auto example = Json::object();
        example[first.at("id").get<std::string>()] = likert ? Json(3) : first.at("options").at(0).at("id");
        const auto format = std::string(likert
            ? "\nすべての設問に、設問 ID をキー・整数 1〜4 を値にした JSON オブジェクト 1 つで答えてください。"
            : "\nすべての設問に、設問 ID をキー・選択肢 ID (文字列) を値にした JSON オブジェクト 1 つで答えてください。")
            + "形の例: " + example.dump() + "。JSON だけを返し、コードブロック・配列・説明文は付けないでください。";
        const auto result = read_reply_json(invoke(std::string(persona_instruction) + format + "\n" + input.dump()));
        if (!valid_section_answers(section, result)) throw InputError("invalid questionnaire section answers");
        auto& output = likert ? answers : forced;
        output.update(result);
    }
    return {{"questionnaire", questionnaire.at("id")}, {"revision", questionnaire.at("revision")},
            {"answers", answers}, {"forcedChoice", forced}};
}

Json generate_reaction_examples(const Json& topics, const Json& persona, const Invoke& invoke) { return ::augur::invoke_observed(::augur::ContractSpec{"C-17", "4444e919", "src/questionnaire_prompts.cpp:81"}, hi::contracts::questionnaire_followups_check, [&]() -> Json { /* augur-inject:contract-wrap:4444e919 */
    auto examples = Json::array();
    for (const auto& topic : topics) {
        const Json input{{"persona", persona_context(persona)}, {"ask", topic.at("ask")}};
        const auto reply = read_reply_json(invoke(std::string(persona_instruction)
            + "\n質問への短い具体的な答えを {\"answer\": \"...\"} の形の JSON だけで返してください。"
              "コードブロック・説明文は付けないでください。\n" + input.dump()));
        if (!reply.is_object() || reply.size() != 1) throw InputError("invalid questionnaire followup answer");
        auto answer = required_string(reply, "answer");
        if (answer.size() > 2048 || answer.find_first_not_of(" \t\r\n") == std::string::npos)
            throw InputError("invalid questionnaire followup answer");
        examples.push_back({{"ask", topic.at("ask")}, {"answer", std::move(answer)}});
    }
    return examples;
}, topics, persona, invoke); /* augur-inject:contract-end:4444e919 */ }
} // namespace hi
