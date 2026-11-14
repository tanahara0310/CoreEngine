#include "pch.h"
#include "Editor/Scene/SceneFogVolumeGizmos.h"

#ifdef CORE_EDITOR

#include "Editor/Scene/SceneViewTools.h"
#include "GameObject/GameObject.h"
#include "GameObject/GameObjectManager.h"
#include "GameObject/Component/Environment/FogVolumeComponent.h"
#include "Utility/CVar/CVar.h"

#include <array>
#include <cmath>
#include <numbers>

namespace CoreEngine::Editor
{
    namespace
    {
        CVar<bool> cvVisualize{
            "d.FogVolume.Visualize", true,
            "シーンビューに局所の霧（FogVolume）の形の輪郭を描く" };

        /// 輪郭の線の太さ [px]
        constexpr float kLineThickness = 1.5f;

        /// 選んでいる局所の霧の線の太さ [px]
        constexpr float kSelectedLineThickness = 2.0f;

        /// 線の下に敷く暗い縁の、線より太い分 [px]
        constexpr float kOutlineWidth = 2.0f;

        /// 物に隠れた所の線の濃さ（隠れていない所に掛ける）
        constexpr float kHiddenAlpha = 0.25f;

        /// 球の輪の分ける数
        constexpr int kRingSegments = 48;

        constexpr ImU32 kLine = IM_COL32(150, 205, 255, 110);
        constexpr ImU32 kSelectedLine = IM_COL32(150, 205, 255, 240);
        constexpr ImU32 kOutline = IM_COL32(8, 10, 14, 140);

        /// @brief 箱の 12 本の辺
        void DrawBox(const VolumetricFogVolumeDesc& volume, ImU32 color, float thickness)
        {
            const Vector3 x = volume.axisX * (volume.size.x * 0.5f);
            const Vector3 y = volume.axisY * (volume.size.y * 0.5f);
            const Vector3 z = volume.axisZ * (volume.size.z * 0.5f);
            std::array<Vector3, 8> corners{};
            for (int i = 0; i < 8; ++i) {
                corners[i] = volume.center
                    + x * ((i & 1) ? 1.0f : -1.0f)
                    + y * ((i & 2) ? 1.0f : -1.0f)
                    + z * ((i & 4) ? 1.0f : -1.0f);
            }
            for (int i = 0; i < 8; ++i) {
                for (int bit = 1; bit < 8; bit <<= 1) {
                    if ((i & bit) == 0) {
                        SceneOverlay::Line(corners[i], corners[i | bit], color, thickness);
                    }
                }
            }
        }

        /// @brief 球（スケールで伸びた楕円体）の 3 本の輪
        void DrawSphere(const VolumetricFogVolumeDesc& volume, ImU32 color, float thickness)
        {
            const Vector3 axes[3] = {
                volume.axisX * (volume.size.x * 0.5f),
                volume.axisY * (volume.size.y * 0.5f),
                volume.axisZ * (volume.size.z * 0.5f) };
            for (int ring = 0; ring < 3; ++ring) {
                const Vector3& u = axes[ring];
                const Vector3& v = axes[(ring + 1) % 3];
                std::array<Vector3, kRingSegments> points{};
                for (int i = 0; i < kRingSegments; ++i) {
                    const float a = 2.0f * std::numbers::pi_v<float> * static_cast<float>(i) / static_cast<float>(kRingSegments);
                    points[i] = volume.center + u * std::cos(a) + v * std::sin(a);
                }
                SceneOverlay::Polyline(points.data(), points.size(), true, color, thickness);
            }
        }

        void DrawShape(const VolumetricFogVolumeDesc& volume, ImU32 color, float thickness)
        {
            if (volume.shape == VolumetricFog::kVolumeShapeSphere) {
                DrawSphere(volume, color, thickness);
            }
            else {
                DrawBox(volume, color, thickness);
            }
        }
    }

    void SceneFogVolumeGizmos::Initialize()
    {
        registration_ = SceneViewTools::Get().Register([this](const SceneViewContext& context) { Draw(context); });
    }

    void SceneFogVolumeGizmos::Draw(const SceneViewContext& context)
    {
        if (!cvVisualize.Get() || !context.objects) {
            return;
        }

        SceneOverlay::BeginOccluded(kHiddenAlpha);
        const FogVolumeComponent* selectedVolume = nullptr;
        for (const auto& object : context.objects->GetAllObjects()) {
            if (!object || object->IsMarkedForDestroy()) {
                continue;
            }
            const FogVolumeComponent* const volume = object->GetComponent<FogVolumeComponent>();
            if (!volume) {
                continue;
            }
            if (object.get() == context.selected) {
                selectedVolume = volume;
                continue;
            }
            if (volume->IsActiveAndEnabled()) {
                DrawShape(volume->BuildDesc(), kLine, kLineThickness);
            }
        }
        if (selectedVolume) {
            const VolumetricFogVolumeDesc desc = selectedVolume->BuildDesc();
            DrawShape(desc, kOutline, kSelectedLineThickness + kOutlineWidth);
            DrawShape(desc, kSelectedLine, kSelectedLineThickness);
        }
        SceneOverlay::EndOccluded();
    }
}

#endif // CORE_EDITOR
