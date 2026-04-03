# CoreEngine ドキュメント

CoreEngine のアプリケーション層向けドキュメントです。  
エンジンが提供するクラスやシステムの使い方をカテゴリごとにまとめています。

## 📁 カテゴリ一覧

| カテゴリ | 内容 |
|---------|------|
| [Core](./Core/) | フレームワーク基盤・エンジンシステム |
| [Scene](./Scene/) | シーン管理・シーン遷移 |
| [GameObject](./GameObject/) | ゲームオブジェクト（3Dモデル・スプライト） |
| [Graphics](./Graphics/) | テクスチャ・モデル・ライト・IBL |
| [Input](./Input/) | キーボード・マウス入力 |
| [Audio](./Audio/) | サウンド再生・音量制御 |
| [Camera](./Camera/) | カメラ管理 |
| [Collision](./Collision/) | 衝突判定 |
| [Particle](./Particle/) | パーティクルシステム |
| [Math](./Math/) | 数学ライブラリ |
| [Utility](./Utility/) | タイマー・乱数・ログ等ユーティリティ |

## 🚀 クイックスタート

CoreEngine でゲームを作成する基本的な流れ:

1. `Framework` を継承したゲームクラスを作成する
2. `SceneManager` にシーンを登録し、初期シーンを設定する
3. `BaseScene` を継承したシーンクラスを作成する
4. シーン内で `CreateObject<T>()` を使い `GameObject` を配置する
5. `ModelGameObject` や `SpriteObject` を継承して独自オブジェクトを作成する

```cpp
// main.cpp
#include "MyGame.h"

int WINAPI WinMain(...) {
    MyGame game;
    game.Run();
    return 0;
}
```

詳細は各カテゴリのドキュメントを参照してください。
