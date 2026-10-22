# M4 アート方針と採否

2026-10-23。画風は「自然観察の図鑑を思わせるガッシュ、深い青緑の水、銅色のひれ」。

従来の泳者は端末/GIFで白い円と黄色い方位印、SVGでは黒い円だった。
タイトル、人物、敵、戦闘画面は存在しなかった。今回、原画を描画できる
ブラウザーの表示層を追加し、既存C11の流体・泳者をそのままWASMへコンパイルした。
生物の絵を置いたことによって、円盤の流体モデルが魚体の実形状モデルになった
わけではない。Model discで計算に使う半径を確認できる。

## 生成・採用

Codex built-in image_genで2枚生成し、2枚を目視して採用した。外部の既成素材は使用していない。
魚は頭を右に向けた俯瞰、左右のひれ、左の二叉の尾、透過を確認した。
水辺は中央を空けた構図、文字・魚の混入なしを確認した。1024角指定に対して
実出力の解像度はツールが決めたため、配布用に下表の寸法へ縮小した。
再描画・背景の塗り直しは行っていない。

| ファイル | 寸法・容量 | 採用した場所 |
|---|---|---|
| `assets/hero-fish.png` | 512×512、138,025 B、透過PNG | 実際に泳ぐスプライト、タイトル、紹介画像 |
| `assets/pond.jpg` | 1024×1024、147,422 B、JPEG | タイトルとプールの背景 |

魚は原画の縦の短冊をストローク中だけ少し曲げて描く。物理状態を変更する
アニメーションではない。背景の岩・水草に当たり判定はない。
画像が欠けた場合は警告と元のモデルを示す円・向きの線へ戻り、数値計算を継続する。

### 魚の生成指示（最終プロンプト）

```text
Use case: stylized-concept. Asset type: game character sprite for current-swimmer, an educational 2D fluid swimming sandbox. Create ONE original small fish seen strictly from directly ABOVE, head pointing to the RIGHT, entire fish visible centered with generous clear padding on all sides. Soft painterly natural-history gouache with crisp readable silhouette at 64 pixels, deep teal body with a single warm ivory stripe along its spine, copper orange paired pectoral fins and forked caudal tail on the LEFT. Symmetric left-right anatomy viewed from above, two tiny dark eyes near the head, elegant translucent fins, no humanoid features, no costume. Long axis horizontal, body about 65 percent of full image width, tail 15 percent, full silhouette fits well inside canvas. Isolated on genuinely transparent background, clean alpha including translucent fins. No ground, no water, no cast shadow outside silhouette, no text, no labels, no border, no logos, no existing franchise design. 1024 by 1024 square.
```

### 水辺の生成指示（最終プロンプト）

```text
Use case: stylized-concept. Asset type: environment artwork used directly under a scientific 2D swimming simulation and on its title screen. Create original painterly gouache illustration, strict overhead view looking straight down through clear deep blue-green pond water. Underwater rounded slate pebbles, delicate submerged aquatic plants and reeds ONLY at the outer edges and corners, dappled sunlight concentrated at upper left edge; the entire central 75 percent is spacious calm deep petrol water with very subtle soft natural tonal variation. Refined natural-history field journal atmosphere, tranquil exploration. Muted midnight teal, pale mint glints, small warm copper leaves. Extremely low contrast center so colored vorticity heatmap and readable white UI can overlay it. No fish, no animals, no people, no manmade objects, no writing, no letters, no numbers, no grid, no UI, no logo, no border, no perspective horizon. Square 1024 by 1024, edge detail and deliberately empty center.
```

## 測定表示

暖色が反時計回り（正）、青が時計回り（負）。色以外にも各極値に + / − を置く。
流速の矢印、渦度、計算円盤を個別に切り替えられる。渦度の凡例は
`± max(1, peak |ω|) / 2` を毎フレーム表示し、tanhで色へ写す相対スケール。
背景にある明暗を流速・水深の測定結果とは扱わない。
速度はdomain/s、Reは無次元、渦度はs⁻¹。最近20秒の速度線もCの状態から取る。

## 技術資料

2026-10-23確認の[Emscripten公式のC/JavaScript接続資料](https://emscripten.org/docs/porting/connecting_cpp_and_javascript/Interacting-with-code.html)
に従い、Cの関数とヒープの読み出し口を明示的にexportした。
シミュレーターをJavaScriptへ書き直していない。

## 実画面と限界

`docs/images/before.jpg` は従来のC出力 `docs/wake.svg` を実表示して撮影した比較画像。
端末の写真ではない。`title.jpg`、`swim.jpg`、`model.jpg`、`mobile.jpg` は
今回のWASMアプリをローカルChromiumで動かした実画面で、原画そのものを
プレイ画面と称したものではない。全5枚を目視確認する。
物理数値の正しさと原画の読み込みは自動検査できるが、アート点は材料の代理指標であり、
見た目の評価ではない。人間の初見試遊、実端末のタッチ、長時間プレイは未検証。
