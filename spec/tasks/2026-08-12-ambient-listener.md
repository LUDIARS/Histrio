---
task: ambient-listener
project: Histrio
kind: 実装
created: 2026-08-12
memory_links:
  - spec/feature/persona-chat-engine.md
  - ../../../Concordia/spec/feature/ambient-listener.md
---
# アンビエントリスナー実装 (Histrio 側: buffer/gate/judge)

## 目的

Concordia spec/feature/ambient-listener.md の実装先を Histrio に変更して実装する
(2026-08-12 の切り出し決定による。Concordia の 2026-08-11-ambient-listener task を置換)。
Cc のイベントフィードから allowlist channel の人間発言を受け、決定的ゲート → 中央 Haiku
1 呼び出しの参加判定 → Cc の投稿/タスク注入 API 呼び出しで自然参加する。

## 完了条件

- Concordia spec/feature/ambient-listener.md の受け入れ基準 5 項目を Histrio 実装で満たす
  (allowlist 外を読まない・ゲート外で Haiku を呼ばない・join=false 無言・
  route_to_session はタスク注入・人間の応答まで連続参加しない)。
- ring buffer はプロセス内メモリのみ。会話本文・draft・reason を DB / ログへ保存しない。
- 参加判定はロールモデルエンジンの状況分類・ロール選択を通る (「静観」で発話しない)。
- allowlist (`ambient_channels`) は Histrio が正本で持ち、Cc 側フィードの購読対象と
  同期される。
- 上記が Vitest でカバーされる。

## スコープ (編集可ディレクトリ)

- Histrio リポ全体 (histrio-bootstrap / role-model-engine の後続)
- Concordia 側のフィード配線は Concordia の histrio-feed task で行い、本 task では触れない
