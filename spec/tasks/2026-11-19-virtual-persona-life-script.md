---
task: virtual-persona-life-script
project: Histrio
kind: 実装
created: 2026-11-19
memory_links:
  - spec/feature/virtual-persona-life-script.md
  - spec/feature/role-model-engine.md
  - spec/feature/persona-chat-engine.md
---
# 仮想人格実装 (人生脚本パターン × OK牧場)

## 目的

spec/feature/virtual-persona-life-script.md を実装する。persona ごとに脚本パターンを 1 個
seed から決定的に割り当て、議論内容の features → 反応表 → 立場ベクトルを導き、OK牧場の
自他象限を認識して会話操作とロールを選ぶ。判断ログとフィードバックでパターンを作りこむ。

## 完了条件

- 全人格が脚本パターンを 1 個、seed から決定的に持ち、既存 persona も移行で再現できる。
- 立場ベクトルは反応表が主因で同一入力に同一ベクトル。エゴグラムを変えてもベクトルは変わらない。
- 象限推定と操作選択が決定的で、LLM の追加呼び出しをしない (描画 Haiku への相乗りのみ)。
- get-rid-of は常に建設的反論として描画され、人間を否定・ラベリングする発話が出ない。
- 相手の象限推定を人間の属性として保存しない。
- `persona_decision_log` から発話の根拠 (パターン → ベクトル → 象限 → 操作 → ロール) を再現できる。
- 学習による変動が上限付きで、`script_patterns` の版と変更履歴を監査できる。
- 上記が Vitest でカバーされ、spec §7 の受け入れ基準 7 項目が全て検証される。

## スコープ (編集可ディレクトリ)

- Histrio リポ全体

## 依存

- histrio-bootstrap (persona store / 描画系) の後続。
- role-model-engine の後続で、同 task の脚本層・ロール選択関数を本 task に統合する。
