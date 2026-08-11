# Histrio

ペルソナ + 会話エンジン。Concordia から persona / チャット判定・描画系を切り出した独立サービス。

- **persona 正本**: 生成・session-end 学習 (heuristic、LLM 不使用)・フィードバック収集 (ぼやき含む)
- **発話判定**: rules engine (tick/event/cooldown)・決定的 decide・注入ルールの決定的レビュー
- **発話描画**: 中央 Haiku 描画 (render/responder)
- **アンビエント参加**: opt-in チャンネルの会話傍聴 → 参加判定 + 発話案
- **汎用 API**: Concordia のチャット面だけでなく、Discutere の討論・議論ペーパー生成にも persona/描画を提供する

Concordia は AI エージェントとワークフローの補佐に比重を置き、会話・人格の比重は Histrio が持つ。
プラットフォーム接続 (Discord/Slack Gateway) は Concordia 側に残り、Histrio は
イベントフィード購読 + 投稿 API 呼び出しで連携する (相互に落ちても縮退動作)。

設計正本: [spec/feature/persona-chat-engine.md](spec/feature/persona-chat-engine.md)
