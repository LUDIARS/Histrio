#include "fixtures.hpp"

#include <iostream>
#include <map>

int main(int argc, char** argv) {
    using namespace hi::test;
    const std::map<std::string_view, void(*)()> tests{
        {"health", health_test}, {"personas", personas_test}, {"import", import_test},
        {"render", render_test}, {"rules", rules_test}, {"feed", feed_test},
        {"bootstrap_contract", bootstrap_contract_test}, {"profile", profile_test},
        {"situation", situation_test}, {"role", role_test}, {"observe", observe_test},
        {"learning", learning_test}, {"pattern", pattern_test}, {"stance", stance_test},
        {"corral", corral_test}, {"constructive", constructive_test}, {"privacy", privacy_test},
        {"audit", audit_test}, {"versions", versions_test}
    };
    if (argc != 2 || !tests.contains(argv[1])) return 2;
    try {
        tests.at(argv[1])();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
