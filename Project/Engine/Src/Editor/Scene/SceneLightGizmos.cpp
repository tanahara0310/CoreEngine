#include "pch.h"
#include "Editor/Scene/SceneLightGizmos.h"

#ifdef CORE_EDITOR

#include "Camera/Camera.h"
#include "Editor/ImGui/Gizmo.h"
#include "Editor/Scene/EditorSceneAccess.h"
#include "Editor/Scene/SceneDebugEditor.h"
#include "Editor/Scene/SceneViewTools.h"
#include "GameObject/GameObject.h"
#include "GameObject/GameObjectManager.h"
#include "GameObject/Component/Light/LightComponent.h"
#include "Graphics/Light/LightManager.h"
#include "Math/MathCore.h"
#include "Utility/CVar/CVar.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

namespace CoreEngine::Editor
{
    namespace
    {
        CVar<bool> cvVisualize{
            "d.Light.Visualize", true,
            "シーンビューにライトのアイコンと、選んだライトの届く範囲を描く" };

        /// LightType の並びと同じ順のアイコンの画像
        constexpr const char* kIconFiles[] = {
            "Engine/Assets/Textures/Icon/Light/directionalLight.png",
            "Engine/Assets/Textures/Icon/Light/pointLight.png",
            "Engine/Assets/Textures/Icon/Light/spotLight.png",
            "Engine/Assets/Textures/Icon/Light/areaLight.png",
        };

        /// アイコンの大きさ [px]
        constexpr float kIconSize = 32.0f;

        /// 選んでいるライトのアイコンの大きさ [px]
        constexpr float kSelectedIconSize = 38.0f;

        /// 範囲の線の太さ [px]
        constexpr float kLineThickness = 2.0f;

        /// 線の下に敷く暗い縁の、線より太い分 [px]
        constexpr float kOutlineWidth = 2.0f;

        /// 物に隠れた所の線の濃さ（隠れていない所に掛ける）
        constexpr float kHiddenAlpha = 0.25f;

        /// 平行光源の矢印の長さ（カメラからの距離に掛ける）
        constexpr float kArrowLengthPerDistance = 0.15f;

        /// 円弧を分ける数
        constexpr int kArcSegments = 24;

        /// 向きを画面へ写した長さがこれより短いときは、アイコンを回さない [px]
        constexpr float kMinScreenDirection = 2.0f;

        constexpr float kHalfPi = std::numbers::pi_v<float> * 0.5f;

        constexpr ImU32 kSelectedRing = IM_COL32(240, 180, 64, 255);
        constexpr ImU32 kSelectedRingFill = IM_COL32(240, 180, 64, 64);

        /// 線の下に敷く暗い縁の色（主な線・薄い線）
        constexpr ImU32 kOutline = IM_COL32(8, 10, 14, 140);
        constexpr ImU32 kOutlineFaint = IM_COL32(8, 10, 14, 60);

        /// 無効のライトのアイコンと線の色（リニア）
        const Vector3 kDisabledTint{ 0.22f, 0.23f, 0.25f };

        /// @brief シーンビューに出すライト 1 灯
        struct IconEntry
        {
            GameObject* object = nullptr;
            const Light* light = nullptr;
            Vector3 position{};
            ImVec2 screen{};
            float depth = 0.0f;
            bool selected = false;
            bool enabled = true;
        };

        /// @brief 範囲の線 1 回分の描き方（暗い縁を敷く回と、色の線を描く回）
        struct Stroke
        {
            ImU32 strong = 0; ///< 主な線
            ImU32 faint = 0;  ///< 補いの線
            ImU32 fill = 0;   ///< 面の塗り（0 なら塗らない）
            float thickness = kLineThickness;
        };

        Vector3 Direction(const Vector3& direction)
        {
            const float length = std::sqrt(CoreEngine::Dot(direction, direction));
            return length > 1.0e-6f ? direction * (1.0f / length) : Vector3(0.0f, -1.0f, 0.0f);
        }

        /// @brief axis に垂直な 2 本の軸
        void Basis(const Vector3& axis, Vector3& u, Vector3& v)
        {
            const Vector3 helper = std::abs(axis.y) < 0.99f ? Vector3(0.0f, 1.0f, 0.0f) : Vector3(1.0f, 0.0f, 0.0f);
            u = CoreEngine::Normalize(CoreEngine::Cross(axis, helper));
            v = CoreEngine::Cross(axis, u);
        }

        /// @brief ライトの色を、暗い色でもアイコンが見える明るさへ持ち上げる
        Vector3 IconTint(const Vector3& color)
        {
            const float peak = (std::max)({ color.x, color.y, color.z });
            if (peak <= 1.0e-4f) {
                return Vector3(1.0f, 1.0f, 1.0f);
            }
            const float scale = 0.75f / peak;
            return Vector3(color.x * scale + 0.25f, color.y * scale + 0.25f, color.z * scale + 0.25f);
        }

