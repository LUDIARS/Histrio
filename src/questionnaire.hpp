#pragma once

#include "service.hpp"

namespace hi {

struct LifeScriptData {
    Json questionnaire;
    Json bank;
};

struct QuestionnaireError : std::runtime_error { using std::runtime_error::runtime_error; };

inline constexpr auto questionnaire_prompt_revision = "persona-life-script-v1";
inline constexpr auto questionnaire_role = "haiku";

LifeScriptData load_life_script(const std::filesystem::path& directory);
Json score_life_script(const Json& questionnaire, const Json& answers);
Json plan_life_script_followups(const LifeScriptData& data, const Json& score);
Json questionnaire_assignment(const Json& seed, const Json& score);
bool questionnaire_eligible(const Json& persona);
bool valid_section_answers(const Json& section, const Json& answers);
Json generate_life_script_answers(const Json& questionnaire, const Json& persona, const Invoke& invoke);
Json generate_reaction_examples(const Json& topics, const Json& persona, const Invoke& invoke);

} // namespace hi
