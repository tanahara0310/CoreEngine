#pragma once

#include "Light.h"

namespace CoreEngine
{
    /// @brief ライトの位置と向きをギズモで描く
    /// @details 編集はライトを持つオブジェクトの Inspector（`LightComponent`）が受け持つ。
    ///          ここは見せるだけで、値は触らない。
    /// @note 描くかどうかは CVar `d.Light.Visualize`（判定は LightManager 側）。
    class LightDebugVisualizer
    {
    public:
        /// @brief ライト1個分のデバッグ可視化を描画（種類で内部分岐）
        /// @param selected 選択中のライトは詳細ギズモ、非選択は簡略マーカーで描く
        void DrawVisualization(const Light& light, bool selected);

    private:
        void DrawDirectionalLightVisualization(const Light& light, bool selected);
        void DrawPointLightVisualization(const Light& light, bool selected);
        void DrawSpotLightVisualization(const Light& light, bool selected);
        void DrawAreaLightVisualization(const Light& light, bool selected);
    };
}