        ImU32 ToColor(const Vector3& color, float alpha)
        {
            return ImGui::ColorConvertFloat4ToU32(ImVec4(color.x, color.y, color.z, alpha));
        }

        /// @brief center から axis を中心に side の側へ -angle〜+angle 振った、半径 radius の円弧
        void Arc(const Vector3& center, const Vector3& axis, const Vector3& side, float radius, float angle,
            ImU32 color, float thickness)
        {
            std::array<Vector3, kArcSegments + 1> points;
            for (int i = 0; i <= kArcSegments; ++i) {
                const float a = -angle + 2.0f * angle * static_cast<float>(i) / static_cast<float>(kArcSegments);
                points[i] = center + (axis * std::cos(a) + side * std::sin(a)) * radius;
            }
            SceneOverlay::Polyline(points.data(), points.size(), false, color, thickness);
        }

        /// @brief 矢じり付きの矢印
        void Arrow(const Vector3& from, const Vector3& to, ImU32 color, float thickness)
        {
            SceneOverlay::Line(from, to, color, thickness);
            const Vector3 delta = to - from;
            const float length = std::sqrt(CoreEngine::Dot(delta, delta));
            if (length < 1.0e-4f) {
                return;
            }
            const Vector3 direction = delta * (1.0f / length);
            Vector3 u{};
            Vector3 v{};
            Basis(direction, u, v);
            const float head = length * 0.2f;
            const Vector3 base = to - direction * head;
            for (const Vector3& side : { u, v }) {
                const Vector3 points[] = { base + side * (head * 0.5f), to, base - side * (head * 0.5f) };
                SceneOverlay::Polyline(points, 3, false, color, thickness);
            }
        }

        /// @brief 点光源：届く距離の球（横の円とカメラから見た輪郭を主に、縦の円 2 本を補いに）
        void DrawPointRange(const Vector3& position, const Light& light, const Vector3& cameraPosition, const Stroke& stroke)
        {
            const float range = light.range;
            SceneOverlay::WireDisc(position, Vector3(1.0f, 0.0f, 0.0f), range, stroke.faint, stroke.thickness);
            SceneOverlay::WireDisc(position, Vector3(0.0f, 0.0f, 1.0f), range, stroke.faint, stroke.thickness);
            SceneOverlay::WireDisc(position, Vector3(0.0f, 1.0f, 0.0f), range, stroke.strong, stroke.thickness);

            // 輪郭の円は、中心からカメラへ r²/d 寄った所にある
            const Vector3 toCamera = cameraPosition - position;
            const float distance = std::sqrt(CoreEngine::Dot(toCamera, toCamera));
            if (distance > range) {
                const Vector3 n = toCamera * (1.0f / distance);
                const float ratio = range / distance;
                SceneOverlay::WireDisc(position + n * (range * ratio), n, range * std::sqrt(1.0f - ratio * ratio),
                    stroke.strong, stroke.thickness);
            }
        }

        /// @brief スポットライト：外側の角度の円すい（先は届く距離の球の一部）と、内側の角度の縁
        void DrawSpotRange(const Vector3& position, const Light& light, const Stroke& stroke)
        {
            const Vector3 direction = Direction(light.direction);
            Vector3 u{};
            Vector3 v{};
            Basis(direction, u, v);
            const float outer = std::clamp(light.outerConeAngleDeg, 0.0f, 90.0f) * MathCore::Constants::kDegToRad;
            const float inner = std::clamp(light.innerConeAngleDeg, 0.0f, 90.0f) * MathCore::Constants::kDegToRad;
            const float range = light.range;

            const Vector3 rimCenter = position + direction * (range * std::cos(outer));
            const float rimRadius = range * std::sin(outer);
            SceneOverlay::WireDisc(rimCenter, direction, rimRadius, stroke.strong, stroke.thickness);
            for (const Vector3& side : { u, v, u * -1.0f, v * -1.0f }) {
                SceneOverlay::Line(position, rimCenter + side * rimRadius, stroke.strong, stroke.thickness);
            }
            Arc(position, direction, u, range, outer, stroke.strong, stroke.thickness);
            Arc(position, direction, v, range, outer, stroke.strong, stroke.thickness);

            SceneOverlay::WireDisc(position + direction * (range * std::cos(inner)), direction,
                range * std::sin(inner), stroke.faint, stroke.thickness);
        }

