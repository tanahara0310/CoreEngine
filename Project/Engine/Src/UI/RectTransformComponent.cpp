#include "pch.h"
#include "UI/RectTransformComponent.h"

#include "GameObject/Component/Core/ComponentFactory.h"
#include "GameObject/Component/Core/ComponentService.h"
#include "GameObject/GameObject.h"
#include "Graphics/Render/RenderManager.h"
#include "Graphics/Render/UI/UIRenderer.h"
#include "UI/UIMaskComponent.h"
#include "WinApp/WinApp.h"

#include <atomic>
#include <cmath>

REFLECT_REGISTER(CoreEngine::RectTransformComponent)
COMPONENT_REGISTER(CoreEngine::RectTransformComponent)

namespace CoreEngine
{
    using namespace CoreEngine::MathCore;

    namespace
    {
        /// @brief 親を指している UI トランスフォームの数
        std::atomic<int> gLinkedCount{ 0 };

        /// @brief 手前に描く UI トランスフォームの数
        std::atomic<int> gOnTopCount{ 0 };

        /// @brief 行列で点を移す（行ベクトル）
        Vector2 TransformPoint(const Matrix4x4& matrix, float x, float y)
        {
            return {
                x * matrix.m[0][0] + y * matrix.m[1][0] + matrix.m[3][0],
                x * matrix.m[0][1] + y * matrix.m[1][1] + matrix.m[3][1],
            };
        }

        /// @brief 基準点を原点にした四角
        UIRect MakeLocalRect(const Vector2& size, const Vector2& pivot)
        {
            UIRect rect;
            rect.min = { -pivot.x * size.x, -pivot.y * size.y };
            rect.max = { (1.0f - pivot.x) * size.x, (1.0f - pivot.y) * size.y };
            return rect;
        }

        /// @brief 測り直した値の丸めの誤差を落とす
        float Snap(float value, float step)
        {
            const float rounded = std::round(value / step) * step;
            return std::fabs(value - rounded) < step * 0.1f ? rounded : value;
        }

        /// @brief 親の四角の中で、基準点が target に来る位置（アンカーからのずらし）
        Vector2 SolveAnchoredPosition(const UILayout& layout, const UIRect& parentRect, const Vector2& target)
        {
            UILayout zero = layout;
            zero.anchoredPos = { 0.0f, 0.0f };
            const Vector2 base = zero.CalculatePivotPosition(parentRect);
            return { target.x - base.x, target.y - base.y };
        }

        /// @brief 実際の大きさが size になる大きさ（アンカーが開いている向きは差にする）
        Vector2 SolveSize(const UILayout& layout, const UIRect& parentRect, const Vector2& size)
        {
            const Vector2 parentSize = parentRect.Size();
            Vector2 result = size;
            if (layout.IsStretchedX()) {
                result.x = size.x - parentSize.x * (layout.anchorMax.x - layout.anchorMin.x);
            }
            if (layout.IsStretchedY()) {
                result.y = size.y - parentSize.y * (layout.anchorMax.y - layout.anchorMin.y);
            }
            return result;
        }
    }

    RectTransformComponent::~RectTransformComponent()
    {
        if (linked_) {
            gLinkedCount.fetch_sub(1, std::memory_order_relaxed);
        }
        if (drawnOnTop_) {
            gOnTopCount.fetch_sub(1, std::memory_order_relaxed);
        }
    }

    void RectTransformComponent::Awake()
    {
        ApplyRenderOrder();
    }

    // ===== 配置 =====

    UIAnchor RectTransformComponent::GetAnchor() const
    {
        UIAnchor anchor = UIAnchor::Center;
        if (TryGetAnchor(layout_.anchorMin, layout_.anchorMax, anchor)) {
            return anchor;
        }
        // 開いている・9 か所から外れているときは、アンカーの中央にいちばん近い 1 点
        const auto column = [](float lo, float hi) {
            const float center = (lo + hi) * 0.5f;
            return center < 0.25f ? 0 : (center < 0.75f ? 1 : 2);
        };
        const int x = column(layout_.anchorMin.x, layout_.anchorMax.x);
        const int y = column(layout_.anchorMin.y, layout_.anchorMax.y);
        return static_cast<UIAnchor>(y * 3 + x);
    }

    void RectTransformComponent::SetAnchor(UIAnchor anchor)
    {
        const Vector2 point = GetAnchorNormalized(anchor);
        SetAnchors(point, point);
    }

    void RectTransformComponent::ChangeAnchorKeepingPosition(UIAnchor anchor, const Vector2& canvasSize)
    {
        const Vector2 point = GetAnchorNormalized(anchor);
        ChangeAnchorsKeepingRect(point, point, canvasSize);
    }

