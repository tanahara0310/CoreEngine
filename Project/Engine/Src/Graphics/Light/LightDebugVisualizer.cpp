#include "pch.h"
#include "LightDebugVisualizer.h"

#include "LightManager.h"
#include "Math/MathCore.h"
#include "Graphics/Line/LineManager.h"
#include <algorithm>
#include <cmath>

namespace CoreEngine
{
    namespace
    {
        constexpr float kPi = MathCore::Constants::kPi;

#ifdef CORE_EDITOR
        // ==================== ギズモ描画ヘルパー ====================
        // 注意: LineManager::DrawLine の第 4 引数は「太さ」ではなく「透明度(alpha)」。

        /// @brief 方向ベクトルに直交する正規直交基底を作る
        void MakePerpBasis(const Vector3& dir, Vector3& outP1, Vector3& outP2)
        {
            Vector3 up = { 0.0f, 1.0f, 0.0f };
            if (std::abs(CoreEngine::Dot(dir, up)) > 0.99f) {
                up = { 1.0f, 0.0f, 0.0f };
            }
            outP1 = CoreEngine::Normalize(CoreEngine::Cross(dir, up));
            outP2 = CoreEngine::Cross(dir, outP1);
        }

        /// @brief 矢じり付きの矢印を描く（UE のライト方向表示風）
        void DrawArrow(LineManager& lm, const Vector3& from, const Vector3& to,
                       const Vector3& color, float alpha)
        {
            const Vector3 delta = to - from;
            const float len = std::sqrt(CoreEngine::Dot(delta, delta));
            if (len < 1e-4f) return;
            const Vector3 dir = delta * (1.0f / len);

            lm.DrawLine(from, to, color, alpha);

            Vector3 p1{}, p2{};
            MakePerpBasis(dir, p1, p2);
            const float head = std::min(0.35f, len * 0.25f);
            const Vector3 base = to - dir * head;
            lm.DrawLine(to, base + p1 * (head * 0.5f), color, alpha);
            lm.DrawLine(to, base - p1 * (head * 0.5f), color, alpha);
            lm.DrawLine(to, base + p2 * (head * 0.5f), color, alpha);
            lm.DrawLine(to, base - p2 * (head * 0.5f), color, alpha);
        }

        /// @brief 任意向きの円を描く（DrawCircle は XZ 平面固定のため自前で描く）
        void DrawOrientedCircle(LineManager& lm, const Vector3& center,
                                const Vector3& axis1, const Vector3& axis2, float radius,
                                int segments, const Vector3& color, float alpha)
        {
            for (int j = 0; j < segments; ++j)
            {
                const float a1 = (float)j / segments * 2.0f * kPi;
                const float a2 = (float)(j + 1) / segments * 2.0f * kPi;
                const Vector3 point1 = center + (axis1 * std::cos(a1) + axis2 * std::sin(a1)) * radius;
                const Vector3 point2 = center + (axis1 * std::cos(a2) + axis2 * std::sin(a2)) * radius;
                lm.DrawLine(point1, point2, color, alpha);
            }
        }

        /// @brief 光源本体のマーカー（小さなワイヤ球）
        void DrawBulbMarker(LineManager& lm, const Vector3& position, float size,
                            const Vector3& color, float alpha)
        {
            DrawOrientedCircle(lm, position, { 1,0,0 }, { 0,1,0 }, size, 12, color, alpha);
            DrawOrientedCircle(lm, position, { 1,0,0 }, { 0,0,1 }, size, 12, color, alpha);
            DrawOrientedCircle(lm, position, { 0,1,0 }, { 0,0,1 }, size, 12, color, alpha);
        }
#endif
    }

    // ==================== デバッグ可視化（UE 風ギズモ） ====================
    // 方針: 光源本体 = 小さなワイヤ球、方向 = 矢じり付き矢印、範囲 = ワイヤ形状。
    //       選択中は不透明で全要素、非選択は半透明でマーカー＋方向のみの簡略表示。

    void LightDebugVisualizer::DrawVisualization(const Light& light, bool selected)
    {
#ifdef CORE_EDITOR
        if (!light.enabled) {
            return;
        }

        switch (light.type) {
        case LightType::Directional: DrawDirectionalLightVisualization(light, selected); break;
        case LightType::Point:       DrawPointLightVisualization(light, selected); break;
        case LightType::Spot:        DrawSpotLightVisualization(light, selected); break;
        case LightType::Area:        DrawAreaLightVisualization(light, selected); break;
        }
#else
        (void)light;
        (void)selected;
#endif
    }

