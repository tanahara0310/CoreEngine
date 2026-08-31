# CoreEngine での msdfgen の使い方

取得元: https://github.com/Chlumsky/msdfgen （MIT ライセンス。`LICENSE.txt` を参照）

**このツリーは配布物そのままではなく、CoreEngine が使う分だけに削ってある。**
更新時はこのファイルの手順どおりに削り直すこと。

## 残してあるもの

```
msdfgen.h                  距離場生成の入口（[CoreEngine 改変] あり。後述）
core/                      距離場生成の本体（45 ファイル / .cpp は 15 本）
LICENSE.txt                MIT ライセンス本文。再配布に必須なので消さないこと
USAGE_IN_COREENGINE.md     このファイル
```

`core/` は外部依存を一切持たず、標準ライブラリだけで完結する。

## 削除したもの

### トップレベル

| 削除物 | 中身 | 理由 |
|---|---|---|
| `ext/` | FreeType / tinyxml2 / libpng を使うフォント・SVG・PNG 読み込み | フォント読み込みは `Engine/Src/Text/DirectWriteFontFace.*` が DirectWrite で担当する。**追加すると FreeType 依存が復活する** |
| `main.cpp` `msdfgen.rc` `resource.h` `icon.ico` | スタンドアロン実行ファイル | ライブラリとして組み込むので不要 |
| `CMakeLists.txt` `CMakePresets.json` `cmake/` `vcpkg.json` `build-release.bat` | CMake / vcpkg ビルド一式 | `CoreEngine.vcxproj` へ直接ソースを登録している |
| `README.md` `CHANGELOG.md` | 配布物のドキュメント | 上記リンクの本家を見ること |

### `core/` から削除したもの（距離場の生成には不要）

`render-sdf` `rasterization` `sdf-error-estimation`（距離場のプレビュー描画と誤差評価）、
`save-bmp` `save-tiff` `save-rgba` `save-fl32`（ファイル出力）、
`shape-description`（テキスト形式の図形記述の読み書き）、
`export-svg`（SVG 出力）— それぞれ `.h` / `.cpp` の対で削除。

これに伴い **`msdfgen.h` から上記 9 本の `#include` を削除している**（該当箇所にコメントあり）。
`msdfgen.h` の改変はこの 1 箇所だけ。

### 削るファイルの決め方

`Shape` の組み立て → `edgeColoringSimple` → `generateMTSDF` から
`#include` を推移的に閉包し、届かないものを落としている。
本家を更新したら同じ手順で洗い直すこと。

## ビルド設定

`core/*.cpp` を `CoreEngine.vcxproj` へ登録済み。各ファイルに以下を指定している。

| 設定 | 値 | 理由 |
|---|---|---|
| `PrecompiledHeader` | `NotUsing` | 第三者コードなので `pch.h` を含まない |
| `WarningLevel` | `Level3` | エンジン側の W4 + 警告エラー化を持ち込まない |
| `TreatWarningAsError` | `false` | 同上 |
| `SDLCheck` | `false` | `/sdl` が `fopen` の C4996 をエラーに昇格させるため |
| `_CRT_SECURE_NO_WARNINGS` | 定義 | 同上 |

プロジェクト全体のプリプロセッサ定義に以下を追加している。

- `MSDFGEN_PUBLIC=` … 空定義。`core/base.h` が CMake 生成物である
  `msdfgen/msdfgen-config.h` を要求しなくなる（CMake を通さずに組み込むため）
- `MSDFGEN_USE_CPP11` … ムーブコンストラクタ等を有効化する

## 使い方

CoreEngine 側の入口は `Engine/Src/Text/` にある。

```
DirectWriteFontFace   フォント解析（DirectWrite）→ msdfgen::Shape の組み立て
MsdfFontBaker         edgeColoringSimple → generateMTSDF → アトラスへ配置
MsdfFont              アトラスの GPU 転送とグリフ検索
```

アウトラインの取得口を増やしたい場合は、削除した `ext/` を戻すのではなく
`DirectWriteFontFace` と同じ形（`msdfgen::Shape` を組み立てて返す）で実装すること。
