#pragma once

#include "GameObject/Component/Core/IComponent.h"
#include "GameObject/Component/Core/ObjectRef.h"
#include "Math/Matrix/Matrix4x4.h"
#include "Math/Vector/Vector2.h"
#include "Math/Vector/Vector4.h"
#include "Reflection/Reflect.h"
#include "UI/UIElement.h"

#include <cstdint>

namespace CoreEngine
{
    /// @brief UI 要素の配置（親・アンカー・位置・基準点・大きさ・回転・拡大・描画順）を持つコンポーネント
    /// @details 座標は UI の基準解像度の画面座標（左上が原点・Y 下正）。親の UI トランスフォームがあれば、
    ///          位置と大きさは親の四角から測り、親の移動・回転・拡大が子に掛かる。
    ///          UI は 3D とスプライトの後に描き、UI 同士は描画順の小さい順に描く。子は親のすぐ後に描く。
    class RectTransformComponent : public IComponent
    {
    public:
        /// @brief アンカーの名前（`UIAnchor` の並び）
        static constexpr const char* kAnchorNames[] = {
            "左上", "上", "右上", "左", "中央", "右", "左下", "下", "右下",
        };

        /// @brief オーナーの描画順へ足す UI の基準（3D とスプライトの描画順より大きくする）
        static constexpr int kRenderOrderBase = 20000;

        /// @brief 親をたどる段数の上限
        static constexpr int kMaxDepth = 64;

        RectTransformComponent() = default;
        ~RectTransformComponent() override;

        const char* GetTypeName() const override { return "RectTransform"; }

        // 回転はラジアンで持ち、インスペクタでは度で見せる。
        // 親が無く、アンカーが 9 か所のどれか 1 点で、拡大が 1 の要素は、anchor（番号）と size だけで保存する
        REFLECT_BEGIN(RectTransformComponent, "UI トランスフォーム")
            REFLECT_ENUM_ACCESSOR("anchor", "アンカー", GetAnchor, SetAnchor, kAnchorNames,
                p.flags = ::CoreEngine::Reflection::PropertyFlags::Hidden,
                p.saveWhen = [](const void* o) { return static_cast<const Self*>(o)->IsSimpleLayout(); })
            REFLECT_ACCESSOR("anchorMin", "アンカー（左上）", GetAnchorMin, SetAnchorMin,
                p.range = Range(0.0f, 1.0f, 0.01f),
                p.saveWhen = [](const void* o) { return !static_cast<const Self*>(o)->IsSimpleLayout(); })
            REFLECT_ACCESSOR("anchorMax", "アンカー（右下）", GetAnchorMax, SetAnchorMax,
                p.range = Range(0.0f, 1.0f, 0.01f),
                p.saveWhen = [](const void* o) { return !static_cast<const Self*>(o)->IsSimpleLayout(); })
            REFLECT_ACCESSOR("anchoredPosition", "位置", GetAnchoredPosition, SetAnchoredPosition,
                p.range = Speed(1.0f))
            REFLECT_ACCESSOR("pivot", "基準点", GetPivot, SetPivot, p.range = Range(0.0f, 1.0f, 0.01f))
            REFLECT_ACCESSOR("size", "大きさ", GetSize, SetSize, p.range = Range(-16384.0f, 16384.0f, 1.0f),
                p.tooltip = "アンカーが開いている向きは、アンカーの間の幅（高さ）との差",
                p.saveWhen = [](const void* o) { return !static_cast<const Self*>(o)->IsSizeDriven(); })
            REFLECT_ACCESSOR("sizeDelta", "大きさの差", GetSize, SetSize,
                p.flags = ::CoreEngine::Reflection::PropertyFlags::Hidden | ::CoreEngine::Reflection::PropertyFlags::NoSave)
            REFLECT_ACCESSOR("rotation", "回転", GetRotation, SetRotation,
                p.range = Speed(0.01f), p.displayScale = kDegreesPerRadian)
            REFLECT_ACCESSOR("scale", "拡大", GetScale, SetScale, p.range = Speed(0.01f),
                p.saveWhen = [](const void* o) { return !static_cast<const Self*>(o)->IsSimpleLayout(); })
            REFLECT_ACCESSOR("sortOrder", "描画順", GetSortOrder, SetSortOrder)
            REFLECT_OBJECT_REF_ACCESSOR("parent", "親", GetParentValue, SetParentValue, ObjectRef<RectTransformComponent>,
                p.saveWhen = [](const void* o) { return !static_cast<const Self*>(o)->IsSimpleLayout(); })
            REFLECT_READONLY_ACCESSOR("rect", "画面上の四角", GetCanvasRectForScript)
        REFLECT_END()

