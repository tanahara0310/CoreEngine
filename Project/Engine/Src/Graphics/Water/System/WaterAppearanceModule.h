#pragma once

#include "GameObject/Component/Core/ObjectRef.h"

namespace CoreEngine
{
    class SkyBoxComponent;
    struct SceneContext;
    struct WaterFrameBinding;
    struct WaterFrameContext;

    /// @brief 水面の見た目と水質、水越しに見るシーン・空との結線を受け持つ
    class WaterAppearanceModule {
    public:
        /// @brief シーンの空（大気散乱の SkyBox）を探して覚える
        void FindSky(SceneContext& scene);

        /// @brief 覚えた空を忘れる
        void Reset();

        /// @brief 色・粗さ・フレネル・深さによる色の変化・水質・屈折率を水面へ渡す
        void ApplySettings(const WaterFrameContext& frame) const;

        /// @brief シーンの色と深度・カメラのクリップ距離・大気の SRV を結線へ書く
        void Bind(const WaterFrameContext& frame, WaterFrameBinding& binding) const;

    private:
        /// @brief 空気遠近感をかけるかの判定に使う空（所有権は GameObjectManager）
        ObjectRef<SkyBoxComponent> skyBox_;
    };
}