        /// @brief エリアライト：発光面と、前側の届く距離の半球
        void DrawAreaRange(const Vector3& position, const Light& light, const Stroke& stroke)
        {
            const Vector3 normal = Direction(light.direction);
            Vector3 right{};
            Vector3 up{};
            LightManager::ComputeAreaLightBasis(normal, right, up);
            const Vector3 halfRight = right * (light.areaWidth * 0.5f);
            const Vector3 halfUp = up * (light.areaHeight * 0.5f);
            const Vector3 corners[4] = {
                position - halfRight - halfUp,
                position + halfRight - halfUp,
                position + halfRight + halfUp,
                position - halfRight + halfUp,
            };
            if (stroke.fill != 0) {
                SceneOverlay::SolidQuad(corners[0], corners[1], corners[2], corners[3], stroke.fill);
            }
            SceneOverlay::Polyline(corners, 4, true, stroke.strong, stroke.thickness);

            SceneOverlay::WireDisc(position, normal, light.range, stroke.faint, stroke.thickness);
            Arc(position, normal, right, light.range, kHalfPi, stroke.strong, stroke.thickness);
            Arc(position, normal, up, light.range, kHalfPi, stroke.strong, stroke.thickness);
        }

        /// @brief 平行光源：光の向きの平行な矢印
        void DrawDirectionalArrows(const Vector3& position, const Light& light, const Vector3& cameraPosition, const Stroke& stroke)
        {
            const Vector3 direction = Direction(light.direction);
            const Vector3 toCamera = cameraPosition - position;
            const float length = (std::max)(0.5f, std::sqrt(CoreEngine::Dot(toCamera, toCamera)) * kArrowLengthPerDistance);
            Vector3 u{};
            Vector3 v{};
            Basis(direction, u, v);

            Arrow(position, position + direction * length, stroke.strong, stroke.thickness);
            const float spread = length * 0.35f;
            for (const Vector3& side : { u, v, u * -1.0f, v * -1.0f }) {
                const Vector3 start = position + side * spread + direction * (length * 0.25f);
                Arrow(start, start + direction * (length * 0.75f), stroke.strong, stroke.thickness);
            }
        }

        void DrawShape(const Vector3& position, const Light& light, const Vector3& cameraPosition, const Stroke& stroke)
        {
            switch (light.type) {
            case LightType::Directional:
                DrawDirectionalArrows(position, light, cameraPosition, stroke);
                break;
            case LightType::Point:
                DrawPointRange(position, light, cameraPosition, stroke);
                break;
            case LightType::Spot:
                DrawSpotRange(position, light, stroke);
                break;
            case LightType::Area:
                DrawAreaRange(position, light, stroke);
                break;
            }
        }

        /// @brief 範囲の線（暗い縁を敷いてから色の線を描く。物に隠れた所は薄く）
        void DrawRange(const Vector3& position, const Light& light, bool enabled, const Vector3& cameraPosition)
        {
            const Vector3 tint = enabled ? IconTint(light.color) : kDisabledTint;
            const Stroke outline{ kOutline, kOutlineFaint, 0, kLineThickness + kOutlineWidth };
            const Stroke line{ ToColor(tint, 0.95f), ToColor(tint, 0.45f), ToColor(tint, 0.12f), kLineThickness };
            SceneOverlay::BeginOccluded(kHiddenAlpha);
            DrawShape(position, light, cameraPosition, outline);
            DrawShape(position, light, cameraPosition, line);
            SceneOverlay::EndOccluded();
        }

        /// @brief アイコンを回す角度（画像の向きを光の向きを画面へ写した向きへ合わせる）
        /// @param imageAngle 画像の中で光が出ていく向き（右が 0、下が π/2）
        float IconRotation(const IconEntry& entry, const Vector3& cameraPosition, float imageAngle)
        {
            const Vector3 toCamera = cameraPosition - entry.position;
            const float step = (std::max)(0.01f, std::sqrt(CoreEngine::Dot(toCamera, toCamera)) * 0.1f);
            ImVec2 tip{};
            if (!SceneOverlay::WorldToScreen(entry.position + Direction(entry.light->direction) * step, tip)) {
                return 0.0f;
            }
            const float dx = tip.x - entry.screen.x;
            const float dy = tip.y - entry.screen.y;
            if (dx * dx + dy * dy < kMinScreenDirection * kMinScreenDirection) {
                return 0.0f;
            }
            return std::atan2(dy, dx) - imageAngle;
        }

        void DrawIcon(ImDrawList& draw, const EditorImage& image, const ImVec2& center, float size, float angle, ImU32 tint)
        {
            const float half = size * 0.5f;
            if (!image.id) {
                draw.AddCircleFilled(center, half * 0.45f, tint);
                draw.AddCircle(center, half * 0.45f, IM_COL32(20, 24, 31, 255), 0, 1.5f);
                return;
            }
            const float c = std::cos(angle);
            const float s = std::sin(angle);
            const auto corner = [&](float x, float y) {
                return ImVec2(center.x + x * c - y * s, center.y + x * s + y * c);
            };
            draw.AddImageQuad(image.id, corner(-half, -half), corner(half, -half), corner(half, half), corner(-half, half),
                ImVec2(0.0f, 0.0f), ImVec2(1.0f, 0.0f), ImVec2(1.0f, 1.0f), ImVec2(0.0f, 1.0f), tint);
        }
    }