    void RectTransformComponent::SetAnchorMin(const Vector2& anchor)
    {
        SetAnchors(anchor, layout_.anchorMax);
    }

    void RectTransformComponent::SetAnchorMax(const Vector2& anchor)
    {
        SetAnchors(layout_.anchorMin, anchor);
    }

    void RectTransformComponent::SetAnchors(const Vector2& anchorMin, const Vector2& anchorMax)
    {
        if (layout_.anchorMin.x == anchorMin.x && layout_.anchorMin.y == anchorMin.y
            && layout_.anchorMax.x == anchorMax.x && layout_.anchorMax.y == anchorMax.y) {
            return;
        }
        layout_.anchorMin = anchorMin;
        layout_.anchorMax = anchorMax;
        ++shapeRevision_;
    }

    void RectTransformComponent::ChangeAnchorsKeepingRect(const Vector2& anchorMin, const Vector2& anchorMax,
        const Vector2& canvasSize)
    {
        UIRect parentRect;
        Matrix4x4 parentMatrix;
        ComputeParentFrame(canvasSize, parentRect, parentMatrix);

        // 親の座標での基準点と実際の大きさを保ったまま、アンカーだけ変える
        const Vector2 pivotPosition = layout_.CalculatePivotPosition(parentRect);
        const Vector2 size = layout_.CalculateSize(parentRect);

        UILayout next = layout_;
        next.anchorMin = anchorMin;
        next.anchorMax = anchorMax;
        next.anchoredPos = SolveAnchoredPosition(next, parentRect, pivotPosition);
        next.size = SolveSize(next, parentRect, size);
        SetLayout(next);
    }

    void RectTransformComponent::SetPivot(const Vector2& pivot)
    {
        if (layout_.pivot.x == pivot.x && layout_.pivot.y == pivot.y) {
            return;
        }
        layout_.pivot = pivot;
        ++shapeRevision_;
    }

    void RectTransformComponent::SetSize(const Vector2& size)
    {
        if (layout_.size.x == size.x && layout_.size.y == size.y) {
            return;
        }
        layout_.size = size;
        ++shapeRevision_;
    }

    void RectTransformComponent::SetRectSize(const Vector2& size, const Vector2& canvasSize)
    {
        if (!layout_.IsStretchedX() && !layout_.IsStretchedY()) {
            SetSize(size);
            return;
        }
        UIRect parentRect;
        Matrix4x4 parentMatrix;
        ComputeParentFrame(canvasSize, parentRect, parentMatrix);
        SetSize(SolveSize(layout_, parentRect, size));
    }

    void RectTransformComponent::SetScale(const Vector2& scale)
    {
        layout_.scale = scale;
    }

    void RectTransformComponent::SetSortOrder(int order)
    {
        layout_.sortOrder = order;
        ApplyRenderOrder();
    }

    void RectTransformComponent::SetLayout(const UILayout& layout)
    {
        SetAnchors(layout.anchorMin, layout.anchorMax);
        SetPivot(layout.pivot);
        SetSize(layout.size);
        layout_.anchoredPos = layout.anchoredPos;
        layout_.rotation = layout.rotation;
        layout_.scale = layout.scale;
        if (layout_.sortOrder != layout.sortOrder) {
            SetSortOrder(layout.sortOrder);
        }
    }

    bool RectTransformComponent::IsSimpleLayout() const
    {
        UIAnchor anchor = UIAnchor::Center;
        return !parent_.GetObjectId().IsValid()
            && TryGetAnchor(layout_.anchorMin, layout_.anchorMax, anchor)
            && layout_.scale.x == 1.0f && layout_.scale.y == 1.0f;
    }

    // ===== 親子 =====

    RectTransformComponent* RectTransformComponent::GetParent() const
    {
        if (!linked_) {
            return nullptr;
        }
        RectTransformComponent* const parent = parent_.Get();
        return parent != this ? parent : nullptr;
    }

