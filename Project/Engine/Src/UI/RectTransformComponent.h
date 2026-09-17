#pragma once

#include "GameObject/Component/Core/IComponent.h"
#include "Reflection/Reflect.h"
#include "UI/UIElement.h"
#include "Math/Vector/Vector2.h"

#include <cstdint>

namespace CoreEngine
{
    /// @brief UI 要素の配置（アンカー・位置・基準点・大きさ・回転・描画順）を持つコンポーネント
    /// @details 座標は UI の基準解像度の画面座標（左上が原点・Y 下正）。
    ///          UI は 3D とスプライトの後に描き、UI 同士は描画順の小さい順に描く。
    class RectTransformComponent : public IComponent
    {
    public:
        /// @brief アンカーの名前（`UIAnchor` の並び）
        static constexpr const char* kAnchorNames[] = {
            "左上", "上", "右上", "左", "中央", "右", "左下", "下", "右下",
        };

        /// @brief オーナーの描画順へ足す UI の基準（3D とスプライトの描画順より大きくする）
        static constexpr int kRenderOrderBase = 20000;

        const char* GetTypeName() const override { return "RectTransform"; }

        // 回転はラジアンで持ち、インスペクタでは度で見せる
        REFLECT_BEGIN(RectTransformComponent, "UI トランスフォーム")
            REFLECT_ENUM_ACCESSOR("anchor", "アンカー", GetAnchor, SetAnchor, kAnchorNames)
            REFLECT_ACCESSOR("anchoredPosition", "位置", GetAnchoredPosition, SetAnchoredPosition,
                p.range = Speed(1.0f))
            REFLECT_ACCESSOR("pivot", "基準点", GetPivot, SetPivot, p.range = Range(0.0f, 1.0f, 0.01f))
            REFLECT_ACCESSOR("size", "大きさ", GetSize, SetSize, p.range = Range(0.0f, 16384.0f, 1.0f))
            REFLECT_ACCESSOR("rotation", "回転", GetRotation, SetRotation,
                p.range = Speed(0.01f), p.displayScale = kDegreesPerRadian)
            REFLECT_ACCESSOR("sortOrder", "描画順", GetSortOrder, SetSortOrder)
        REFLECT_END()

        /// @brief 描画順をオーナーへ反映する
        void Awake() override;

        // ===== 配置 =====

        UIAnchor GetAnchor() const { return layout_.anchor; }
        void SetAnchor(UIAnchor anchor) { layout_.anchor = anchor; }

        /// @brief アンカーを変え、画面上の見た目の位置を保つ
        /// @param canvasSize UI の基準解像度（`UIRenderer::GetScreenSize()`）
        void ChangeAnchorKeepingPosition(UIAnchor anchor, const Vector2& canvasSize);

        Vector2 GetAnchoredPosition() const { return layout_.anchoredPos; }
        void SetAnchoredPosition(const Vector2& position) { layout_.anchoredPos = position; }

        /// @brief 基準点（0,0 = 左上 / 0.5,0.5 = 中央 / 1,1 = 右下）
        Vector2 GetPivot() const { return layout_.pivot; }
        void SetPivot(const Vector2& pivot);

        /// @brief 大きさ（px）
        /// @note 中身に合わせて大きさを決める要素（文字）は、別の大きさにされたら合わせるのをやめる。
        Vector2 GetSize() const { return layout_.size; }
        void SetSize(const Vector2& size);

        /// @brief 回転（ラジアン）
        float GetRotation() const { return layout_.rotation; }
        void SetRotation(float radians) { layout_.rotation = radians; }

        /// @brief UI 同士での描画順（大きいほど手前）
        int GetSortOrder() const { return layout_.sortOrder; }
        void SetSortOrder(int order);

        const UILayout& GetLayout() const { return layout_; }

        // ===== 変更の検知 =====

        /// @brief 基準点か大きさが変わるたびに進む番号
        uint32_t GetShapeRevision() const { return shapeRevision_; }

    private:
        /// @brief 描画順をオーナーの描画順へ反映する
        void ApplyRenderOrder();

        UILayout layout_;
        uint32_t shapeRevision_ = 0;
    };
}
