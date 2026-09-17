#pragma once

#include "GameObject/Component/Core/IComponent.h"
#include "GameObject/Component/Render/IRenderableComponent.h"
#include "Graphics/Render/UI/TextRenderer.h"
#include "Math/Vector/Vector2.h"
#include "Math/Vector/Vector4.h"
#include "Reflection/Reflect.h"

#include <cstdint>
#include <string>
#include <vector>

namespace CoreEngine
{
    class MsdfFont;
    class RectTransformComponent;

    /// @brief MSDF フォントで文字列を UI パスへ描くコンポーネント
    /// @details 位置・基準点・枠の大きさ・回転は兄弟の `RectTransformComponent` から取る（無ければ Awake で足す）。
    ///          頂点は em 単位（フォントサイズ 1 のときの大きさ）で組み、フォントサイズ・位置・回転は描画時に掛ける。
    ///          描画ではレンダラーのバッチへ頂点を積むだけで、ドローコールはレンダラーがまとめて出す。
    ///          色・縁取り・太さは頂点へ焼き込むので、変えても頂点は組み直さない。
    class UITextComponent : public IComponent, public IRenderableComponent
    {
    public:
        /// @brief 横揃えの名前（`TextAlignH` の並び）
        static constexpr const char* kAlignHNames[] = { "左", "中央", "右" };

        /// @brief 縦揃えの名前（`TextAlignV` の並び）
        static constexpr const char* kAlignVNames[] = { "上", "中央", "下" };

        UITextComponent();
        ~UITextComponent() override;

        const char* GetTypeName() const override { return "UIText"; }

        REFLECT_DECLARE(UITextComponent)

        /// @brief UI トランスフォームを使う
        bool RequiresComponent(const IComponent& other) const override;

        /// @brief UI トランスフォームを確保し、レンダラーを引く（名前で指したフォントは最初に描くときに引く）
        void Awake() override;

        // ===== IRenderableComponent =====

        RenderPassType GetRenderPassType() const override { return RenderPassType::UIText; }
        BlendMode GetBlendMode() const override { return BlendMode::kBlendModeNormal; }
        void SetBlendMode(BlendMode) override {}

        /// @brief 必要なら頂点を組み直し、レンダラーのバッチへ積む
        void Render(const DrawViewInfo& view) override;

        // ===== 文字列 =====

        /// @brief 表示する文字列（UTF-8。\n で改行）
        const std::string& GetText() const { return text_; }
        void SetText(const std::string& textUtf8);

        // ===== フォント =====

        /// @brief フォントの名前
        const std::string& GetFontName() const { return fontName_; }

        /// @brief フォントを名前で指す
        /// @param fontName FontManager の登録名・フォントフォルダのファイル名・インストール済みのフォント名（空なら既定）
        void SetFontByName(const std::string& fontName);

        /// @brief 使っているフォント（まだ引いていなければ nullptr）
        MsdfFont* GetFont() const { return font_; }

        /// @brief フォントサイズ（px）
        /// @note 折り返しも枠の固定もしていなければ、頂点は組み直さない
        float GetFontSize() const { return fontSize_; }
        void SetFontSize(float pixelSize);

        /// @brief 行間の倍率（1 でフォント本来の行送り）
        float GetLineSpacing() const { return lineSpacing_; }
        void SetLineSpacing(float scale);

        // ===== 枠と揃え =====

        /// @brief 枠（UI トランスフォームの大きさ）を文字列に合わせるか
        /// @details 合わせた後で UI トランスフォームの大きさを別の値にされたら false になり、
        ///          以後はその枠の中で折り返し・揃えをする。
        bool IsFieldAutoFit() const { return fieldAutoFit_; }
        void SetFieldAutoFit(bool enable);

        /// @brief 折り返し幅（px。0 で折り返さない）
        /// @note 枠を文字列に合わせていない間は使わず、枠の幅で折り返す
        float GetWrapWidth() const { return wrapWidth_; }
        void SetWrapWidth(float pixelWidth);

        /// @brief 枠の中での揃え
        TextAlignH GetAlignH() const { return alignH_; }
        void SetAlignH(TextAlignH alignment);
        TextAlignV GetAlignV() const { return alignV_; }
        void SetAlignV(TextAlignV alignment);
        void SetAlign(TextAlignH horizontal, TextAlignV vertical);

