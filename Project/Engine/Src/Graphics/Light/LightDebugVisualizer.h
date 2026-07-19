#pragma once

#include "Light.h"

namespace CoreEngine
{
    class LightManager;

    /// @brief ライトのデバッグ可視化とImGui UI管理クラス
    /// @details Hierarchy の Environment > Lighting 配下に各ライトを列挙し、
    ///          選択したライトのみ Inspector に表示する（Unity 風の選択駆動編集）。
    ///          追加・削除・複製は LightManager のハンドル API 経由で行う。
    class LightDebugVisualizer
    {
    public:
        /// @brief Hierarchy の Lighting 配下に各ライトの子行を描画する
        /// @return いずれかの子行がクリックされた場合 true（Inspector を Lighting へルーティングする）
        bool DrawHierarchyChildren(LightManager& manager);

        /// @brief Inspector の内容を描画（選択ライトのプロパティ or 概要）
        void DrawImGui(LightManager& manager);

        /// @brief ライトの選択を解除する（親エントリ選択時＝概要表示へ戻す）
        void ClearSelection() { selectedLight_ = {}; }

        /// @brief 選択中のライトハンドルを取得する（未選択・削除済みの場合は無効ハンドル）
        LightHandle GetSelectedLight() const { return selectedLight_; }

        /// @brief ライト1個分のデバッグ可視化を描画（種類で内部分岐）
        /// @param selected 選択中のライトは詳細ギズモ、非選択は簡略マーカーで描く
        void DrawVisualization(const Light& light, bool selected);

        /// @brief デバッグ可視化の有効/無効を設定
        void SetVisualizationEnabled(bool enabled) { enableVisualization_ = enabled; }

        /// @brief デバッグ可視化が有効かどうかを取得
        bool IsVisualizationEnabled() const { return enableVisualization_; }

    private:
        /// @brief 概要表示（ライト数・追加ボタン・可視化トグル）
        void DrawOverview(LightManager& manager);

        /// @brief 選択中ライトのプロパティ編集（名前・色・種類別フィールド・複製/削除）
        void DrawSelectedLightInspector(LightManager& manager);

        /// @brief 種類別プロパティ編集 UI（物理単位ラベル付き）
        void DrawLightProperties(Light& light);

        void DrawDirectionalLightVisualization(const Light& light, bool selected);
        void DrawPointLightVisualization(const Light& light, bool selected);
        void DrawSpotLightVisualization(const Light& light, bool selected);
        void DrawAreaLightVisualization(const Light& light, bool selected);

    private:
        LightHandle selectedLight_{};  ///< Hierarchy で選択中のライト（無効=概要表示）

#ifdef _DEBUG
        bool enableVisualization_ = true;
#else
        bool enableVisualization_ = false;
#endif
    };
}
