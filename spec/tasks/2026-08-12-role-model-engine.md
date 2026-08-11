---
task: role-model-engine
project: Histrio
kind: 実装
created: 2026-08-12
memory_links:
  - spec/feature/role-model-engine.md
  - spec/feature/persona-chat-engine.md
---
# ロールモデルエンジン実装 (人生脚本 × 状況対応)

## 目的

spec/feature/role-model-engine.md をフルセットで実装する。persona の脚本プロファイル
(エゴグラム/ドライバー/基本的構え/脚本傾向) を seed から決定的に導出し、状況分類 ×
プロファイル × 学習重みでロールモデルを決定論的に選択、描画 intent/tone と会話ポリシーへ渡す。

## 完了条件

- `persona_profiles` が seed から決定的に導出され、同 seed で再現可能。
- 状況分類 (発散/収束/停滞/対立/祝福/雑談) が buffer 統計のみで動き、LLM を追加で呼ばない。
- score 関数によるロール選択が乱数・時刻依存なしで、同一入力に常に同一結果。
  「静観」選択時は発話しない。
- matrix/重みが設定データとして注入可能 (rules の決定的レビュー経由)。
- feedback の重み畳み込みが変動上限付きで動き、変更履歴が監査保存される。
- WebUI (または API) で現在のプロファイル・重み・選択根拠を参照できる。
- 上記が Vitest でカバーされ、spec の受け入れ基準 5 項目が全て検証される。

## スコープ (編集可ディレクトリ)

- Histrio リポ全体 (histrio-bootstrap の後続。persona store スキーマへの追加を含む)