    void SceneLightGizmos::Initialize(GraphicsCore& graphics)
    {
        for (size_t i = 0; i < icons_.size(); ++i) {
            icons_[i] = LoadEditorImage(graphics, kIconFiles[i]);
        }
        registration_ = SceneViewTools::Get().Register([this](const SceneViewContext& context) { Draw(context); });
    }

    void SceneLightGizmos::Draw(const SceneViewContext& context)
    {
        if (!cvVisualize.Get() || !context.objects || !context.camera) {
            return;
        }

        const Vector3 cameraPosition = context.camera->GetPosition();

        // 選んでいるライトの範囲（アイコンが画面の外でも描く。アイコンより下）
        if (GameObject* const selected = context.selected; selected && !selected->IsMarkedForDestroy()) {
            if (const LightComponent* const light = selected->GetComponent<LightComponent>()) {
                DrawRange(selected->GetWorldPosition(), light->Get(), light->IsActiveAndEnabled(), cameraPosition);
            }
        }

        std::vector<IconEntry> entries;
        for (const auto& object : context.objects->GetAllObjects()) {
            if (!object || object->IsMarkedForDestroy()) {
                continue;
            }
            const LightComponent* const light = object->GetComponent<LightComponent>();
            if (!light) {
                continue;
            }
            IconEntry entry;
            entry.object = object.get();
            entry.light = &light->Get();
            entry.position = object->GetWorldPosition();
            if (!SceneOverlay::WorldToScreen(entry.position, entry.screen)) {
                continue;
            }
            const Vector3 toLight = entry.position - cameraPosition;
            entry.depth = CoreEngine::Dot(toLight, toLight);
            entry.selected = entry.object == context.selected;
            entry.enabled = light->IsActiveAndEnabled();
            entries.push_back(entry);
        }

        // 遠いものから描き、選んでいるものは最後に描く
        std::sort(entries.begin(), entries.end(), [](const IconEntry& a, const IconEntry& b) {
            if (a.selected != b.selected) {
                return b.selected;
            }
            return a.depth > b.depth;
        });

        ImDrawList& draw = *ImGui::GetWindowDrawList();
        const ImVec2 mouse = ImGui::GetMousePos();
        const IconEntry* hovered = nullptr;

        for (const IconEntry& entry : entries) {
            const float size = entry.selected ? kSelectedIconSize : kIconSize;
            const Vector3 tint = entry.enabled ? IconTint(entry.light->color) : kDisabledTint;
            const ImU32 color = ToColor(tint, entry.enabled ? 1.0f : 0.6f);

            if (entry.selected) {
                draw.AddCircleFilled(entry.screen, size * 0.62f, kSelectedRingFill);
                draw.AddCircle(entry.screen, size * 0.62f, kSelectedRing, 0, 2.0f);
            }

            float angle = 0.0f;
            if (entry.light->type == LightType::Spot) {
                angle = IconRotation(entry, cameraPosition, 0.0f);
            } else if (entry.light->type == LightType::Area) {
                angle = IconRotation(entry, cameraPosition, kHalfPi);
            }
            const size_t type = static_cast<size_t>(entry.light->type);
            DrawIcon(draw, type < icons_.size() ? icons_[type] : EditorImage{}, entry.screen, size, angle, color);

            const float dx = mouse.x - entry.screen.x;
            const float dy = mouse.y - entry.screen.y;
            const float radius = size * 0.5f;
            if (dx * dx + dy * dy <= radius * radius) {
                hovered = &entry;
            }
        }

        if (!hovered || !context.hovered || Gizmo::IsOver() || Gizmo::IsUsing()) {
            return;
        }

        // 名前（影つき）
        const std::string& name = hovered->object->GetName();
        const ImVec2 textSize = ImGui::CalcTextSize(name.c_str());
        const ImVec2 at(hovered->screen.x - textSize.x * 0.5f,
            hovered->screen.y + (hovered->selected ? kSelectedIconSize : kIconSize) * 0.5f + 2.0f);
        draw.AddText(ImVec2(at.x + 1.0f, at.y + 1.0f), IM_COL32(0, 0, 0, 200), name.c_str());
        draw.AddText(at, IM_COL32(235, 238, 245, 255), name.c_str());

        // アイコンの上の左クリックはこの道具が受け、そのオブジェクトを選ぶ
        SceneViewTools::Get().RequestMouse();
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (SceneDebugEditor* const editor = SceneAccess::SceneEditor()) {
                editor->SelectFromViewport(hovered->object);
            }
        }
    }
}

#endif // CORE_EDITOR