        // ===== 見た目 =====

        Vector4 GetColor() const { return style_.color; }
        void SetColor(const Vector4& color) { style_.color = color; }

        /// @brief 縁取りの色（不透明度 0 で縁取りなし）
        Vector4 GetOutlineColor() const { return style_.outlineColor; }
        void SetOutlineColor(const Vector4& color) { style_.outlineColor = color; }

        /// @brief 縁取りの太さ（em 単位）
        /// @note フォントがあれば `GetMaxOutlineWidth()` で頭打ちにする
        float GetOutlineWidth() const { return style_.outlineWidthEm; }
        void SetOutlineWidth(float widthEm);

        /// @brief 縁取りの色と太さをまとめて設定する
        void SetOutline(const Vector4& color, float widthEm);

        /// @brief 文字の太さの調整（em 単位。正で太く）
        float GetWeight() const { return style_.weightEm; }
        void SetWeight(float weightEm) { style_.weightEm = weightEm; }

        /// @brief 縁取りとして表せる最大の太さ（em 単位。フォントが無ければ 0）
        /// @note 距離場が持つ幅で決まる。太くしたいときはフォントを焼くときの pxRange を上げる
        float GetMaxOutlineWidth() const;

        // ===== 組んだ結果 =====

        /// @brief 最後に組んだ文字列を囲む大きさ（px）
        Vector2 GetMeasuredSize() const;

        /// @brief 描くグリフの数
        uint32_t GetGlyphCount() const { return static_cast<uint32_t>(glyphVertices_.size() / 4); }

        /// @brief 折り返した後の行数
        uint32_t GetLineCount() const { return lineCount_; }

        /// @brief 兄弟の UI トランスフォーム（配置の出どころ）
        RectTransformComponent* GetRectTransform() const;

    private:
        /// @brief 名前で指したフォントを引く
        void ResolveFont();

        /// @brief 枠・基準点・グリフの変化を見て、要るなら頂点を組み直すか枠を合わせ直す
        void UpdateGeometry(RectTransformComponent& rect);

        /// @brief 文字列からグリフの矩形の並び（em 単位）を組む
        void RebuildGeometry(RectTransformComponent& rect);

        /// @brief 枠を文字列の大きさにする
        void FitRect(RectTransformComponent& rect);

        TextRenderer* renderer_ = nullptr;
        mutable RectTransformComponent* rect_ = nullptr;

        MsdfFont* font_ = nullptr;
        std::string fontName_;
        /// 名前でフォントを引けなかったか（名前を変えるまで引き直さない）
        bool fontResolveFailed_ = false;

        std::string text_;
        float fontSize_ = 32.0f;
        float lineSpacing_ = 1.0f;
        float wrapWidth_ = 0.0f;

        bool fieldAutoFit_ = true;
        TextAlignH alignH_ = TextAlignH::Left;
        TextAlignV alignV_ = TextAlignV::Top;

        /// 色・縁取り・太さ（頂点へ焼き込む）
        TextDrawStyle style_;

        /// CPU 側の頂点（em 単位）
        std::vector<TextGlyphVertex> glyphVertices_;
        /// 文字列を囲む矩形（em 単位）
        Vector2 measuredSizeEm_ = { 0.0f, 0.0f };
        uint32_t lineCount_ = 0;

        bool geometryDirty_ = true;
        /// 頂点はそのままで、枠だけ合わせ直すか
        bool fitDirty_ = false;

        /// 最後に組んだときの UI トランスフォームの形の番号
        uint32_t builtShapeRevision_ = 0;
        /// 最後に組んだときのフォントのグリフ世代
        uint32_t builtGlyphGeneration_ = 0;

        /// 最後に文字列へ合わせた枠の大きさ（`hasFitted_` のときだけ意味を持つ）
        Vector2 fittedSize_ = { 0.0f, 0.0f };
        bool hasFitted_ = false;

        /// グリフ数の上限の警告を 1 回だけ出す
        bool glyphLimitWarned_ = false;
    };
}
