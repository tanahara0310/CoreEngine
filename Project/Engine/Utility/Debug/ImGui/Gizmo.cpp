#define NOMINMAX
#include "Gizmo.h"
#include "WinApp/WinApp.h"
#include "MathCore.h"
#include <algorithm>
#include <cfloat>

namespace CoreEngine
{

using namespace CoreEngine::MathCore;

// ビューポート位置・サイズを保持する静的変数
static ImVec2 viewportPos;
static ImVec2 viewportSize;
// 操作モードを保持
static GizmoOperation currentOperation = GizmoOperation::Translate;

void Gizmo::SetOperation(GizmoOperation operation)
{
    currentOperation = operation;
}

GizmoOperation Gizmo::GetOperation()
{
    return currentOperation;
}

void Gizmo::Prepare(const ImVec2& pos, const ImVec2& size)
{

    // 渡されたビューポート情報を保存
    viewportPos = pos;
    viewportSize = size;

    // ギズモを現在のImGui描画リストに追加可能にする
    ImGuizmo::SetDrawlist();

    // 透視投影を使用（falseでパースあり）
    ImGuizmo::SetOrthographic(false);

    // 軸が反転しないよう設定
    ImGuizmo::AllowAxisFlip(false);

    // ビューポート位置とサイズに合わせてギズモの描画範囲を設定
    ImGuizmo::SetRect(pos.x, pos.y, size.x, size.y);
}

void Gizmo::RegisterTarget(WorldTransform* worldTransform, const BoundingSphere& bounds, bool* visible)
{
    targets_.emplace_back(worldTransform, bounds, visible);
}

void Gizmo::BeginFrame(const Matrix4x4& view, const Matrix4x4& proj)
{

    hoveredIndex_ = -1;
    PerformPicking(view, proj);
    DrawGizmo(view, proj);
}

bool Gizmo::HasSelection() const
{
    return selectedIndex_ >= 0;
}

void Gizmo::PerformPicking(const Matrix4x4& view, const Matrix4x4& proj)
{
    ImGuiIO& io = ImGui::GetIO();

    // マウス位置をビューポート原点基準で計算
    float localX = io.MousePos.x - viewportPos.x;
    float localY = io.MousePos.y - viewportPos.y;
    // ビューポート外ならピッキング処理せず抜ける
    if (localX < 0.0f || localY < 0.0f || localX > viewportSize.x || localY > viewportSize.y) {
        return;
    }

    // ビューポート内の正規化デバイス座標
    float ndcX = 2.0f * localX / viewportSize.x - 1.0f;
    float ndcY = 1.0f - 2.0f * localY / viewportSize.y;

    // カメラ位置取得
    Matrix4x4 invView = Matrix::Inverse(view);
    Vector3 rayOrigin = { invView.m[3][0], invView.m[3][1], invView.m[3][2] };

    // レイ方向算出
    Vector4 rayClip = { ndcX, ndcY, 1.0f, 1.0f };
    Matrix4x4 invProj = Matrix::Inverse(proj);
    Vector4 rayEye = CoordinateTransform::TransformCoord(rayClip, invProj);
    rayEye = { rayEye.x, rayEye.y, 1.0f, 0.0f };
    Vector4 rayWorld4 = CoordinateTransform::TransformCoord(rayEye, invView);
    Vector3 rayDir = { rayWorld4.x, rayWorld4.y, rayWorld4.z };
    float len = sqrtf(rayDir.x * rayDir.x + rayDir.y * rayDir.y + rayDir.z * rayDir.z);
    rayDir.x /= len;
    rayDir.y /= len;
    rayDir.z /= len;

    // 当たり判定
    for (size_t i = 0; i < targets_.size(); ++i) {
        if (targets_[i].visible && !*targets_[i].visible)
            continue;

        Vector3 worldCenter = targets_[i].wt->translate;
        float maxScale = (std::max)({ targets_[i].wt->scale.x,
            targets_[i].wt->scale.y,
            targets_[i].wt->scale.z });

        BoundingSphere worldSphere;
        worldSphere.center = worldCenter;
        worldSphere.radius = targets_[i].bounds.radius * maxScale;

        bool hit = RayIntersectsSphere(rayOrigin, rayDir, worldSphere);

        if (hit) {
            hoveredIndex_ = static_cast<int>(i);
            break;
        }
    }

    // クリックで選択/解除
    if (ImGui::IsMouseClicked(0)) {
        if (hoveredIndex_ >= 0) {
            selectedIndex_ = hoveredIndex_;
        } else {
            selectedIndex_ = -1;
        }
    }
}

void Gizmo::DrawGizmo(const Matrix4x4& view, const Matrix4x4& proj)
{

    if (selectedIndex_ < 0 || selectedIndex_ >= (int)targets_.size())
        return;

    // 非表示モデル選択中なら描画スキップ
    if (targets_[selectedIndex_].visible && !*targets_[selectedIndex_].visible)
        return;

    WorldTransform* target = targets_[selectedIndex_].wt;
    Matrix4x4 world = target->GetWorldMatrix();

    ImGuizmo::OPERATION op;
    switch (currentOperation) {
    case GizmoOperation::Translate:
        op = ImGuizmo::TRANSLATE;

        break;
    case GizmoOperation::Rotate:
        op = ImGuizmo::ROTATE;

        break;
    case GizmoOperation::Scale:
        op = ImGuizmo::SCALE;

        break;
    default:
        op = ImGuizmo::TRANSLATE; // デフォルトは移動
        break;
    }

    ImGuizmo::Manipulate(&view.m[0][0], &proj.m[0][0],
        op, ImGuizmo::LOCAL,
        &world.m[0][0]);

    if (ImGuizmo::IsUsing()) {
        Vector3 t, r, s;
        ImGuizmo::DecomposeMatrixToComponents(&world.m[0][0], &t.x, &r.x, &s.x);

        // → ラジアンに変換
        const float degToRad = std::numbers::pi_v<float> / 180.0f;
        r.x *= degToRad;
        r.y *= degToRad;
        r.z *= degToRad;

        switch (currentOperation) {
        case GizmoOperation::Translate:
            target->translate = t;
            break;
        case GizmoOperation::Rotate:
            target->rotate = r;
            break;
        case GizmoOperation::Scale:
            target->scale = s;
            break;
        }

        target->TransferMatrix();
    }
}

bool Gizmo::RayIntersectsSphere(const Vector3& rayOrigin, const Vector3& rayDir, const BoundingSphere& sphere)
{

    Vector3 oc = rayOrigin - sphere.center;
    float b = Vector::Dot(oc, rayDir);
    float c = Vector::Dot(oc, oc) - sphere.radius * sphere.radius;
    float disc = b * b - c;
    return disc >= 0.0f;
}
}
