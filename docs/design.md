# 詳細設計(current-swimmer)

今のコードから起こした構成。未検証のものは最後にまとめる。

## ファイルと責務

| ファイル | 責務 |
|---|---|
| `src/fluid.c` / `fluid.h` | 48×48 の 2 次元非圧縮流体(Stable Fluids、自由滑り壁) |
| `src/swimmer.c` / `swimmer.h` | 直径 0.06 の円盤の泳者、推力・抗力・流体への運動量 |
| `src/view.c` | ブラウザーの自由遊泳(`_view_*`) |
| `src/journey.c` / `journey.h` | 物語 Upstream。物理は変えず、噴流の強さとスタミナの収支、区間の目印・輪・場面だけを持つ |
| `src/journey_web.c` | Upstream の状態をブラウザーへ 33 個の float で渡す(`_jw_state`) |
| `src/main.c`・`render.c`・`svg.c`・`gif.c` | 端末表示・CSV/SVG・GIF |
| `web/app.js` | 台詞、描画(canvas 2D)、入力(キー・ボタン・水面のタッチ)、`window.swimmerView` と `window.__tmGate` |
| `web/index.html`・`style.css` | 画面の配置(PC・縦持ち・横持ち) |
| `tests/story_policy.h` / `story_run.h` / `story_bench.c` / `test_story.c` | 人に近い自動の泳者、bench の JSON、物語の検査 |

## 画面の状態

`mode`: `title` → `swim`(Enter the water)/ `demo`(Watch a swim)/ `journey`(Swim upstream)。
`swim` と `demo` からは Swim upstream ボタンで `journey` へ。`journey` の中は `journeyState()[11]` の場面番号
(`JS_OUTLET` … `JS_SPRING`、-1 は泳いでいる間)。結末の場面のボタンで最初から。
`paused` は Pause ボタン・`P`・フォーカスを失う・画面が隠れると真、ボタンか水面のタップで戻る。
旅の途中の Start the journey again は 1 回目で「Tap again to restart」になり、3 秒以内のもう 1 回で始め直す。

## 物語の進行(`journey_step`)

- 区間ごとに目印(`mark_x/y`)を順に通る。水路の 2 つ目(堰)と、残った道の第 5 区間の 1 つ目(葦)は
  「輪」: 半径 0.14 の中にいる間だけ `held` が増え、必要秒(堰 18 秒・葦 22 秒)に届くと次へ。時計と流れは止まらない。
- 第 5 区間の堰板(`JS_SURGE`)は、去った道は x ≥ 0.5、残った道は葦の輪を終えた時に開く。
- 時計 `time` は泳いだステップ × 0.02 秒だけで増える(一律の加算は無い)。

## 入力の割り当て

| 入力 | 結果 |
|---|---|
| `W` / `Space` / `↑`、Stroke ボタン | 尾を打つ(`stroke = 1`) |
| `A` / `D` / `←` / `→`、Left / Right ボタン | 向きを変える(`turn = ±1`) |
| 水面のタップ(300 ms 未満・12 px 未満の移動) | 目的地を置く。向きの誤差 × 2.5 で曲がり、0.6 秒打って 0.26 秒滑る。着く(0.04 以内)か 8 秒で終わる |
| 水面の押したまま・なぞり | 指へ向かう。離すとタップの目的地へ戻る |
| 輪の中 | タップの目的地では尾を打たず滑る(押したままなら輪の外の指へは打つ) |

## canvas

canvas は箱の CSS 寸法 × 端末の画素密度(上限 2)。単位正方形の池を短い辺に合わせて中央に描き、
外側は岸として暗くする。縦持ちは幅いっぱい・高さ min(122vw, 62svh)、横持ちは左に池・右に操作。
canvas に書く文字は渦度の極値の `+` / `−` だけで、18 × 密度 px(CSS で 18 px)。

## test hook

- `window.swimmerView`(読むだけ): モード・物理の値・Upstream の場面や輪の進み。`make smoke` が使う。
- `window.__tmGate`(遊べる門の共通形、読むだけ): `logical`(canvas の画素)、`scene()`、`goal()`(Upstream の第 1 区間を抜けた)、
  `texts()`(このフレームで canvas に書いた文字と px)、`player()`(泳者の canvas 座標)、`hint()`(画面を読む人が次に押す場所:
  タイトルの Swim upstream、場面の最初の答え、一時停止中の水面、点線の輪)。

## 保存

なし。

## 既知の限界

- 実 iPhone・Safari・実 GPU・人の初見試遊は未検証(Chromium の端末エミュレーションと SwiftShader で測った)。
- 自動の泳者は輪の中では打たずに滑る。人がどれくらい輪にとどまれるかは未検証。
- ビート表の感情値は別担当の見直し(#7)の後に変えていない。
