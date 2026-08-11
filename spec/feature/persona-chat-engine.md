---
type: feature
title: "Histrio — ペルソナ + 会話エンジン (Concordia からの切り出し)"
description: "Concordia の persona エンジンとチャット判定・描画系を独立サービス Histrio へ切り出す設計正本。Cc とはイベントフィード + 投稿 API で密連携しつつ一方向依存を排し、Discutere の討論生成にも同じ persona/描画 API を提供する。"
service: histrio
domain: chat-brain
tags:
  - persona
  - llm
  - rule-engine
  - relay
  - extraction
status: planned
related:
  - ../../..//Concordia/spec/feature/blackbox-chat-engine.md
  - ../../..//Concordia/spec/feature/boyaki-channel.md
  - ../../..//Concordia/spec/feature/ambient-listener.md
  - ../../..//Concordia/spec/feature/participants.md
updated: 2026-08-12
---

# Histrio — ペルソナ + 会話エンジン

## 0. 切り出しの理由 (2026-08-12 neco 決定)

Concordia は AI エージェントとワークフローの補佐が役割であり、サービスとして会話・人格に
比重を置かない。persona エンジンとチャットの仕組みは独立サービス Histrio に移し、
サービス間は密な関係を保つ。Discutere (議論・討論ペーパー生成) にも応用が効く形にする。

## 1. 責務分割

### Histrio が持つ (会話の頭脳)

| 領域 | 内容 | 由来 (Cc) |
|---|---|---|
| persona 正本 | seed + signals からの heuristic 生成・session-end 学習・`persona_feedback_log` (ぼやき収集含む) | `src/personas/` |
| 発話判定 | rules engine (tick/event/cooldown)・決定的 decide・`PUT /v1/rules` 注入 + 決定的レビュー | `src/rules/` |
| 発話描画 | 中央 Haiku 描画 (render/render-config/responder)。設定不備の無言フォールバック禁止 | `src/chat/` (blackbox-chat-engine spec) |
| 会話ポリシー | channel 別返信確率・quiet-hours・MAX_REPLY_DEPTH・mute/コスト停止 | dispatcher の判定部 |
| アンビエント | buffer/gate/judge (ambient-listener spec の実装先を Histrio に変更) | 新規 |
| 汎用描画 API | persona 指定で発話/文章を描画する stateless API (Di の討論生成向け) | 新規 |

### Concordia に残る (プラットフォームと工程)

- Discord/Slack 接続・session channel routing・ingress/egress・webhook identity・participants
  (Gateway 本社 1 本の federation 方針を維持)
- セッション帰属発話のタスク注入の実行系 (セッション管理と不可分)
- Director / delegation / inbox / セッション lifecycle — ワークフロー補佐の本体

## 2. 連携契約 (密だが一方向依存なし)

```
Cc ──(イベントフィード WS/HTTP push)──▶ Histrio
     chat.posted / session lifecycle 要約 / ambient 対象 channel の人間発言

Histrio ──(既存 API 呼び出し)──▶ Cc
     POST /v1/chat            persona 発話の投稿 (中央 Haiku 系統)
     タスク注入 API            セッション帰属発話の依頼 (chitchat-suggest / chat-reply 等)
```

- 双方 loopback。port は Excubitor catalog で採番・登録する (ハードコード禁止)。
- **縮退動作**: Histrio 停止時、Cc は会話・persona 機能なしで完動する (会話イベントは
  流さず捨てるか短期バッファ)。Cc 停止時、Histrio は Di 向け API のみで完動する。
- イベントフィードは Cc 側 module-manifest の 1 モジュール (`histrio-feed`) として実装し、
  off で完全停止できる。
- 認証: loopback 限定 + 事前共有トークン (federation の secureValuesMatch パターンを流用)。

## 3. Discutere への提供

- `POST /v1/render` — persona id + intent + 文脈を渡すと persona の声色で 1 発話を描画する
  stateless API。Di の討論・議論ペーパーの話者描画が呼ぶ。
- `GET/POST /v1/personas` — persona の trait/記憶の読み書き。namespace (`cc` / `di` / `shared`)
  で用途を分離し、Di 既存のペルソナエンジン v2 (Vo/Di、T1-T16 マージ済) との統合方針は
  Di 側 spec で別途確定する (Histrio 側は namespace を用意するまで)。

## 4. 移行方針

1. **Histrio 骨格**: HTTP server + Excubitor catalog 登録 + health + persona store
   (SQLite、Cc の `persona_feedback_log` 等をインポート)。
2. **描画系移植**: chat/render・responder・rules を Cc からコピーして Histrio で稼働。
   blackbox-chat-engine spec の実装は最初から Histrio で行う (Cc には実装しない)。
3. **Cc 側配線**: イベントフィード module + `POST /v1/chat` 受け口はそのまま。dispatcher の
   判定部を Histrio 呼び出しへ置換し、タスク注入の実行系だけ残す。
4. **アンビエント**: ambient-listener を Histrio 実装に変更 (Cc はフィードで対象 channel の
   発言を流すだけ)。
5. **Di 連携**: `/v1/render` 公開と Di 側の呼び出し spec。

旧 Cc 実装 (`src/personas/` `src/rules/` `src/chat/`) の撤去は Histrio 安定稼働の確認後に
独立 PR で行う (二重稼働期間中は Cc 側を既定 OFF)。

## 5. 受け入れ基準

- [ ] Histrio 停止時に Cc が会話機能なしで完動し、error スパムを出さない。
- [ ] Cc 停止時に Histrio の `/v1/render` が単体で応答する (Di 用途)。
- [ ] persona 正本が Histrio に一本化され、Cc 側に書き込み経路が残らない。
- [ ] ambient / blackbox-chat-engine / boyaki の実装が Histrio 側にあり、Cc には
      フィード + 投稿受け口だけがある。
- [ ] port / URL の正本が Excubitor catalog にあり、ハードコードが無い。
