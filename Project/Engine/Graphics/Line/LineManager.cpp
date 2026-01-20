#include "LineManager.h"
#include "Engine/Graphics/Render/Line/LineRendererPipeline.h"
#include "Math/MathCore.h"
#include <cassert>
#include <numbers>

#ifdef _DEBUG
#include <imgui.h>
#endif

namespace CoreEngine
{

LineManager& LineManager::GetInstance() {
    static LineManager instance;
    return instance;
}

void LineManager::Initialize(LineRendererPipeline* lineRenderer) {
    assert(lineRenderer != nullptr);
    lineRenderer_ = lineRenderer;
}

void LineManager::DrawLine(const Vector3& start, const Vector3& end, const Vector3& color, float alpha) {
    if (!lineRenderer_) {
        return;
    }

    Line line(start, end, color, alpha);
    lineRenderer_->AddLine(line);
}

void LineManager::DrawLine(const Line& line) {
    if (!lineRenderer_) {
        return;
    }

    lineRenderer_->AddLine(line);
}

void LineManager::DrawLines(const std::vector<Line>& lines) {
    if (!lineRenderer_) {
        return;
    }

    lineRenderer_->AddLines(lines);
}

void LineManager::DrawGrid(float size, int divisions, const Vector3& center, const Vector3& color, float alpha) {
if (!lineRenderer_) {
    return;
}

float halfSize = size * 0.5f;
    float step = size / static_cast<float>(divisions);

    // X方向のライン
    for (int i = 0; i <= divisions; ++i) {
        float z = -halfSize + step * i;
        Vector3 start = center + Vector3(-halfSize, 0.0f, z);
        Vector3 end = center + Vector3(halfSize, 0.0f, z);
        DrawLine(start, end, color, alpha);
    }

    // Z方向のライン
    for (int i = 0; i <= divisions; ++i) {
        float x = -halfSize + step * i;
        Vector3 start = center + Vector3(x, 0.0f, -halfSize);
        Vector3 end = center + Vector3(x, 0.0f, halfSize);
        DrawLine(start, end, color, alpha);
    }
}

void LineManager::DrawWireBox(const Vector3& center, const Vector3& size, const Vector3& color, float alpha) {
if (!lineRenderer_) {
    return;
}

Vector3 halfSize = size * 0.5f;

    // 8つの頂点を定義
    Vector3 vertices[8] = {
        center + Vector3(-halfSize.x, -halfSize.y, -halfSize.z),
        center + Vector3( halfSize.x, -halfSize.y, -halfSize.z),
        center + Vector3( halfSize.x,  halfSize.y, -halfSize.z),
        center + Vector3(-halfSize.x,  halfSize.y, -halfSize.z),
        center + Vector3(-halfSize.x, -halfSize.y,  halfSize.z),
        center + Vector3( halfSize.x, -halfSize.y,  halfSize.z),
        center + Vector3( halfSize.x,  halfSize.y,  halfSize.z),
        center + Vector3(-halfSize.x,  halfSize.y,  halfSize.z),
    };

    // 12本のエッジを描画
    // 下面
    DrawLine(vertices[0], vertices[1], color, alpha);
    DrawLine(vertices[1], vertices[2], color, alpha);
    DrawLine(vertices[2], vertices[3], color, alpha);
    DrawLine(vertices[3], vertices[0], color, alpha);

    // 上面
    DrawLine(vertices[4], vertices[5], color, alpha);
    DrawLine(vertices[5], vertices[6], color, alpha);
    DrawLine(vertices[6], vertices[7], color, alpha);
    DrawLine(vertices[7], vertices[4], color, alpha);

    // 縦のエッジ
    DrawLine(vertices[0], vertices[4], color, alpha);
    DrawLine(vertices[1], vertices[5], color, alpha);
    DrawLine(vertices[2], vertices[6], color, alpha);
    DrawLine(vertices[3], vertices[7], color, alpha);
}

void LineManager::DrawAxis(const Vector3& origin, float length, float alpha) {
if (!lineRenderer_) {
    return;
}

// X軸（赤）
    DrawLine(origin, origin + Vector3(length, 0.0f, 0.0f), {1.0f, 0.0f, 0.0f}, alpha);

    // Y軸（緑）
    DrawLine(origin, origin + Vector3(0.0f, length, 0.0f), {0.0f, 1.0f, 0.0f}, alpha);

    // Z軸（青）
    DrawLine(origin, origin + Vector3(0.0f, 0.0f, length), {0.0f, 0.0f, 1.0f}, alpha);
}

void LineManager::DrawCircle(const Vector3& center, float radius, int segments, const Vector3& color, float alpha) {
if (!lineRenderer_ || segments < 3) {
    return;
}

float angleStep = (std::numbers::pi_v<float> * 2.0f) / static_cast<float>(segments);

    for (int i = 0; i < segments; ++i) {
        float angle1 = angleStep * i;
        float angle2 = angleStep * (i + 1);

        Vector3 point1 = center + Vector3(
            std::cos(angle1) * radius,
            0.0f,
            std::sin(angle1) * radius
        );

        Vector3 point2 = center + Vector3(
            std::cos(angle2) * radius,
            0.0f,
            std::sin(angle2) * radius
        );

        DrawLine(point1, point2, color, alpha);
    }
}

void LineManager::DrawWireSphere(const Vector3& center, float radius, int segments, const Vector3& color, float alpha) {
if (!lineRenderer_ || segments < 3) {
    return;
}

// XY平面の円
float angleStep = (std::numbers::pi_v<float> * 2.0f) / static_cast<float>(segments);
    for (int i = 0; i < segments; ++i) {
        float angle1 = angleStep * i;
        float angle2 = angleStep * (i + 1);

        Vector3 point1 = center + Vector3(
            std::cos(angle1) * radius,
            std::sin(angle1) * radius,
            0.0f
        );

        Vector3 point2 = center + Vector3(
            std::cos(angle2) * radius,
            std::sin(angle2) * radius,
            0.0f
        );

        DrawLine(point1, point2, color, alpha);
    }

    // YZ平面の円
    for (int i = 0; i < segments; ++i) {
        float angle1 = angleStep * i;
        float angle2 = angleStep * (i + 1);

        Vector3 point1 = center + Vector3(
            0.0f,
            std::cos(angle1) * radius,
            std::sin(angle1) * radius
        );

        Vector3 point2 = center + Vector3(
            0.0f,
            std::cos(angle2) * radius,
            std::sin(angle2) * radius
        );

        DrawLine(point1, point2, color, alpha);
    }

    // XZ平面の円
    for (int i = 0; i < segments; ++i) {
        float angle1 = angleStep * i;
        float angle2 = angleStep * (i + 1);

        Vector3 point1 = center + Vector3(
            std::cos(angle1) * radius,
            0.0f,
            std::sin(angle1) * radius
        );

        Vector3 point2 = center + Vector3(
            std::cos(angle2) * radius,
            0.0f,
            std::sin(angle2) * radius
        );

        DrawLine(point1, point2, color, alpha);
    }
}

void LineManager::DrawCross(const Vector3& position, float size, const Vector3& color, float alpha) {
if (!lineRenderer_) {
    return;
}

float halfSize = size * 0.5f;

    // X軸方向
    DrawLine(
        position + Vector3(-halfSize, 0.0f, 0.0f),
        position + Vector3( halfSize, 0.0f, 0.0f),
        color, alpha
    );

    // Y軸方向
    DrawLine(
        position + Vector3(0.0f, -halfSize, 0.0f),
        position + Vector3(0.0f,  halfSize, 0.0f),
        color, alpha
    );

    // Z軸方向
    DrawLine(
        position + Vector3(0.0f, 0.0f, -halfSize),
        position + Vector3(0.0f, 0.0f,  halfSize),
        color, alpha
    );
}

void LineManager::ClearAll() {
    if (!lineRenderer_) {
        return;
    }

    lineRenderer_->ClearBatch();
}

#ifdef _DEBUG
void LineManager::DrawDebugShapes() {
    // グリッド
    if (debugGrid_.enabled) {
        DrawGrid(debugGrid_.size, debugGrid_.divisions, debugGrid_.center, debugGrid_.color, debugGrid_.alpha);
    }

    // 軸
    if (debugAxis_.enabled) {
        DrawAxis(debugAxis_.origin, debugAxis_.length, debugAxis_.alpha);
    }

    // ボックス
    if (debugBox_.enabled) {
        DrawWireBox(debugBox_.center, debugBox_.size, debugBox_.color, debugBox_.alpha);
    }

    // 円
    if (debugCircle_.enabled) {
        DrawCircle(debugCircle_.center, debugCircle_.radius, debugCircle_.segments, debugCircle_.color, debugCircle_.alpha);
    }

    // 球
    if (debugSphere_.enabled) {
        DrawWireSphere(debugSphere_.center, debugSphere_.radius, debugSphere_.segments, debugSphere_.color, debugSphere_.alpha);
    }

    // クロス
    if (debugCross_.enabled) {
        DrawCross(debugCross_.position, debugCross_.size, debugCross_.color, debugCross_.alpha);
    }
}

void LineManager::DrawImGui() {
    if (ImGui::Begin("ラインデバッグ")) {
        ImGui::Text("デバッグ形状のパラメータ設定");
        ImGui::Separator();

        // グリッド設定
        if (ImGui::CollapsingHeader("グリッド", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("##GridEnabled", &debugGrid_.enabled);
            ImGui::SameLine();
            ImGui::Text("有効");
            ImGui::DragFloat("サイズ##Grid", &debugGrid_.size, 0.1f, 1.0f, 100.0f);
            ImGui::DragInt("分割数##Grid", &debugGrid_.divisions, 1, 1, 50);
            ImGui::DragFloat3("中心##Grid", (float*)&debugGrid_.center, 0.1f);
            ImGui::ColorEdit3("色##Grid", (float*)&debugGrid_.color);
            ImGui::SliderFloat("透明度##Grid", &debugGrid_.alpha, 0.0f, 1.0f);
        }

        // 軸設定
        if (ImGui::CollapsingHeader("座標軸")) {
            ImGui::Checkbox("##AxisEnabled", &debugAxis_.enabled);
            ImGui::SameLine();
            ImGui::Text("有効");
            ImGui::DragFloat3("原点##Axis", (float*)&debugAxis_.origin, 0.1f);
            ImGui::DragFloat("長さ##Axis", &debugAxis_.length, 0.1f, 0.1f, 50.0f);
            ImGui::SliderFloat("透明度##Axis", &debugAxis_.alpha, 0.0f, 1.0f);
        }

        // ボックス設定
        if (ImGui::CollapsingHeader("ワイヤーボックス")) {
            ImGui::Checkbox("##BoxEnabled", &debugBox_.enabled);
            ImGui::SameLine();
            ImGui::Text("有効");
            ImGui::DragFloat3("中心##Box", (float*)&debugBox_.center, 0.1f);
            ImGui::DragFloat3("サイズ##Box", (float*)&debugBox_.size, 0.1f, 0.1f, 50.0f);
            ImGui::ColorEdit3("色##Box", (float*)&debugBox_.color);
            ImGui::SliderFloat("透明度##Box", &debugBox_.alpha, 0.0f, 1.0f);
        }

        // 円設定
        if (ImGui::CollapsingHeader("円")) {
            ImGui::Checkbox("##CircleEnabled", &debugCircle_.enabled);
            ImGui::SameLine();
            ImGui::Text("有効");
            ImGui::DragFloat3("中心##Circle", (float*)&debugCircle_.center, 0.1f);
            ImGui::DragFloat("半径##Circle", &debugCircle_.radius, 0.1f, 0.1f, 50.0f);
            ImGui::DragInt("セグメント数##Circle", &debugCircle_.segments, 1, 3, 64);
            ImGui::ColorEdit3("色##Circle", (float*)&debugCircle_.color);
            ImGui::SliderFloat("透明度##Circle", &debugCircle_.alpha, 0.0f, 1.0f);
        }

        // 球設定
        if (ImGui::CollapsingHeader("ワイヤー球")) {
            ImGui::Checkbox("##SphereEnabled", &debugSphere_.enabled);
            ImGui::SameLine();
            ImGui::Text("有効");
            ImGui::DragFloat3("中心##Sphere", (float*)&debugSphere_.center, 0.1f);
            ImGui::DragFloat("半径##Sphere", &debugSphere_.radius, 0.1f, 0.1f, 50.0f);
            ImGui::DragInt("セグメント数##Sphere", &debugSphere_.segments, 1, 3, 32);
            ImGui::ColorEdit3("色##Sphere", (float*)&debugSphere_.color);
            ImGui::SliderFloat("透明度##Sphere", &debugSphere_.alpha, 0.0f, 1.0f);
        }

        // クロス設定
        if (ImGui::CollapsingHeader("クロスマーカー")) {
            ImGui::Checkbox("##CrossEnabled", &debugCross_.enabled);
            ImGui::SameLine();
            ImGui::Text("有効");
            ImGui::DragFloat3("位置##Cross", (float*)&debugCross_.position, 0.1f);
            ImGui::DragFloat("サイズ##Cross", &debugCross_.size, 0.01f, 0.01f, 10.0f);
            ImGui::ColorEdit3("色##Cross", (float*)&debugCross_.color);
            ImGui::SliderFloat("透明度##Cross", &debugCross_.alpha, 0.0f, 1.0f);
        }

        ImGui::Separator();
        if (ImGui::Button("すべて無効化")) {
            debugGrid_.enabled = false;
            debugAxis_.enabled = false;
            debugBox_.enabled = false;
            debugCircle_.enabled = false;
            debugSphere_.enabled = false;
            debugCross_.enabled = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("すべて有効化")) {
            debugGrid_.enabled = true;
            debugAxis_.enabled = true;
            debugBox_.enabled = true;
            debugCircle_.enabled = true;
            debugSphere_.enabled = true;
            debugCross_.enabled = true;
        }
    }
    ImGui::End();
}
#endif

}
