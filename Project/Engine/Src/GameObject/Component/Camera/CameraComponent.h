#pragma once

#include "Camera/CameraStructs.h"
#include "GameObject/Component/Core/IComponent.h"
#include "Reflection/Reflect.h"

#include <string>

namespace CoreEngine
{
    class Camera;

    /// @brief シーンのカメラ 1 台を持つコンポーネント
    /// @details 構図はオブジェクトの Transform が持ち、レンズ（投影・視野角・手前と奥の限界）を
    ///          このコンポーネントが持つ。実体（`Camera`）の生成・登録・毎フレームの反映は
    ///          `CameraFeature` が引き受けるので、ここは値と登録のキーだけを持つ。
    /// @note 「ゲームの視点」を入れたカメラがゲームビューに映る。入っているものが複数あれば
    ///       最初の 1 台を使う。1 台も無ければエンジン既定のカメラに戻る。
    ///       実体は 1 つのオブジェクトに 1 台（2 つ目以降のコンポーネントには作らない）。
    class CameraComponent : public IComponent
    {
    public:
        /// @brief 投影の名前（`CameraProjectionType` の並び）
        static constexpr const char* kProjectionNames[] = { "透視投影", "平行投影" };

        const char* GetTypeName() const override { return "Camera"; }

        REFLECT_BEGIN(CameraComponent, "カメラ")
            REFLECT_ACCESSOR("isMainCamera", "ゲームの視点", IsMainCamera, SetMainCamera,
                p.tooltip = "入れるとこのカメラがゲームビューに映る")
            REFLECT_ENUM_ACCESSOR("projection", "投影",
                GetProjectionType, SetProjectionType, kProjectionNames)
            REFLECT_ACCESSOR("fov", "視野角", GetFovDegrees, SetFovDegrees,
                p.range = Range(1.0f, 170.0f, 0.5f), p.tooltip = "透視投影のときだけ使う [度]")
            REFLECT_PROPERTY(parameters_.nearClip, "手前の限界", p.range = Speed(0.01f))
            REFLECT_PROPERTY(parameters_.farClip, "奥の限界", p.range = Speed(10.0f))
        REFLECT_END()

        /// @brief 構図を持たせるため Transform を確かめる
        void Awake() override;

        /// @brief 値とオブジェクトの姿勢を実体へ写す
        /// @note 向きはオブジェクトのローカル回転をそのまま使う（親の回転は含めない）。
        void ApplyTo(Camera& camera) const;

        CameraProjectionType GetProjectionType() const { return parameters_.projectionType; }
        void SetProjectionType(CameraProjectionType type) { parameters_.projectionType = type; }

        /// @brief 視野角（度。入れた値をそのまま持ち、実体へはラジアンにして渡す）
        float GetFovDegrees() const { return fovDegrees_; }
        void SetFovDegrees(float degrees)
        {
            fovDegrees_ = degrees;
            parameters_.SetFovDegrees(degrees);
        }

        bool IsMainCamera() const { return isMainCamera_; }
        void SetMainCamera(bool value) { isMainCamera_ = value; }

        /// @brief 実体を登録したキー（オブジェクトの ID。まだ登録していなければ空）
        /// @note 実体は `CameraManager` が持ち、`CameraFeature` がこのキーで毎回引く。
        const std::string& GetRegisteredKey() const { return registeredKey_; }
        void SetRegisteredKey(std::string key) { registeredKey_ = std::move(key); }

    private:
        CameraParameters parameters_{};
        float fovDegrees_ = parameters_.GetFovDegrees();
        bool isMainCamera_ = true;

        // 実体を登録したキー（オブジェクトの ID）
        std::string registeredKey_;
    };
}
