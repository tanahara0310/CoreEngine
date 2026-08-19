#pragma once
#include "Math/Vector/Vector3.h"

namespace CoreEngine {

    struct ViewInfo;

    /// @brief 毎フレーム全ポストエフェクトへ配られる文脈
    /// @details エフェクト固有の値注入をこの 1 経路に集約し、フレームワーク側の分岐をなくす。
    /// @note 載せるのはサービス（Manager のポインタ）ではなくデータ。
    ///       太陽の向きが要るなら太陽の向きだけを渡す。
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