    bool RectTransformComponent::SetParent(RectTransformComponent* parent, bool keepCanvasPosition)
    {
        if (parent == GetParent() && (parent != nullptr || !linked_)) {
            return true;
        }
        if (parent && IsSelfOrDescendant(parent)) {
            return false;
        }

        const Vector2 canvasSize = FindCanvasSize();
        const Matrix4x4 oldMatrix = GetCanvasMatrix(canvasSize);
        const Vector2 oldSize = GetRectSize(canvasSize);

        parent_.Set(parent);
        UpdateLinked();

        if (!keepCanvasPosition) {
            return true;
        }

        UIRect parentRect;
        Matrix4x4 parentMatrix;
        const bool hasParent = ComputeParentFrame(canvasSize, parentRect, parentMatrix);
        const Matrix4x4 local = hasParent ? oldMatrix * Matrix::Inverse(parentMatrix) : oldMatrix;

        // 行列を拡大・回転・位置へ分ける（行ベクトル。1 行目 = 拡大 X × (cos, sin)）
        const float ax = local.m[0][0];
        const float ay = local.m[0][1];
        const float bx = local.m[1][0];
        const float by = local.m[1][1];
        const float scaleX = std::sqrt(ax * ax + ay * ay);
        const float determinant = ax * by - ay * bx;
        const float scaleY = (determinant < 0.0f ? -1.0f : 1.0f) * std::sqrt(bx * bx + by * by);
        const float rotation = scaleX > 0.0f ? std::atan2(ay, ax) : 0.0f;

        UILayout next = layout_;
        next.scale = { Snap(scaleX, 0.0001f), Snap(scaleY, 0.0001f) };
        next.rotation = std::fabs(rotation) < 1e-6f ? 0.0f : rotation;
        const Vector2 target = { local.m[3][0], local.m[3][1] };
        const Vector2 position = SolveAnchoredPosition(next, parentRect, target);
        next.anchoredPos = { Snap(position.x, 0.001f), Snap(position.y, 0.001f) };
        const Vector2 size = SolveSize(next, parentRect, oldSize);
        next.size = { Snap(size.x, 0.001f), Snap(size.y, 0.001f) };
        SetLayout(next);
        return true;
    }

    void RectTransformComponent::SetParentValue(const Reflection::ObjectRefValue& value)
    {
        parent_.SetValue(value, this);
        UpdateLinked();
    }

    bool RectTransformComponent::IsSelfOrDescendant(const RectTransformComponent* candidate) const
    {
        const RectTransformComponent* current = candidate;
        for (int depth = 0; current && depth < kMaxDepth; ++depth) {
            if (current == this) {
                return true;
            }
            current = current->GetParent();
        }
        return false;
    }

    bool RectTransformComponent::IsActiveInHierarchy() const
    {
        const RectTransformComponent* current = this;
        for (int depth = 0; current && depth < kMaxDepth; ++depth) {
            const GameObject* const owner = current->GetOwner();
            if (!owner || !owner->IsActive()) {
                return false;
            }
            current = current->GetParent();
        }
        return true;
    }

    bool RectTransformComponent::AnyHasParent()
    {
        return gLinkedCount.load(std::memory_order_relaxed) > 0;
    }

    void RectTransformComponent::SetDrawnOnTop(bool onTop)
    {
        if (drawnOnTop_ == onTop) {
            return;
        }
        drawnOnTop_ = onTop;
        if (onTop) {
            gOnTopCount.fetch_add(1, std::memory_order_relaxed);
        } else {
            gOnTopCount.fetch_sub(1, std::memory_order_relaxed);
        }
    }

    bool RectTransformComponent::AnyDrawnOnTop()
    {
        return gOnTopCount.load(std::memory_order_relaxed) > 0;
    }

    void RectTransformComponent::UpdateLinked()
    {
        const bool linked = parent_.GetObjectId().IsValid();
        if (linked == linked_) {
            return;
        }
        linked_ = linked;
        if (linked) {
            gLinkedCount.fetch_add(1, std::memory_order_relaxed);
        } else {
            gLinkedCount.fetch_sub(1, std::memory_order_relaxed);
        }
    }

    int RectTransformComponent::CollectChain(const RectTransformComponent* (&chain)[kMaxDepth]) const
    {
        // 自分から親へたどって並べ、根が先頭に来るよう裏返す
        int count = 0;
        const RectTransformComponent* current = this;
        while (current && count < kMaxDepth) {
            bool seen = false;
            for (int i = 0; i < count; ++i) {
                if (chain[i] == current) {
                    seen = true;
                    break;
                }
            }
            if (seen) {
                break;
            }
            chain[count++] = current;
            current = current->GetParent();
        }
        for (int i = 0; i < count / 2; ++i) {
            const RectTransformComponent* const swap = chain[i];
            chain[i] = chain[count - 1 - i];
            chain[count - 1 - i] = swap;
        }
        return count;
    }

