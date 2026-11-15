#pragma once

#ifdef CORE_EDITOR

#include "Editor/Environment/SkyMapWidget.h"
#include "Editor/Environment/SunMoonController.h"
#include "Editor/Environment/SunMoonSceneGizmo.h"
#include "Utility/Lifetime/ScopedRegistration.h"

namespace CoreEngine
{
    class EngineSystem;

    /// @brief 空と大気散乱（SkyBox）のインスペクタ
    /// @details DebugSubsystem がエンジンの寿命で 1 個持ち、シーンに置かれた空の物のインスペクタとして中身を描く。
    ///          太陽と月は空の図とシーンビューの印をドラッグして置き、値は左にラベルが並ぶ行で直す。
    class AtmosphereEditor
    {
    public:
        /// @brief 空のコンポーネントのインスペクタとシーンビューの道具として登録する
        void Initialize(EngineSystem& engine);

    private:
        /// @brief インスペクタの中身を描く
        void DrawContent();

        /// @brief 空の図・プリセット・太陽と月の値
        void DrawPlacementSection();
        void DrawSunRows();
        void DrawMoonRows();

        /// @brief 時刻と緯度から太陽を置く
        void DrawTimeSection();

        /// @brief 空から来る環境光と映り込み
        void DrawAmbientSection();

        /// @brief 惑星と大気の散乱の係数
        void DrawAtmosphereSection();

        /// @brief 大気が計算した値
        void DrawDiagnosticsSection();

        EngineSystem* engine_ = nullptr;
        SunMoonController controller_;
        SkyMapWidget skyMap_;
        SunMoonSceneGizmo sceneGizmo_;

        // インスペクタの出し方の登録（破棄すると外れる。ほかのメンバより先に破棄される）
        ScopedRegistration inspector_;
    };
}

#endif // CORE_EDITOR
