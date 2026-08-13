#pragma once
#include "Math/Vector/Vector3.h"

namespace CoreEngine {

    struct ViewInfo;

    /// @brief 毎フレーム全ポストエフェクトへ配られる文脈
    /// @details エフェクト固有の値注入（Outline のクリップ距離、LensFlare の太陽位置など）を
    ///          この 1 経路に集約するための入れ物。これが無かった頃は PostEffectPass と
    ///          RenderPipeline にエフェクト名ごとの分岐が生えており、エフェクトを追加するたびに
    ///          フレームワーク側を編集する必要があった。
    ///
    ///          【設計方針】ここにはサービス（Manager のポインタ）ではなく **データ** を載せる。
    ///          AtmosphereManager* を載せるとポストエフェクト層が大気システムへ依存してしまうため、
    ///          必要なのが太陽の向きなら太陽の向きだけを渡す。
    ///
    ///          設計: Docs/Engine/Graphics/PostProcess/PostEffect_Refactoring_Plan.md
    struct PostEffectFrameContext {
        /// @brief 実際に描画へ使われたビュー（フレーム先頭で確定したスナップショット）
        /// @details 別のカメラの行列を使うと投影位置がずれるため、必ずこれを使うこと
        const ViewInfo* view = nullptr;

        /// @brief 太陽光の進行方向（正規化済み）。太陽の見える向きはこの逆ベクトル
        Vector3 sunDirection{};

        /// @brief sunDirection が有効か（大気システムが無いシーンでは false）
        bool sunDirectionValid = false;

        /// @brief 前フレームからの経過時間 [秒]
        float deltaTime = 0.0f;
    };
}
