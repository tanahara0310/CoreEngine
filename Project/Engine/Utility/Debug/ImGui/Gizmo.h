#pragma once
#include <vector>
#include <cfloat>

#include "Engine/WorldTransfom/WorldTransform.h"
#include "Engine/Math/BoundingBox.h"
#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>

#include <ImGuizmo.h>
#include <imgui.h>

// 簡易バウンディングスフィア(ローカル)

namespace CoreEngine
{
struct BoundingSphere {
    Vector3 center; ///< 中心座標
    float radius; ///< 半径
};

enum class GizmoOperation {
    Translate,
    Rotate,
    Scale
};

/// @brief ギズモ描画クラス
class Gizmo {
public: // メンバ関数
    static void SetOperation(GizmoOperation operation);

    static GizmoOperation GetOperation();

    /// @brief ギズモ描画準備（矩形設定も含む）
    /// @param pos ビューポート左上座標
    /// @param size ビューポートサイズ
    static void Prepare(const ImVec2& pos, const ImVec2& size);

    /// @brief 従来のスフィア当たり判定でターゲット登録
    /// @param worldTransform ワールドトランスフォーム
    /// @param bounds バウンディングスフィア
    /// @param visible 表示フラグ
    void RegisterTarget(WorldTransform* worldTransform, const BoundingSphere& bounds, bool* visible);

    void BeginFrame(const Matrix4x4& view, const Matrix4x4& proj);

    bool HasSelection() const;

private: // メンバ変数
    struct Target {
        WorldTransform* wt;
        BoundingSphere bounds;
        bool* visible;
        
        Target(WorldTransform* worldTransform, const BoundingSphere& boundingSphere, bool* visibleFlag)
            : wt(worldTransform), bounds(boundingSphere), visible(visibleFlag) {}
    };

    std::vector<Target> targets_;
    int hoveredIndex_ = -1;
    int selectedIndex_ = -1;

    static GizmoOperation operation_;

private: // メンバ関数
    void PerformPicking(const Matrix4x4& view, const Matrix4x4& proj);
    void DrawGizmo(const Matrix4x4& view, const Matrix4x4& proj);

    // レイと球の交差判定
    bool RayIntersectsSphere(const Vector3& rayOrigin, const Vector3& rayDir, const BoundingSphere& sphere);
};
}
