#pragma once

#include "GameObject/Component/Core/IComponent.h"
#include "Graphics/Light/Light.h"
#include "Reflection/Reflect.h"

#include <string>

namespace CoreEngine
{
    class LightManager;

    /// @brief シーンのライト 1 灯を持つコンポーネント
    /// @details 値はこのコンポーネントが持ち、`SyncWithManager()` で `LightManager` の実体へ写す。
    ///          実体を直に書き換える経路（Lighting パネル・Sky Atmosphere エディタ・シーンのコード）の
    ///          編集は次の同期で取り込むので、どちらから触っても保存データに残る。
    /// @note 位置はオブジェクトの Transform が持つ（実体側で動かされたらオブジェクトを動かす）。
    ///       向きは今はこのコンポーネントの値で、オブジェクトの回転からは決めていない。
    class LightComponent : public IComponent
    {
    public:
        /// @brief 種類の名前（`LightType` の並び）
        static constexpr const char* kLightTypeNames[] = {
            "平行光源", "点光源", "スポットライト", "エリアライト",
        };

        const char* GetTypeName() const override { return "Light"; }

        // 強さの単位は種類で違う（平行光源 = 照度 [lx] / 点光源・スポット = 光度 [cd] / エリア = 輝度 [nt]）
        REFLECT_BEGIN(LightComponent, "ライト")
            REFLECT_ENUM_ACCESSOR("type", "種類", GetLightType, SetLightType, kLightTypeNames)
            REFLECT_ACCESSOR("color", "色", GetColor, SetColor,
                p.type = ::CoreEngine::Reflection::PropertyType::Color,
                p.flags = ::CoreEngine::Reflection::PropertyFlags::NoAlpha)
            REFLECT_PROPERTY(light_.intensity, "強さ", Speed(10.0f))
            REFLECT_PROPERTY(light_.direction, "向き", Range(-1.0f, 1.0f, 0.01f))
            REFLECT_PROPERTY(light_.range, "届く距離", Range(0.0f, 1000.0f, 0.1f))
            REFLECT_PROPERTY(light_.innerConeAngleDeg, "内側の角度", Range(0.0f, 90.0f, 0.5f))
            REFLECT_PROPERTY(light_.outerConeAngleDeg, "外側の角度", Range(0.0f, 90.0f, 0.5f))
            REFLECT_PROPERTY(light_.areaWidth, "発光面の幅", Range(0.0f, 100.0f, 0.1f))
            REFLECT_PROPERTY(light_.areaHeight, "発光面の高さ", Range(0.0f, 100.0f, 0.1f))
            REFLECT_PROPERTY(light_.isAtmosphereSun, "大気の太陽")
            REFLECT_PROPERTY(light_.isAtmosphereMoon, "大気の月")
            REFLECT_PROPERTY(light_.atmosphereIntensity, "空の明るさ", Range(0.0f, 100.0f, 0.1f))
        REFLECT_END()

        LightComponent();
        ~LightComponent() override;

        /// @brief ライトの実体を作る（値が流し込まれた後に呼ばれる）
        void Awake() override;

        /// @brief 値を実体へ写し、外から変えられていた分を取り込む
        /// @note `LightingFeature` が FrameStart に全灯ぶん呼ぶ（停止中も回るので、
        ///       編集中でもインスペクタの変更がそのフレームの画に出る）。
        void SyncWithManager();

        LightType GetLightType() const { return light_.type; }
        void SetLightType(LightType type) { light_.type = type; }

        /// @brief 色（α は使わない）
        Vector4 GetColor() const;
        void SetColor(const Vector4& color);

        /// @brief 保存される側の値
        Light& Get() { return light_; }
        const Light& Get() const { return light_; }

        /// @brief `LightManager` が持つ実体（作れていなければ nullptr）
        Light* GetLight() const;

    private:
        /// @brief エンジンから `LightManager` を引く（一度引いたら覚える）
        LightManager* ResolveManager() const;

        /// @brief 実体を直に書き換えた分を、保存される側へ取り込む
        void AdoptExternalEdits(const Light& live);

        /// @brief 保存される側の値を実体へ書く
        void ApplyToLight(Light& live);

        /// @brief 実体側で動かされた位置を、オブジェクトの Transform へ戻す
        void MoveOwnerTo(const Vector3& position);

        Light light_{};

        /// 前回この コンポーネントが実体へ書いた内容（外からの編集を見分けるための控え）
        Light lastWritten_{};

        /// オブジェクトの名前（変わったときだけ実体の名前へ写す）
        std::string ownerName_;

        LightHandle handle_{};
        mutable LightManager* lightManager_ = nullptr;
    };
}
