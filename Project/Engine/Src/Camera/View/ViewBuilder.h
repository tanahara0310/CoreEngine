#pragma once

#include "Camera/View/ViewInfo.h"

/// @brief カメラから ViewInfo（描画用の不変スナップショット）を構築する

namespace CoreEngine
{
    class ICamera;

    class ViewBuilder {
    public:
        /// @brief カメラの現在状態から ViewInfo を構築する
        /// @details 呼ぶタイミングが重要: TAA の射影ジッタが確定した「後」に呼ぶこと。
        ///          ここでスナップショットした projection がフレーム内の唯一の真実になり、
        ///          モデルの WVP・深度復元・視錐台カリングがすべてこれに揃う。
        /// @param camera スナップショット元のカメラ（nullptr 可）
        /// @param type ビュー種別
        /// @return 構築した ViewInfo（camera が nullptr なら isValid == false）
        static ViewInfo Build(const ICamera* camera, RenderViewType type);
    };
}
