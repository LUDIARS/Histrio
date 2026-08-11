---
task: histrio-bootstrap
project: Histrio
kind: 実装
created: 2026-08-12
memory_links:
  - spec/feature/persona-chat-engine.md
---
# Histrio 骨格 + persona store + 描画系移植

## 目的

spec/feature/persona-chat-engine.md §4 の移行 1〜2 を実装する。HTTP server (loopback)、
Excubitor catalog 登録、health、persona store (SQLite) を立ち上げ、Concordia の
persona / rules / chat (render・responder) を Histrio へ移植して blackbox-chat-engine の
実装を Histrio 側で完成させる。

## 完了条件

- loopback HTTP server + `/health` が動き、port が Excubitor catalog で採番・登録されている
  (ハードコード無し)。
- persona store (SQLite) が Cc の personas / persona_feedback_log をインポートでき、
  `GET/POST /v1/personas` が namespace (`cc`/`di`/`shared`) 付きで動く。
- 中央 Haiku 描画 (`/v1/render`: persona id + intent + 文脈 → 1 発話) が stateless に動き、
  設定不備は error ログ + スキップ (無言フォールバック禁止)。
- rules engine (tick/event/cooldown・決定的 decide・`PUT /v1/rules` + 決定的レビュー) が
  Histrio 内で動く。
- Cc 側イベントフィードへの接続クライアント (事前共有トークン・再接続バックオフ) がある
  (Cc 側 module は別 task)。
- 上記が Vitest でカバーされる。

## スコープ (編集可ディレクトリ)

- Histrio リポ全体 (新規実装)
- Concordia のコードは変更しない (参照コピーのみ。Cc 側配線は Concordia の histrio-feed task)
