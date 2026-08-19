#pragma once

#include "Camera/View/ViewInfo.h"

/// @file
/// @brief カメラから ViewInfo（描画用の不変スナップショット）を構築する

namespace CoreEngine
{
    class Camera;

    /// @brief カメラから ViewInfo（フレーム内で不変のビュー情報）を組み立てる
    class ViewBuilder {
    public:
        /// @brief カメラの現在状態から ViewInfo を構築する（camera が nullptr なら isValid == false）
        /// @warning TAA の射影ジッタが確定した「後」に呼ぶこと。
        ///          ここでスナップショットした projection がフレーム内の唯一の真実になる。
        static ViewInfo Build(const Camera* camera, RenderViewType type);
    };
}
