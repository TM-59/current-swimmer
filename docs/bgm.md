# BGM(M63 #33、親 TM-59/project-context#598)

方針(project-context 20-stacks.md「ゲームの BGM」): 基調はバラード。戦闘の役と物語は別の曲で、場面の切り替えで替える。歌は文字を読まない場面だけ。

## 鳴らす経路

ブラウザー版(C の物理を Emscripten で動かす頁)だけに Web Audio の再生を足した。物理は C のまま、JS は画面から曲を選ぶだけ。
端末版・CSV/SVG/GIF は無音のまま。円柱の後流の頁 `wake.html` は物語の無い計測の頁なので鳴らさない。
曲は最初の操作の後に `bgm/list.json` と `bgm/<曲>.ogg` を読む(初期ロードに入れない)。読めない・復号できない曲は無音のまま遊びを止めない。

## 場面 → 曲

| 画面 | 曲 | 強さ |
|---|---|---|
| タイトル・自由泳ぎ(Enter the water)・実演(Watch a swim) | pond(基調のバラード) | 1 |
| Upstream の 5 区間を泳ぐ間 | upstream(戦闘の役、pulse) | 1 |
| Upstream の場面(outlet・culvert・eddy・warm・taste) | story | 0.7 |
| 重い場面(reeds・surge) | story | 0.4 |
| 泉の場面(結末、spring) | spring | 1 |

戦う相手・主戦は無い。流れに逆らって泳ぐ区間を戦闘の役にした。場面は選択を押すまで止まる(時間で流れない)。判定は `web/bgm.js` の `cue()`(画面の mode・場面・重さ・一時停止だけを読む)。

