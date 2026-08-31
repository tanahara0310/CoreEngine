#pragma once

#include "UIElement.h"
#include "GameObject/GameObject.h"
#include "Graphics/Render/UI/TextRenderer.h"
// MsdfGlyph を値で扱うので実体が要る（POD だけの軽いヘッダ）
#include "Text/MsdfFontTypes.h"
#include "Math/Vector/Vector2.h"
#include "Math/Vector/Vector4.h"

#include <string>
#include <vector>

namespace CoreEngine
{
    class MsdfFont;

    /// @brief MSDF フォントで文字列を描く UI 要素
    /// @details
    ///  頂点は **em 単位**（フォントサイズ 1.0 のときの大きさ）で組み、
    ///  フォントサイズ・位置・回転は描画時にまとめて掛ける。
    ///  そのため SetFontSize() は頂点を組み直さない
    ///  （折り返しが有効なときだけ、折り位置が変わるので組み直す）。
    ///  「スケールを変えても輪郭が崩れない」は距離場が担保し、
    ///  「スケール変更が軽い」はこの構成が担保する。
    ///
    ///  **描画はここでは行わない。** Draw() は TextRenderer へ頂点を積むだけで、
    ///  実際のドローコールはレンダラー側で 1 本にまとめて発行される。
    ///  そのため色・縁取り・変換はテキストごとの定数バッファではなく
    ///  頂点へ焼き込まれる（TextRenderer::Submit を参照）。
    class UIText : public GameObject
    {
    public:
        UIText() = default;
        ~UIText() override = default;

        /// @brief 初期化
        /// @param font 生成済みの MSDF フォント（寿命は呼び出し側が持つ）
        /// @param textUtf8 表示文字列（UTF-8。改行 \n に対応）
        /// @param name デバッグ名（任意）
        void Initialize(MsdfFont* font, const std::string& textUtf8, const std::string& name = "");

        // ===== GameObject インターフェース =====
        RenderPassType GetRenderPassType() const override { return RenderPassType::UIText; }
        BlendMode GetBlendMode() const override { return BlendMode::kBlendModeNormal; }
        const char* GetObjectName() const override { return "UIText"; }
        void Draw(const Camera* camera) override;
        void Draw(const DrawViewInfo& view) override;

        Vector3 GetWorldPosition() const override
        {
            return { layout_.anchoredPos.x, layout_.anchoredPos.y, 0.0f };
        }

        // ===== テキスト =====

        /// @brief 表示文字列を差し替える（頂点を組み直す）
        /// @note 組み直すのは CPU 側の配列だけなので、毎フレーム呼んでも GPU 側は壊れない
        void SetText(const std::string& textUtf8);
        const std::string& GetText() const { return textUtf8_; }

        /// @brief フォントサイズ（px）を設定する
        /// @note 頂点は em 単位なので通常は再構築が起きない。
        ///       折り返しが有効なときだけ、折り位置が変わるので組み直す
        void SetFontSize(float pixelSize);
        float GetFontSize() const { return fontSize_; }

        /// @brief 折り返し幅（px）。0 で折り返し無効
        /// @details
        ///  和文は単語の切れ目が無いのでどこでも折れるが、
        ///  句読点や閉じ括弧が行頭に落ちないよう禁則処理をかける。
        ///  欧文は空白でのみ折る（単語の途中で切らない）。
        void SetWrapWidth(float pixelWidth);
        float GetWrapWidth() const { return wrapWidthPx_; }

        /// @brief 行間の倍率（1.0 でフォント本来の行送り）
        void SetLineSpacing(float scale);
        float GetLineSpacing() const { return lineSpacing_; }

        /// @brief 文字列を囲む矩形のサイズ（px）
        Vector2 GetMeasuredSize() const
        {
            return { measuredSizeEm_.x * fontSize_, measuredSizeEm_.y * fontSize_ };
        }

        /// @brief 描画するグリフ数
        uint32_t GetGlyphCount() const { return static_cast<uint32_t>(glyphVertices_.size() / 4); }

        /// @brief 折り返し後の行数
        uint32_t GetLineCount() const { return lineCount_; }

        // ===== UILayout アクセサ =====
        void SetAnchor(UIAnchor anchor) { layout_.anchor = anchor; }
        UIAnchor GetAnchor() const { return layout_.anchor; }

