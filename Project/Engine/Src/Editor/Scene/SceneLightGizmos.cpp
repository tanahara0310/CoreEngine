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
        constexpr float kLineThickness = 1.5f;

        /// 平行光源の矢印の長さ（カメラからの距離に掛ける）
        constexpr float kArrowLengthPerDistance = 0.15f;

        /// 円弧を分ける数
        constexpr int kArcSegments = 24;

        /// 向きを画面へ写した長さがこれより短いときは、アイコンを回さない [px]
        constexpr float kMinScreenDirection = 2.0f;

        constexpr float kHalfPi = std::numbers::pi_v<float> * 0.5f;

        constexpr ImU32 kSelectedRing = IM_COL32(240, 180, 64, 255);
        constexpr ImU32 kSelectedRingFill = IM_COL32(240, 180, 64, 64);

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
        void Arc(const Vector3& center, const Vector3& axis, const Vector3& side, float radius, float angle, ImU32 color)
        {
            Vector3 previous = center + (axis * std::cos(-angle) + side * std::sin(-angle)) * radius;
            for (int i = 1; i <= kArcSegments; ++i) {
                const float a = -angle + 2.0f * angle * static_cast<float>(i) / static_cast<float>(kArcSegments);
                const Vector3 point = center + (axis * std::cos(a) + side * std::sin(a)) * radius;
                SceneOverlay::Line(previous, point, color, kLineThickness);
                previous = point;
            }
        }

        /// @brief 矢じり付きの矢印
        void Arrow(const Vector3& from, const Vector3& to, ImU32 color)
        {
            SceneOverlay::Line(from, to, color, kLineThickness);
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
                SceneOverlay::Line(to, base + side * (head * 0.5f), color, kLineThickness);
                SceneOverlay::Line(to, base - side * (head * 0.5f), color, kLineThickness);
            }
        }

        /// @brief 点光源：届く距離の球
        void DrawPointRange(const Vector3& position, const Light& light, ImU32 color)
        {
            SceneOverlay::WireSphere(position, light.range, color, kLineThickness);
        }

        /// @brief スポットライト：外側の角度の円すい（先は届く距離の球の一部）と、内側の角度の縁
        void DrawSpotRange(const Vector3& position, const Light& light, ImU32 color, ImU32 faint)
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
            SceneOverlay::WireDisc(rimCenter, direction, rimRadius, color, kLineThickness);
            for (const Vector3& side : { u, v, u * -1.0f, v * -1.0f }) {
                SceneOverlay::Line(position, rimCenter + side * rimRadius, color, kLineThickness);
            }
            Arc(position, direction, u, range, outer, color);
            Arc(position, direction, v, range, outer, color);

            SceneOverlay::WireDisc(position + direction * (range * std::cos(inner)), direction,
                range * std::sin(inner), faint, kLineThickness);
        }

        /// @brief エリアライト：発光面と、前側の届く距離の半球
        void DrawAreaRange(const Vector3& position, const Light& light, ImU32 color, ImU32 faint, ImU32 fill)
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
            SceneOverlay::SolidQuad(corners[0], corners[1], corners[2], corners[3], fill);
            SceneOverlay::Polyline(corners, 4, true, color, kLineThickness);

            SceneOverlay::WireDisc(position, normal, light.range, faint, kLineThickness);
            Arc(position, normal, right, light.range, kHalfPi, color);
            Arc(position, normal, up, light.range, kHalfPi, color);
        }

        /// @brief 平行光源：光の向きの平行な矢印
        void DrawDirectionalArrows(const Vector3& position, const Light& light, const Vector3& cameraPosition, ImU32 color)
        {
            const Vector3 direction = Direction(light.direction);
            const Vector3 toCamera = cameraPosition - position;
            const float length = (std::max)(0.5f, std::sqrt(CoreEngine::Dot(toCamera, toCamera)) * kArrowLengthPerDistance);
            Vector3 u{};
            Vector3 v{};
            Basis(direction, u, v);

            Arrow(position, position + direction * length, color);
            const float spread = length * 0.35f;
            for (const Vector3& side : { u, v, u * -1.0f, v * -1.0f }) {
                const Vector3 start = position + side * spread + direction * (length * 0.25f);
                Arrow(start, start + direction * (length * 0.75f), color);
            }
        }

        void DrawRange(const Vector3& position, const Light& light, bool enabled, const Vector3& cameraPosition)
        {
            const Vector3 tint = enabled ? IconTint(light.color) : kDisabledTint;
            const ImU32 color = ToColor(tint, 0.9f);
            const ImU32 faint = ToColor(tint, 0.35f);
            switch (light.type) {
            case LightType::Directional:
                DrawDirectionalArrows(position, light, cameraPosition, color);
                break;
            case LightType::Point:
                DrawPointRange(position, light, color);
                break;
            case LightType::Spot:
                DrawSpotRange(position, light, color, faint);
                break;
            case LightType::Area:
                DrawAreaRange(position, light, color, faint, ToColor(tint, 0.12f));
                break;
            }
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