    bool RectTransformComponent::ComputeParentFrame(const Vector2& canvasSize, UIRect& outParentRect,
        Matrix4x4& outParentMatrix) const
    {
        outParentRect = UILayout::CanvasRect(canvasSize);
        outParentMatrix = Matrix::Identity();
        if (!linked_) {
            return false;
        }

        const RectTransformComponent* chain[kMaxDepth] = {};
        const int count = CollectChain(chain);
        if (count <= 1) {
            return false;
        }

        // 根から順に、親の四角の中に自分を置き、自分の座標へ降りていく
        for (int i = 0; i < count - 1; ++i) {
            const UILayout& layout = chain[i]->layout_;
            const Vector2 pivotPosition = layout.CalculatePivotPosition(outParentRect);
            const Vector2 size = layout.CalculateSize(outParentRect);
            const Matrix4x4 matrix = Matrix::MakeAffine(
                Vector3(layout.scale.x, layout.scale.y, 1.0f),
                Vector3(0.0f, 0.0f, layout.rotation),
                Vector3(pivotPosition.x, pivotPosition.y, 0.0f));
            outParentMatrix = i == 0 ? matrix : matrix * outParentMatrix;
            outParentRect = MakeLocalRect(size, layout.pivot);
        }
        return true;
    }

    // ===== 形 =====

    Vector2 RectTransformComponent::GetRectSize(const Vector2& canvasSize) const
    {
        if (!layout_.IsStretchedX() && !layout_.IsStretchedY()) {
            return layout_.size;
        }
        UIRect parentRect;
        Matrix4x4 parentMatrix;
        ComputeParentFrame(canvasSize, parentRect, parentMatrix);
        return layout_.CalculateSize(parentRect);
    }

    UIRect RectTransformComponent::GetLocalRect(const Vector2& canvasSize) const
    {
        return MakeLocalRect(GetRectSize(canvasSize), layout_.pivot);
    }

    Matrix4x4 RectTransformComponent::GetCanvasMatrix(const Vector2& canvasSize) const
    {
        return BuildDrawMatrix({ 1.0f, 1.0f }, canvasSize);
    }

    Matrix4x4 RectTransformComponent::BuildDrawMatrix(const Vector2& contentScale, const Vector2& canvasSize) const
    {
        UIRect parentRect;
        Matrix4x4 parentMatrix;
        const bool hasParent = ComputeParentFrame(canvasSize, parentRect, parentMatrix);
        const Vector2 pivotPosition = layout_.CalculatePivotPosition(parentRect);
        const Matrix4x4 local = Matrix::MakeAffine(
            Vector3(contentScale.x * layout_.scale.x, contentScale.y * layout_.scale.y, 1.0f),
            Vector3(0.0f, 0.0f, layout_.rotation),
            Vector3(pivotPosition.x, pivotPosition.y, 0.0f));
        return hasParent ? local * parentMatrix : local;
    }

    Vector2 RectTransformComponent::GetCanvasPivot(const Vector2& canvasSize) const
    {
        UIRect parentRect;
        Matrix4x4 parentMatrix;
        const bool hasParent = ComputeParentFrame(canvasSize, parentRect, parentMatrix);
        const Vector2 pivotPosition = layout_.CalculatePivotPosition(parentRect);
        return hasParent ? TransformPoint(parentMatrix, pivotPosition.x, pivotPosition.y) : pivotPosition;
    }

    UIQuad RectTransformComponent::GetCanvasQuad(const Vector2& canvasSize) const
    {
        UIRect parentRect;
        Matrix4x4 parentMatrix;
        if (!ComputeParentFrame(canvasSize, parentRect, parentMatrix)) {
            return layout_.CalculateQuad(parentRect);
        }
        const UIQuad local = layout_.CalculateQuad(parentRect);
        UIQuad quad;
        quad.topLeft     = TransformPoint(parentMatrix, local.topLeft.x, local.topLeft.y);
        quad.topRight    = TransformPoint(parentMatrix, local.topRight.x, local.topRight.y);
        quad.bottomRight = TransformPoint(parentMatrix, local.bottomRight.x, local.bottomRight.y);
        quad.bottomLeft  = TransformPoint(parentMatrix, local.bottomLeft.x, local.bottomLeft.y);
        return quad;
    }

    UIRect RectTransformComponent::GetCanvasBounds(const Vector2& canvasSize) const
    {
        if (!linked_ || !GetParent()) {
            return layout_.CalculateRect(UILayout::CanvasRect(canvasSize));
        }
        return GetCanvasQuad(canvasSize).Bounds();
    }