        void SetAnchoredPosition(const Vector2& pos) { layout_.anchoredPos = pos; }
        Vector2 GetAnchoredPosition() const { return layout_.anchoredPos; }

        /// @brief 文字列全体の基準点（0,0 = 左上 / 0.5,0.5 = 中央）
        void SetPivot(const Vector2& pivot);
        Vector2 GetPivot() const { return layout_.pivot; }

        void SetUIRotation(float radians) { layout_.rotation = radians; }
        float GetUIRotation() const { return layout_.rotation; }

        void SetSortOrder(int order) { layout_.sortOrder = order; SetRenderOrder(order); }
        int  GetSortOrder() const { return layout_.sortOrder; }

        const UILayout& GetLayout() const { return layout_; }

        // ===== 見た目 =====
        // 色・縁取りは頂点へ焼き込まれるので、変えても再構築は起きない
        void SetColor(const Vector4& color) { style_.color = color; }
        Vector4 GetColor() const { return style_.color; }

        /// @brief 縁取りを設定する
        /// @param color 縁取り色（a = 0 で無効）
        /// @param widthEm 太さ（em 単位＝フォントサイズに対する割合）
        /// @note 距離場が持つ情報量で上限が決まる（GetMaxOutlineWidth）。
        ///       それ以上太くしたい場合はベイク時の pxRange を上げること。
        void SetOutline(const Vector4& color, float widthEm);
        Vector4 GetOutlineColor() const { return style_.outlineColor; }
        float GetOutlineWidth() const { return style_.outlineWidthEm; }

        /// @brief 文字の太さ調整（em 単位。正で太く）
        void SetWeight(float weightEm) { style_.weightEm = weightEm; }
        float GetWeight() const { return style_.weightEm; }

        /// @brief 縁取りとして表現できる最大の太さ（em 単位）
        float GetMaxOutlineWidth() const;

#ifdef USE_IMGUI
        int  GetInspectorTabs(InspectorTabDef* outTabs, int maxTabs) const override;
        bool DrawInspectorTabContent(int tabIndex) override;
#endif

    private:
        /// @brief 行の範囲（コードポイント列への添字）と幅（em）
        struct LineRange
        {
            size_t begin = 0;
            size_t end = 0;   ///< 半開区間
            float  width = 0.0f;
        };

        /// @brief 文字列からグリフのクワッド列（CPU 側・em 単位）を組み立てる
        void RebuildGeometry();

        /// @brief 折り返し位置を決めて行に分ける
        /// @param codePoints 文字列
        /// @param glyphs 各文字のグリフ情報（codePoints と同じ長さ）
        /// @param wrapWidthEm 折り返し幅（em）。0 なら改行文字だけで分ける
        static void BuildLines(const std::vector<char32_t>& codePoints,
            const std::vector<MsdfGlyph>& glyphs,
            float wrapWidthEm,
            std::vector<LineRange>& outLines);

        TextRenderer* renderer_ = nullptr;
        MsdfFont* font_ = nullptr;

        std::string textUtf8_;
        float fontSize_ = 32.0f;
        float lineSpacing_ = 1.0f;
        float wrapWidthPx_ = 0.0f;

        UILayout layout_;
        /// 文字列を囲む矩形（em 単位）。GetMeasuredSize / pivot の計算に使う
        Vector2 measuredSizeEm_ = { 0.0f, 0.0f };
        uint32_t lineCount_ = 0;

        /// CPU 側の頂点（em 単位）。描画時にレンダラーのバッチへ積む
        std::vector<TextGlyphVertex> glyphVertices_;

        /// 色・縁取り・太さ。頂点へ焼き込まれる
        TextDrawStyle style_;

        bool geometryDirty_ = false;
        /// グリフ数上限の警告を 1 回だけ出すためのフラグ
        bool glyphLimitWarned_ = false;

        /// @brief 最後に組んだときのフォント側のグリフ世代
        /// @details 実行時ベイクでグリフ表が更新されると進む。
        ///          変化を検出したら頂点を組み直し、□ が本来の字へ差し替わる
        uint32_t lastGlyphGeneration_ = 0;
    };
}
