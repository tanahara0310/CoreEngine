#pragma once

#include "GameObject/Component/Core/IComponent.h"
#include "Graphics/Fog/Volumetric/VolumetricFog.h"
#include "Math/Vector/Vector3.h"
#include "Math/Vector/Vector4.h"
#include "Reflection/Reflect.h"

namespace CoreEngine
{
    /// @brief 箱か球の形の中だけに霧を置くコンポーネント（ボリュメトリックフォグの格子へ足す）
    /// @details 形の中心・向きはオブジェクトのトランスフォーム、大きさは「大きさ」×スケール。
    ///          高さフォグの設定に関係なく描かれる（高さフォグのボリュメトリックが無効でも、局所の霧があれば格子を作る）。
    ///          ボリュメトリックフォグの格子が届く距離より奥の部分は、合成が視線に沿って足す。
    ///          シーンの更新（EnvironmentFeature）が有効なものを毎フレーム集めて霧へ渡す。
    class FogVolumeComponent final : public IComponent {
    public:
        /// @brief 形（VolumetricFog::VolumeShape と同じ並び）
        enum class Shape : int {
            Box = 0,
            Sphere = 1,
        };

        static constexpr const char* kShapeNames[] = { "箱", "球" };

        const char* GetTypeName() const override { return "FogVolume"; }

        REFLECT_BEGIN(FogVolumeComponent, "局所の霧")
            REFLECT_ENUM_ACCESSOR("shape", "形", GetShape, SetShape, kShapeNames)
            REFLECT_PROPERTY(size_, "大きさ", p.range = Range(0.01f, 10000.0f, 0.1f),
                p.tooltip = "箱は各辺の長さ・球は直径 [m]。オブジェクトのスケールも掛かる")
            REFLECT_PROPERTY(edgeFade_, "縁のぼかし", p.range = Range(0.0f, 1.0f, 0.01f),
                p.tooltip = "縁で霧を薄くしていく幅（中心から表面までに対する割合）。0 でくっきり・1 で中心から薄くなる")
            REFLECT_PROPERTY(density_, "濃さ", p.range = Range(0.0f, 10.0f, 0.001f),
                p.tooltip = "消散係数 [1/m]。0.1 で 10m 先がおよそ 37% 見える")
            REFLECT_ACCESSOR("albedo", "色", GetAlbedo, SetAlbedo,
                p.type = ::CoreEngine::Reflection::PropertyType::Color,
                p.flags = ::CoreEngine::Reflection::PropertyFlags::NoAlpha,
                p.tooltip = "光を散乱する割合。白で光をそのまま散らし、暗くすると霧が暗く・色を付けると色の霧になる")
            REFLECT_ACCESSOR("emissiveColor", "発光の色", GetEmissiveColor, SetEmissiveColor,
                p.type = ::CoreEngine::Reflection::PropertyType::Color,
                p.flags = ::CoreEngine::Reflection::PropertyFlags::NoAlpha)
            REFLECT_PROPERTY(emissiveIntensity_, "発光の強さ", p.range = Range(0.0f, 100000.0f, 0.1f),
                p.tooltip = "霧 1m あたりの発光の輝度 [nt/m]。光が無くても霧が光って見える。0 で光らない")
            REFLECT_PROPERTY(noiseStrength_, "ノイズの強さ", p.range = Range(0.0f, 1.0f, 0.01f),
                p.tooltip = "濃さにむらを付ける強さ。模様の大きさは r.Fog.Volumetric.NoiseScale、流れは風（r.Wind.*）")
        REFLECT_END()

        void Awake() override;

        Shape GetShape() const { return shape_; }
        void SetShape(Shape shape) { shape_ = shape; }

        Vector4 GetAlbedo() const { return { albedo_.x, albedo_.y, albedo_.z, 1.0f }; }
        void SetAlbedo(const Vector4& color) { albedo_ = { color.x, color.y, color.z }; }

        Vector4 GetEmissiveColor() const { return { emissiveColor_.x, emissiveColor_.y, emissiveColor_.z, 1.0f }; }

        /// @brief 「大きさ」（スケールを掛ける前）[m]
        const Vector3& GetSize() const { return size_; }
        void SetEmissiveColor(const Vector4& color) { emissiveColor_ = { color.x, color.y, color.z }; }

        /// @brief 格子へ渡す値（中心・向き・大きさはオブジェクトのワールドの値から作る）
        VolumetricFogVolumeDesc BuildDesc() const;

    private:
        Shape shape_ = Shape::Box;
        Vector3 size_{ 10.0f, 10.0f, 10.0f };
        float edgeFade_ = 0.25f;
        float density_ = 0.05f;
        Vector3 albedo_{ 1.0f, 1.0f, 1.0f };
        Vector3 emissiveColor_{ 1.0f, 1.0f, 1.0f };
        float emissiveIntensity_ = 0.0f;
        float noiseStrength_ = 0.0f;
    };
}
