#pragma once

#include "GameObject/Component/Core/IComponent.h"
#include "Graphics/Light/Light.h"
#include "Reflection/Reflect.h"

#include <string>
#include <vector>

namespace CoreEngine
{
    class LightManager;

    /// @brief シーンのライト 1 灯を持つコンポーネント
    /// @details 値はこのコンポーネントだけが持ち、`SyncWithManager()` で `LightManager` の実体へ写す。
    ///          太陽・月を動かす側（Sky Atmosphere エディタ・昼夜サイクル）も、`Find()` で
    ///          このコンポーネントを引いて値を書く。
    /// @note 位置はオブジェクトの Transform が持つ。
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
            REFLECT_PROPERTY(light_.intensity, "強さ", p.range = Speed(10.0f),
                p.tooltip = "平行光源は照度 [lx]（快晴の太陽 = 100000）、点光源とスポットは光度 [cd]")
            REFLECT_PROPERTY(light_.direction, "向き", p.range = Range(-1.0f, 1.0f, 0.01f))
            REFLECT_PROPERTY(light_.range, "届く距離", p.range = Range(0.0f, 1000.0f, 0.1f))
            REFLECT_PROPERTY(light_.innerConeAngleDeg, "内側の角度", p.range = Range(0.0f, 90.0f, 0.5f))
            REFLECT_PROPERTY(light_.outerConeAngleDeg, "外側の角度", p.range = Range(0.0f, 90.0f, 0.5f))
            REFLECT_PROPERTY(light_.areaWidth, "発光面の幅", p.range = Range(0.0f, 100.0f, 0.1f))
            REFLECT_PROPERTY(light_.areaHeight, "発光面の高さ", p.range = Range(0.0f, 100.0f, 0.1f))
            REFLECT_PROPERTY(light_.isAtmosphereSun, "大気の太陽")
            REFLECT_PROPERTY(light_.isAtmosphereMoon, "大気の月")
            REFLECT_PROPERTY(light_.atmosphereIntensity, "空の明るさ", p.range = Range(0.0f, 100.0f, 0.1f),
                p.tooltip = "空・雲の明るさ（無次元。太陽の目安 20）。0 で照度から自動換算")
        REFLECT_END()

        /// @brief 点光源として作る
        LightComponent();

        /// @brief 種類を決めて作る（種類ごとの既定値が入る）
        explicit LightComponent(LightType type);

        ~LightComponent() override;

        /// @brief ライトの実体を作る（値が流し込まれた後に呼ばれる）
        void Awake() override;

        /// @brief 実体を点ける
        void OnEnable() override;

        /// @brief 実体を消す
        void OnDisable() override;

        /// @brief 値を実体へ写す
        /// @note `LightingFeature` が FrameStart に有効な全灯ぶん呼ぶ（停止中も回るので、
        ///       編集中でもインスペクタの変更がそのフレームの画に出る）。値を書いた側がすぐ写すときも呼ぶ。
        void SyncWithManager();

        LightType GetLightType() const { return light_.type; }

        /// @brief 種類を変える
        /// @note 実体へは次の同期で `LightManager::ChangeType` を通して写す。
        ///       変える先の種類が最大数に達していれば断られ、実体の種類へ戻る。
        void SetLightType(LightType type) { light_.type = type; }

        /// @brief 色（α は使わない）
        Vector4 GetColor() const;
        void SetColor(const Vector4& color);

        /// @brief 保存される側の値
        Light& Get() { return light_; }
        const Light& Get() const { return light_; }

        /// @brief `LightManager` が持つ実体（読むだけ。作れていなければ nullptr）
        const Light* GetLight() const;

        /// @brief 実体を持つコンポーネントをハンドルから引く（無ければ nullptr）
        static LightComponent* Find(LightHandle handle);

        /// @brief このライトのギズモを、次の描画だけ詳細表示にする
        /// @note 選択中のライトを目立たせるためにインスペクタから毎フレーム呼ぶ。
        void FocusGizmo() const;

    private:
        /// @brief エンジンから `LightManager` を引く（一度引いたら覚える）
        LightManager* ResolveManager() const;

        Light light_{};

        /// オブジェクトの名前（変わったときだけ実体の名前へ写す）
        std::string ownerName_;

        LightHandle handle_{};
        mutable LightManager* lightManager_ = nullptr;

        /// 実体を持っているコンポーネント（Find で引く）
        static std::vector<LightComponent*> instances_;
    };
}