    bool RectTransformComponent::ContainsCanvasPoint(const Vector2& point, const Vector2& canvasSize) const
    {
        UIRect clip;
        if (FindClipRect(canvasSize, clip) && !clip.Contains(point)) {
            return false;
        }
        UIRect parentRect;
        Matrix4x4 parentMatrix;
        if (!ComputeParentFrame(canvasSize, parentRect, parentMatrix)) {
            return layout_.ContainsPoint(point, parentRect);
        }
        Vector2 local;
        if (!CanvasToLocal(point, canvasSize, local)) {
            return false;
        }
        return MakeLocalRect(layout_.CalculateSize(parentRect), layout_.pivot).Contains(local);
    }

    bool RectTransformComponent::CanvasToLocal(const Vector2& canvasPoint, const Vector2& canvasSize,
        Vector2& outLocal) const
    {
        const Matrix4x4 matrix = GetCanvasMatrix(canvasSize);
        const float a = matrix.m[0][0];
        const float b = matrix.m[0][1];
        const float c = matrix.m[1][0];
        const float d = matrix.m[1][1];
        const float determinant = a * d - b * c;
        if (std::fabs(determinant) < 1e-12f) {
            return false;
        }
        const float dx = canvasPoint.x - matrix.m[3][0];
        const float dy = canvasPoint.y - matrix.m[3][1];
        outLocal = { (dx * d - dy * c) / determinant, (dy * a - dx * b) / determinant };
        return true;
    }

    bool RectTransformComponent::FindClipRect(const Vector2& canvasSize, UIRect& outClip) const
    {
        // 手前に描く物は、自分より上の親のマスクに切らせない
        if (!linked_ || drawnOnTop_) {
            return false;
        }
        bool found = false;
        const RectTransformComponent* current = GetParent();
        for (int depth = 0; current && depth < kMaxDepth; ++depth) {
            const GameObject* const owner = current->GetOwner();
            const UIMaskComponent* const mask = owner ? owner->GetComponent<UIMaskComponent>() : nullptr;
            if (mask && mask->IsEnabled()) {
                const Matrix4x4 matrix = current->GetCanvasMatrix(canvasSize);
                const float diagonal = std::fabs(matrix.m[0][0]) + std::fabs(matrix.m[1][1]);
                const bool rotated = std::fabs(matrix.m[0][1]) + std::fabs(matrix.m[1][0]) > diagonal * 1e-5f;
                if (rotated) {
                    mask->WarnRotatedOnce();
                }
                // 回っているマスクは 4 隅を囲む四角で切る
                const UIRect bounds = rotated
                    ? current->GetCanvasQuad(canvasSize).Bounds()
                    : current->GetCanvasBounds(canvasSize);
                outClip = found ? outClip.Intersect(bounds) : bounds;
                found = true;
            }
            if (current->drawnOnTop_) {
                break;
            }
            current = current->GetParent();
        }
        return found;
    }

    Vector2 RectTransformComponent::FindCanvasSize() const
    {
        if (RenderManager* const renderManager = FindService<RenderManager>(*this)) {
            if (const UIRenderer* const renderer = renderManager->GetRendererAs<UIRenderer>(RenderPassType::UI)) {
                return renderer->GetScreenSize();
            }
        }
        return { static_cast<float>(WinApp::kReferenceWidth), static_cast<float>(WinApp::kReferenceHeight) };
    }

    Vector2 RectTransformComponent::GetAnchorCanvasPoint(const Vector2& normalized, const Vector2& canvasSize) const
    {
        UIRect parentRect;
        Matrix4x4 parentMatrix;
        const bool hasParent = ComputeParentFrame(canvasSize, parentRect, parentMatrix);
        const Vector2 parentSize = parentRect.Size();
        const Vector2 point = { parentRect.min.x + parentSize.x * normalized.x, parentRect.min.y + parentSize.y * normalized.y };
        return hasParent ? TransformPoint(parentMatrix, point.x, point.y) : point;
    }

    Vector4 RectTransformComponent::GetCanvasRectForScript() const
    {
        const UIRect bounds = GetCanvasQuad(FindCanvasSize()).Bounds();
        const Vector2 size = bounds.Size();
        return { bounds.min.x, bounds.min.y, size.x, size.y };
    }

    // ===== 描く順 =====

    void RectTransformComponent::ApplyRenderOrder()
    {
        hasResolvedOrder_ = false;
        if (GameObject* const owner = GetOwner()) {
            owner->SetRenderOrder(kRenderOrderBase + layout_.sortOrder);
        }
    }

    void RectTransformComponent::ApplyResolvedRenderOrder(int order)
    {
        resolvedOrder_ = order - kRenderOrderBase;
        hasResolvedOrder_ = true;
        if (GameObject* const owner = GetOwner()) {
            owner->SetRenderOrder(order);
        }
    }
}
