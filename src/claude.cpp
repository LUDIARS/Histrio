#include "service.hpp"

#include <cctype>

#include <lapilli/one_shot.hpp>

namespace hi {
Invoke claude_client(std::optional<std::string> role) {
    return [role = std::move(role)](std::string_view prompt) -> std::string {
        namespace cli = lapilli::one_shot;
        cli::PrepareOptions config;
        config.env = lapilli::process::current_environment();
        const auto model = role.value_or(lapilli::process::env_get(*config.env, "HISTRIO_RENDER_MODEL").value_or("haiku"));
        const auto executable = lapilli::process::env_get(*config.env, "HISTRIO_CLAUDE_PATH").value_or("claude");
        if (model.empty() || executable.empty()) throw std::runtime_error("empty Claude configuration");
        auto launch = cli::prepare(executable,
            {"-p", "--model", model, "--output-format", "json", "--tools=",
             "--strict-mcp-config", "--disable-slash-commands", "--safe-mode"}, config);
        // The service token and arbitrary Vault-injected settings never enter the child environment.
        auto& env = launch.env;
        std::erase_if(env, [](const auto& entry) {
            auto name = entry.first;
            std::ranges::transform(name, name.begin(), [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
            constexpr std::array allowed{"PATH", "PATHEXT", "SYSTEMROOT", "WINDIR", "COMSPEC", "HOME", "USERPROFILE",
                "APPDATA", "LOCALAPPDATA", "TEMP", "TMP", "TMPDIR", "LANG", "LC_ALL", "CLAUDE_CONFIG_DIR",
                "CLAUDE_CODE_GIT_BASH_PATH"};
            return std::ranges::find(allowed, name) == allowed.end();
        });
        cli::RunOptions options;
        options.cwd = std::filesystem::temp_directory_path();
        // 配列形式の出力は init イベント (プラグイン一覧など) を含むので、本文より大きくなる。
        options.max_stdout = 1024 * 1024;
        options.max_stderr = 4096;
        // アンケートの 1 セクション (最大 20 問) を 1 回で答えさせるため、描画より長く待つ。
        options.timeout = std::chrono::seconds(120);
        const auto reply = cli::run(launch, prompt, options);
        if (!reply.ok()) throw std::runtime_error("Claude CLI invocation failed");
        return claude_result_text(reply.out);
    };
}

std::string claude_result_text(std::string_view output) {
    const auto parsed = read_json(output);
    const Json* envelope = nullptr;
    if (parsed.is_array()) {
        for (const auto& event : parsed) {
            if (event.is_object() && field(event, "type", "") == "result") envelope = &event;
        }
    } else if (parsed.is_object()) {
        envelope = &parsed;
    }
    if (envelope == nullptr || envelope->value("is_error", false) || field(*envelope, "subtype", "success") != "success")
        throw std::runtime_error("Claude CLI returned an error");
    return required_string(*envelope, "result");
}
} // namespace hi
