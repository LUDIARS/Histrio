# Histrio

Histrio は Concordia と Discutere が共用する C++23 のペルソナ・会話サービスです。SQLite で人格と脚本を管理し、決定的な判断処理で会話操作を選びます。発話の描画にはローカルの Claude CLI を使います。

- [ペルソナ・会話エンジン](spec/feature/persona-chat-engine.md)
- [ロールモデル](spec/feature/role-model-engine.md)
- [人生脚本と OK 牧場](spec/feature/virtual-persona-life-script.md)

## ビルドと検証

Windows では VS2026、MSVC 14.52、CMake 4.2 以上、OpenSSL 3 を使います。Lorica は Discutere の互換版 `5e8281b0e60fa16f814e16eaa620715937e8618d` を `LORICA_SOURCE_DIR` で指定するか、同版の CMake パッケージを用意します。

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -T version=14.52 -DLORICA_SOURCE_DIR=<Loricaの互換版checkout>
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

CMake が固定版の Lapilli、Boost、nlohmann/json、SQLite ソースを取得します。オフラインでは `FETCHCONTENT_SOURCE_DIR_LAPILLI`、`FETCHCONTENT_SOURCE_DIR_BOOST`、`FETCHCONTENT_SOURCE_DIR_JSON`、`FETCHCONTENT_SOURCE_DIR_SQLITE` に展開済みソースを指定できます。SQLite の配布アーカイブから C の amalgamation をコンパイルします。

テストは一時 SQLite DB、偽の描画クライアント、フィードの応答 fixture を使用します。

## 設定

- 実行ファイルは `build/bin/histrio-server.exe`。サービス catalog のポートを CMake の構成時に取り込み、IPv4 ループバックに bind します。`HISTRIO_PORT` で上書きできます。
- `HISTRIO_DB` は DB のパス。既定は Windows の `%LOCALAPPDATA%/Histrio/histrio.sqlite`、その他では `$XDG_STATE_HOME/Histrio/histrio.sqlite` または `~/.local/state/Histrio/histrio.sqlite` です。`HISTRIO_DATA_DIR` の既定は作業ディレクトリの `data` です。
- `HISTRIO_RENDER_MODEL` は描画モデルの役割名または明示的な CLI モデル指定。既定の役割は `haiku` です。`HISTRIO_CLAUDE_PATH` で実行ファイルを指定できます。
- `HISTRIO_CC_FEED_URL` と Vault から渡す `HISTRIO_CC_FEED_TOKEN` を揃えると、Cc フィードを購読します。`HISTRIO_RULES_DISABLED=1` でルールを無効化できます。
- HTTP の Host はループバック名と `LUDIARS_ALLOWED_HOSTS` を許可します。ブラウザーの Origin 付きリクエストは拒否します。

Cc フィードはループバックの設定済み URL に `GET` と Bearer トークンを送ります。JSON の `cursor` と `events` を受け、次回の `cursor` をクエリに付けます。各イベントには `type` と任意の `active_session_count` が入ります。Cc 側のモジュール実装時にこの接続仕様を照合してください。

## API

各 API の namespace はクエリ `?namespace=cc`、`di` または `shared` で指定します。既定は `shared`。人格・脚本は同じ namespace の値を優先し、なければ `shared` を参照します。

| メソッドとパス | 入出力 |
| --- | --- |
| `GET /health` | `{"status":"ok"}` |
| `GET /v1/personas` | 人格一覧 |
| `POST /v1/personas` | `id` をキーに更新。任意の `seed`、`signals`、`script_pattern_id`、声色などを受け取る |
| `GET /v1/personas/:id/profile` | `script_pattern_id`、版・表示名、エゴグラムなど |
| `GET /v1/personas/:id/learning` | 現在版のロール重みと反応表の補正値 |
| `POST /v1/personas/:id/decide` | `messages`、任意の `event`、`situation`、`topic` を受け取り、根拠と判断 ID を保存。`incremental: true` は同じ話題の新規メッセージだけを渡す |
| `POST /v1/render` | `persona_id`、`intent`、`context` と任意の `decision_id` から発話を 1 件描画。`skipped` は静観または描画不可を表す |
| `GET /v1/decisions/:id` | 判断の根拠・版・学習履歴 |
| `POST /v1/decisions/:id/feedback` | 一意な `id` と `kind` を受け、上限付きで学習 |
| `GET /v1/rules` / `PUT /v1/rules` | `rules` 配列と任意の `role_matrix`。違反を決定的に拒否 |
| `GET /v1/script-patterns` / `PUT /v1/script-patterns` | カタログを取得。更新は `version` と `patterns` を渡し、既存版の変更は拒否 |

`histrio-server.exe import-cc <sqlite-file>` は明示指定した旧 DB を読み取り専用で開き、人格とフィードバックを `cc` に移行します。同じインポートをやり直しても既存の人格や履歴を上書きしません。