        /// @brief 描画順をオーナーへ反映する
        void Awake() override;

        // ===== 配置 =====

        /// @brief アンカー（2 点が 9 か所のどれか 1 点に重なっていればその種別、それ以外は近い 1 点）
        UIAnchor GetAnchor() const;

        /// @brief アンカーを 9 か所のどれか 1 点にする
        void SetAnchor(UIAnchor anchor);

        /// @brief アンカーを変え、画面上の見た目の位置を保つ
        /// @param canvasSize UI の基準解像度（`UIRenderer::GetScreenSize()`）
        void ChangeAnchorKeepingPosition(UIAnchor anchor, const Vector2& canvasSize);

        /// @brief アンカーの左上（親の四角の中の割合。0,0 = 左上 / 1,1 = 右下）
        Vector2 GetAnchorMin() const { return layout_.anchorMin; }
        void SetAnchorMin(const Vector2& anchor);

        /// @brief アンカーの右下（親の四角の中の割合）
        Vector2 GetAnchorMax() const { return layout_.anchorMax; }
        void SetAnchorMax(const Vector2& anchor);

        /// @brief アンカーの 2 点をまとめて変える
        void SetAnchors(const Vector2& anchorMin, const Vector2& anchorMax);

        /// @brief アンカーの 2 点を変え、画面上の四角を保つ（位置と大きさを測り直す）
        void ChangeAnchorsKeepingRect(const Vector2& anchorMin, const Vector2& anchorMax, const Vector2& canvasSize);

        Vector2 GetAnchoredPosition() const { return layout_.anchoredPos; }
        void SetAnchoredPosition(const Vector2& position) { layout_.anchoredPos = position; }

        /// @brief 基準点（0,0 = 左上 / 0.5,0.5 = 中央 / 1,1 = 右下）
        Vector2 GetPivot() const { return layout_.pivot; }
        void SetPivot(const Vector2& pivot);

        /// @brief 大きさ（px）。アンカーが開いている向きは、アンカーの間の幅（高さ）との差
        /// @note 中身に合わせて大きさを決める要素（文字）は、別の大きさにされたら合わせるのをやめる。
        Vector2 GetSize() const { return layout_.size; }
        void SetSize(const Vector2& size);

        /// @brief 実際の大きさが size になるように大きさを決める（アンカーが 1 点なら `SetSize` と同じ）
        void SetRectSize(const Vector2& size, const Vector2& canvasSize);

        /// @brief 大きさを中身（文字）が決めているか（決めている間は大きさを保存しない）
        bool IsSizeDriven() const { return sizeDriven_; }
        void SetSizeDriven(bool driven) { sizeDriven_ = driven; }

        /// @brief 回転（ラジアン）
        float GetRotation() const { return layout_.rotation; }
        void SetRotation(float radians) { layout_.rotation = radians; }

        /// @brief 拡大（基準点を中心に、自分と子を拡大する）
        Vector2 GetScale() const { return layout_.scale; }
        void SetScale(const Vector2& scale);

        /// @brief UI 同士での描画順（大きいほど手前。子は兄弟の中で比べる）
        int GetSortOrder() const { return layout_.sortOrder; }
        void SetSortOrder(int order);

        const UILayout& GetLayout() const { return layout_; }

        /// @brief 配置をまとめて置き換える（親は変えない）
        void SetLayout(const UILayout& layout);

        /// @brief 今の書き方（anchor と size だけ）で保存できる配置か
        /// @details 親が無く、アンカーが 9 か所のどれか 1 点で、拡大が 1 のとき。
        bool IsSimpleLayout() const;

        // ===== 親子 =====

        /// @brief 親の UI トランスフォーム（無い・見つからないなら nullptr）
        RectTransformComponent* GetParent() const;

        /// @brief 親を付け替える（nullptr で親を外す）
        /// @param keepCanvasPosition true なら画面上の位置・回転・大きさを保つように配置を測り直す
        /// @return 自分自身か自分の子孫を指したときは付け替えずに false
        bool SetParent(RectTransformComponent* parent, bool keepCanvasPosition);

        /// @brief 親を指す値（型記述子とやり取りする値）
        Reflection::ObjectRefValue GetParentValue() const { return parent_.GetValue(); }
        void SetParentValue(const Reflection::ObjectRefValue& value);

        /// @brief candidate が自分自身か自分の子孫か
        bool IsSelfOrDescendant(const RectTransformComponent* candidate) const;