    void LightDebugVisualizer::DrawDirectionalLightVisualization(const Light& light, bool selected)
    {
        (void)light;
        (void)selected;
#ifdef CORE_EDITOR
        auto& lm = LineManager::GetInstance();
        const Vector3 color = light.color;
        const float alpha = selected ? 1.0f : 0.45f;

        // 太陽アイコン: position をアンカーに小さなワイヤ球＋放射状の短い光線
        const Vector3 anchor = light.position;
        const Vector3 dir = CoreEngine::Normalize(light.direction);

        DrawBulbMarker(lm, anchor, 0.35f, color, alpha);

        Vector3 p1{}, p2{};
        MakePerpBasis(dir, p1, p2);
        const int rays = 8;
        for (int j = 0; j < rays; ++j)
        {
            const float a = (float)j / rays * 2.0f * kPi;
            const Vector3 rayDir = p1 * std::cos(a) + p2 * std::sin(a);
            lm.DrawLine(anchor + rayDir * 0.5f, anchor + rayDir * 0.85f, color, alpha);
        }

        // 方向矢印（本体）
        DrawArrow(lm, anchor, anchor + dir * 3.0f, color, alpha);

        // 選択中は平行光線の矢印を追加（UE のディレクショナルライト風）
        if (selected)
        {
            const Vector3 offsets[4] = {
                p1 * 0.9f, p1 * -0.9f, p2 * 0.9f, p2 * -0.9f
            };
            for (const Vector3& off : offsets)
            {
                DrawArrow(lm, anchor + off + dir * 0.8f, anchor + off + dir * 2.8f, color, 0.5f);
            }
        }
#endif
    }

    void LightDebugVisualizer::DrawPointLightVisualization(const Light& light, bool selected)
    {
        (void)light;
        (void)selected;
#ifdef CORE_EDITOR
        auto& lm = LineManager::GetInstance();
        const Vector3 color = light.color;

        // 光源本体
        DrawBulbMarker(lm, light.position, 0.25f, color, selected ? 1.0f : 0.45f);
        if (!selected) return;

        // 到達距離のワイヤ球（3大円）
        DrawOrientedCircle(lm, light.position, { 1,0,0 }, { 0,1,0 }, light.range, 32, color, 0.8f);
        DrawOrientedCircle(lm, light.position, { 1,0,0 }, { 0,0,1 }, light.range, 32, color, 0.8f);
        DrawOrientedCircle(lm, light.position, { 0,1,0 }, { 0,0,1 }, light.range, 32, color, 0.8f);
#endif
    }

    void LightDebugVisualizer::DrawSpotLightVisualization(const Light& light, bool selected)
    {
        (void)light;
        (void)selected;
#ifdef CORE_EDITOR
        auto& lm = LineManager::GetInstance();
        const Vector3 color = light.color;
        const float alpha = selected ? 1.0f : 0.45f;

        const Vector3 dir = CoreEngine::Normalize(light.direction);

        // 光源本体＋方向矢印
        DrawBulbMarker(lm, light.position, 0.2f, color, alpha);
        DrawArrow(lm, light.position,
            light.position + dir * std::min(1.5f, light.range * 0.3f), color, alpha);
        if (!selected) return;

        // コーン（外角: 母線4本＋端円 / 減衰開始角: 内側の淡い円）
        const float outerAngle = light.outerConeAngleDeg * (kPi / 180.0f);
        const float innerAngle = light.innerConeAngleDeg * (kPi / 180.0f);
        const float outerRadius = std::tan(outerAngle) * light.range;
        const float innerRadius = std::tan(innerAngle) * light.range;
        const Vector3 coneEnd = light.position + dir * light.range;

        Vector3 p1{}, p2{};
        MakePerpBasis(dir, p1, p2);

        DrawOrientedCircle(lm, coneEnd, p1, p2, outerRadius, 32, color, 0.9f);
        for (int j = 0; j < 4; ++j)
        {
            const float a = (float)j / 4.0f * 2.0f * kPi;
            const Vector3 rim = coneEnd + (p1 * std::cos(a) + p2 * std::sin(a)) * outerRadius;
            lm.DrawLine(light.position, rim, color, 0.9f);
        }

        DrawOrientedCircle(lm, coneEnd, p1, p2, innerRadius, 32, color, 0.35f);
#endif
    }

    void LightDebugVisualizer::DrawAreaLightVisualization(const Light& light, bool selected)
    {
        (void)light;
        (void)selected;
#ifdef CORE_EDITOR
        auto& lm = LineManager::GetInstance();
        const Vector3 color = light.color;
        const float alpha = selected ? 1.0f : 0.45f;

        // GPU 転送と同じ規則で発光面の基底を導出する
        const Vector3 normal = CoreEngine::Normalize(light.direction);
        Vector3 right{}, up{};
        LightManager::ComputeAreaLightBasis(light.direction, right, up);

        const Vector3 halfRight = right * (light.areaWidth * 0.5f);
        const Vector3 halfUp = up * (light.areaHeight * 0.5f);

        const Vector3 corners[4] = {
            light.position - halfRight - halfUp,
            light.position + halfRight - halfUp,
            light.position + halfRight + halfUp,
            light.position - halfRight + halfUp,
        };

        // 発光面の矩形＋法線矢印（UE の Rect Light 風）
        for (int j = 0; j < 4; ++j)
        {
            lm.DrawLine(corners[j], corners[(j + 1) % 4], color, alpha);
        }
        DrawArrow(lm, light.position, light.position + normal * 1.0f, color, alpha);
        if (!selected) return;

        // 放射方向のプレビュー: 少し先へ投影した淡い矩形と4隅の接続線
        const float previewDist = std::min(light.range * 0.25f, 2.0f);
        for (int j = 0; j < 4; ++j)
        {
            const Vector3 projected0 = corners[j] + normal * previewDist;
            const Vector3 projected1 = corners[(j + 1) % 4] + normal * previewDist;
            lm.DrawLine(projected0, projected1, color, 0.35f);
            lm.DrawLine(corners[j], projected0, color, 0.35f);
        }
#endif
    }
}