- 切り替えは 2.5 秒の交差フェード。一時停止は 0.4 倍。
- **全部器楽**: どの場面も文字か計器を読むので、歌入りは作らない。
- 消音: 見出しの ♪(44px)と `M`。保存はしない(この repo は保存機能を持たない)。消音は出口(master)でも切るので、交差フェードで消えていく前の曲も同時に黙る(TM-59/project-context#668 の穴を最初から作らない)。

## 曲(採用・門の値)

| 場面(曲) | 鳴る画面 | 門 | 採用 | 通過 | ループ | LUFS / 山 | 継ぎ目の音色 | 落ち込み / 静かな窓 | 大きさ |
|---|---|---|---|---|---|---|---|---|---|
| pond | 基調(タイトル・自由泳ぎ・実演) | soft | pond-c0-s903 | 11/12 | 28.235 秒 | -15.9 / -1.9 dBFS | 0.6 | 0.0 秒 / 0.0 | 650,932 B |
| upstream | 戦闘の役(Upstream の 5 区間を流れに逆らって泳ぐ間) | pulse 105.3 BPM(計画 104) | upstream-c1-s902 | 8/12 | 37.145 秒 | -15.9 / -1.9 dBFS | 0.49 | 0.0 秒 / 0.0 | 919,093 B |
| story | 物語(Upstream の場面。重い場面 reeds・surge と一時停止は 0.4 倍) | soft | story-c1-s901 | 8/12 | 24.828 秒 | -16.0 / -1.7 dBFS | 0.61 | 0.0 秒 / 0.0 | 551,197 B |
| spring | 結末(泉に着いた後の場面) | soft | spring-c0-s903 | 12/12 | 22.5 秒 | -15.9 / -1.8 dBFS | 0.53 | 1.0 秒 / 0.044 | 519,807 B |

計 2,641,029 B(4 曲)。通過 39/48。全候補の値は `docs/bgm/gate.md`、生の値は `docs/bgm/report.json`。

## 作り方

1. 計画: `docs/bgm/plan.json`(場面 × caption 2 × seed 6、seed 901〜906。他の作品と同じ caption・seed を使わない)。caption は音の特徴だけで、曲名・作者・作品名を書かない。
2. 生成: ACE-Step 1.5 turbo(MIT、steps 8)を Colab の L4 で 1 回の確保(計算枠 587.75 → 587.62、端末からの 1 回 284.9 秒、48 本)。
   `cd ~/dev/local-llm-ops && .venv/bin/python -m tools.colab bgm --repo <この repo> --out <dir> --backend colab --gpu L4`
   Colab が使えない時の落ちる先は `scripts/bgm_gen.py`(手元の CPU、local-llm-ops の music venv)。
3. 門と書き出し: `~/.local/share/local-llm-ops/genvenv/music/bin/python scripts/bgm_bake.py --takes <dir>`(ffmpeg が要る)。
   門は spot-gap #33・td-games #38 と同じ値で緩めていない: soft(バラード・物語・結末)は local-llm-ops `tools/genmodels/mood.py` の soft の門、pulse(戦闘の役)は PULSE_GATE + テンポ ±5%、全部に -16 LUFS ±1.5・山 -1 dBFS・継ぎ目の音色の跳び 2 倍以下・12 dB の落ち込み 2 秒以下、pulse は静かな窓の割合 0.10 以下、soft のループは 16 秒以上。
   この repo の scripts/ は Ruff ALL・mypy strict の対象なので、spot-gap の `scripts/bgm_bake.py` を同じ門のまま書き直した(soundfile・matplotlib を使わず ffmpeg で読み書き・作図)。書き直しの確かめ: 同じ 48 本に spot-gap の元の script を掛け、48 本とも通過・落ちる理由が一致した(採用は pond だけ継ぎ目の値 0.60 / 0.61 の差で別の候補)。
4. 出所と利用条件: 曲は ACE-Step 1.5(MIT、出力に第三者の権利の主張なし)で手元の計画から生成した独自の音源。第三者の音源・歌詞・旋律は使っていない。外部の生成 AI・API key は使っていない。

## 波形とスペクトル(自分で見た所見)

画像は 2 周つないだ物で、真ん中が継ぎ目(上が波形、下が対数の周波数のスペクトル)。

- `pond`: 頭の 3.5 秒は音数が少なく、そこから和音が厚くなる形が 1 周ごとに繰り返す。途切れ・雑音の帯は無い。継ぎ目は静かな頭へ戻る所で段差は見えない。
- `upstream`: 拍が等間隔に最後まで続き、落ち込みは無い。ループの後ろ半分で高い帯(打楽器)が増え、継ぎ目で元に戻る(音色の跳び 0.49)。
- `story`: 音数の少ない旋律が切れずに続き、句の間で音量が下がるが無音にはならない。高い帯は薄い。
- `spring`: 句ごとに膨らんで静まる遅い形。句の間が最も静か(落ち込み 1.0 秒、門 2 秒の内)。継ぎ目は句の切れ目に来る。

人の試聴は門にしない(客観の門を通した物だけを採る)。

## 試験

- `node scripts/bgm-smoke.mjs`(ローカル専用、port `CS_BGM_PORT` 既定 22080、記録 `docs/bgm/smoke.json`):
- nothing fetched before the first press
- pond plays(出口 -28.4 dBFS)
- pause ducks(出口 -28.4 → -35.1 dBFS)
- story plays(出口 -24.7 dBFS)
- mute during a crossfade silences the fading track too(出口 -120 dBFS)
- upstream plays(出口 -20.9 dBFS)
- ♪ mutes and restores(出口 -120 → -32.8 dBFS)
- cues: heavy scenes story 0.4x, ending spring, paused reach 0.4x, demo pond
- dist bytes
- dist 3,047,673 B(10 MB の門の内。BGM 4 曲 2,641,029 B を含む)。
- 既存の `make test`(物理・GIF・物語・後流、`story_bench --check` 8 行一致)と `node scripts/smoke.mjs`(Upstream を最後まで泳ぐ実入力)は変わらず通る。
- 遊べる門(`bin/playability-gate.py`、main と branch を同じ時間帯 2026-11-27 08:34〜08:35): main 7/7・branch 7/7(②タップで進行 p 6.8 / l 6.7 秒 → 6.8 / 6.5 秒、⑤の最小は ♪ の 44×44)。