        /// @brief 自分と親がすべてアクティブか（親が非アクティブなら子も描かず押せない）
        bool IsActiveInHierarchy() const;

        /// @brief 親を指している UI トランスフォームが 1 つでもあるか（無ければ描く順の並べ直しを省く）
        static bool AnyHasParent();

        // ===== 形（キャンバス座標） =====

        /// @brief 実際の大きさ（アンカーが開いていれば親の大きさから決まる。拡大は掛けない）
        Vector2 GetRectSize(const Vector2& canvasSize) const;

        /// @brief 自分の四角（基準点を原点にした自分の座標。拡大と回転は掛けない）
        UIRect GetLocalRect(const Vector2& canvasSize) const;

        /// @brief 自分の座標（基準点が原点・px）からキャンバス座標への行列（自分の拡大と回転を含む）
        Matrix4x4 GetCanvasMatrix(const Vector2& canvasSize) const;

        /// @brief 大きさ 1 の中身を描くときの、中身の座標からキャンバス座標への行列
        /// @param contentScale 中身の 1 を何 px にするか（画像なら大きさ、文字ならフォントの大きさ）
        Matrix4x4 BuildDrawMatrix(const Vector2& contentScale, const Vector2& canvasSize) const;

        /// @brief 基準点のキャンバス座標
        Vector2 GetCanvasPivot(const Vector2& canvasSize) const;

        /// @brief 回転を反映した 4 隅（キャンバス座標）
        UIQuad GetCanvasQuad(const Vector2& canvasSize) const;

        /// @brief 回転を見ない矩形（親が無ければ基準点と大きさの矩形、親があれば 4 隅を囲む矩形）
        UIRect GetCanvasBounds(const Vector2& canvasSize) const;

        /// @brief 点が要素の上にあり、親の `UIMask` の外でもないか（キャンバス座標）
        bool ContainsCanvasPoint(const Vector2& point, const Vector2& canvasSize) const;

        /// @brief キャンバス座標を自分の座標（基準点が原点・拡大と回転を外す）へ戻す
        /// @return 潰れていて戻せないときは false
        bool CanvasToLocal(const Vector2& canvasPoint, const Vector2& canvasSize, Vector2& outLocal) const;

        /// @brief 親の `UIMask` が切る範囲（キャンバス座標。親が何重にも切るなら重なり）
        /// @return 切る親が無ければ false
        bool FindClipRect(const Vector2& canvasSize, UIRect& outClip) const;

        /// @brief UI の基準解像度（UIRenderer が無ければエンジン既定の基準解像度）
        Vector2 FindCanvasSize() const;

        /// @brief 親の四角（親の座標）と、親の座標からキャンバス座標への行列を求める
        /// @return 親が無ければ false（親の四角はキャンバス全体・行列は単位行列）
        bool ComputeParentFrame(const Vector2& canvasSize, UIRect& outParentRect, Matrix4x4& outParentMatrix) const;

        /// @brief 親の四角の中の割合の点（アンカーと同じ書き方）のキャンバス座標
        Vector2 GetAnchorCanvasPoint(const Vector2& normalized, const Vector2& canvasSize) const;

        /// @brief 画面上の四角（x, y = 左上・z, w = 幅と高さ。回っているときは 4 隅を囲む四角）
        Vector4 GetCanvasRectForScript() const;

        // ===== 描く順 =====

        /// @brief 描画順（`kRenderOrderBase` + 描画順）をオーナーへ反映する
        void ApplyRenderOrder();

        /// @brief 描く順を並べ直した結果をオーナーへ書く（親子のある UI が使う）
        void ApplyResolvedRenderOrder(int order);

        /// @brief ポインタの当たりで比べる順（大きいほど手前。並べ直していれば描く順、いなければ描画順）
        int GetPointerOrder() const { return hasResolvedOrder_ ? resolvedOrder_ : layout_.sortOrder; }

        // ===== 変更の検知 =====

        /// @brief 基準点か大きさが変わるたびに進む番号
        uint32_t GetShapeRevision() const { return shapeRevision_; }

    private:
        /// @brief 親から自分までをたどった並び（根が先頭）
        /// @return 並べた数（循環を見つけたら、そこから先は根として扱う）
        int CollectChain(const RectTransformComponent* (&chain)[kMaxDepth]) const;

        /// @brief 親を指しているかを数え直す
        void UpdateLinked();

        UILayout layout_;
        ObjectRef<RectTransformComponent> parent_;
        uint32_t shapeRevision_ = 0;
        int resolvedOrder_ = 0;
        bool hasResolvedOrder_ = false;
        bool sizeDriven_ = false;
        bool linked_ = false;
    };
}
