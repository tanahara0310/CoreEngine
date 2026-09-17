#pragma once

#include "GameObject/Component/Core/IComponent.h"
#include "GameObject/Component/Render/IRenderableComponent.h"
#include "Graphics/Render/Text3D/Text3DRenderer.h"
#include "Math/Vector/Vector2.h"
#include "Math/Vector/Vector4.h"
#include "Text/TextGeometryBuilder.h"

#include <cstdint>
#include <string>
#include <vector>

namespace CoreEngine
{
    class MsdfFont;
    class TransformComponent;

    /// @brief 3D テキストのビルボード方式
    enum class Text3DBillboard : uint8_t
    {
        /// @brief ビルボードなし（既定）
        /// @details トランスフォームの回転がそのまま効く。看板・床の文字向け
        None,

        /// @brief カメラの向きに完全に合わせる
        /// @details ネームプレート・ダメージ数値のように常に正面から読ませたいもの向け
        ViewFacing,

        /// @brief Y 軸まわりだけカメラへ向ける
        /// @details 文字が立ったまま追従する。見下ろし視点で ViewFacing だと
        ///          文字が寝てしまう場合はこちら
        YAxisOnly,
    };

    /// @brief MSDF フォントで文字列をワールド空間へ描くコンポーネント
    /// @details
    ///  組版（折り返し・禁則・整列）は UITextComponent と同じ `TextGeometry::Build` を通す。
    ///  出力が em 単位なので、UI は「1em = fontSize px」、こちらは
    ///  「1em = fontSize ワールド単位」と読み替えるだけで同じ組版が両方で使える。
    ///
    ///  位置・回転・スケールは兄弟の `TransformComponent` を使う（無ければ Awake で足す）。
    ///  文字固有の大きさは SetFontSize() で別に指定する。
    ///
    ///  **描画はここでは行わない。** Render() は Text3DRenderer へ頂点を積むだけで、
    ///  実際のドローコールはレンダラー側でまとめて発行される。
    ///  色・縁取り・変換はテキストごとの定数バッファではなく
    ///  頂点へ焼き込まれる（Text3DRenderer::Submit を参照）。
    class Text3DRendererComponent : public IComponent, public IRenderableComponent
    {
    public:
        Text3DRendererComponent() = default;
        ~Text3DRendererComponent() override = default;

        const char* GetTypeName() const override { return "Text3DRenderer"; }

        /// @brief トランスフォームを使う
        bool RequiresComponent(const IComponent& other) const override;

#ifdef CORE_EDITOR
        /// @brief 文字列・フォント・見た目・配置・描画順の編集 UI
        /// @return 値が変更されたら true
        bool DrawEditorUI();
#endif

        // ===== ライフサイクル =====

        /// @brief トランスフォームを確保し、描画先のレンダラーを取得する
        void Awake() override;

        // ===== IRenderableComponent =====

        RenderPassType GetRenderPassType() const override { return RenderPassType::Text3D; }
        BlendMode GetBlendMode() const override { return BlendMode::kBlendModeNormal; }

        /// @brief 頂点をレンダラーのバッチへ積む
        void Render(const DrawViewInfo& view) override;

        // ===== フォント =====

        /// @brief 使用フォントを実体で指定する
        /// @param font FontManager から取得したフォント
        /// @param fontName FontManager で引ける名前。空ならシーン JSON にフォントを書かない
        void SetFont(MsdfFont* font, const std::string& fontName = {});

        /// @brief 使用フォントを名前で差し替える（FontManager で引ける名前）
        void SetFontByName(const std::string& fontName);

        MsdfFont* GetFont() const { return font_; }
        const std::string& GetFontName() const { return fontName_; }

        // ===== テキスト =====

        /// @brief 表示文字列を差し替える（頂点を組み直す）
        void SetText(const std::string& textUtf8);
        const std::string& GetText() const { return textUtf8_; }

        /// @brief 1em の大きさを **ワールド単位** で設定する
        /// @details UI 版の「フォントサイズ px」に相当する。1.0 なら 1em が 1 ワールド単位。
        /// @note 頂点は em 単位なので通常は再構築が起きない。
        ///       折り返しが有効なときだけ、折り位置が変わるので組み直す
        void SetFontSize(float worldUnitsPerEm);
        float GetFontSize() const { return fontSize_; }

        /// @brief 折り返し幅（ワールド単位）。0 で折り返し無効
        void SetWrapWidth(float worldUnits);
        float GetWrapWidth() const { return wrapWidth_; }

        // ===== テキストフィールド =====
        // 文字を流し込む枠。ワールド単位で持つ

        /// @brief フィールドの大きさ（ワールド単位）を明示指定する
        /// @details 指定すると自動調整は切れる。
        ///          幅は折り返し幅として、高さは縦揃えの基準として使われる
        void SetFieldSize(const Vector2& sizeWorld);
        Vector2 GetFieldSize() const { return fieldSize_; }

        /// @brief フィールドを文字列の大きさへ自動で合わせるか
        void SetFieldAutoFit(bool enable);
        bool IsFieldAutoFit() const { return fieldAutoFit_; }

        /// @brief フィールド内での文字の揃え
        void SetAlign(TextAlignH horizontal, TextAlignV vertical);
        TextAlignH GetAlignH() const { return alignH_; }
        TextAlignV GetAlignV() const { return alignV_; }

        /// @brief 行間の倍率（1.0 でフォント本来の行送り）
        void SetLineSpacing(float scale);
        float GetLineSpacing() const { return lineSpacing_; }

        /// @brief 文字列全体の基準点（0,0 = 左上 / 0.5,0.5 = 中央）
        /// @note 既定は中央
        void SetPivot(const Vector2& pivot);
        Vector2 GetPivot() const { return pivot_; }

        /// @brief 文字列を囲む矩形のサイズ（ワールド単位）
        Vector2 GetMeasuredSize() const
        {
            return { measuredSizeEm_.x * fontSize_, measuredSizeEm_.y * fontSize_ };
        }

        /// @brief 描画するグリフ数
        uint32_t GetGlyphCount() const { return static_cast<uint32_t>(glyphVertices_.size() / 4); }

        /// @brief 折り返し後の行数
        uint32_t GetLineCount() const { return lineCount_; }

        // ===== 3D 固有 =====

        /// @brief ビルボード方式
        void SetBillboard(Text3DBillboard mode) { billboard_ = mode; }
        Text3DBillboard GetBillboard() const { return billboard_; }

        /// @brief 深度の扱い（壁の裏で隠れるか、壁越しに見えるか）
        void SetDepthMode(Text3DDepthMode mode) { depthMode_ = mode; }
        Text3DDepthMode GetDepthMode() const { return depthMode_; }

        // ===== 見た目 =====
        // 色・縁取りは頂点へ焼き込まれるので、変えても再構築は起きない
        void SetColor(const Vector4& color) { style_.color = color; }
        Vector4 GetColor() const { return style_.color; }

        /// @brief 縁取りを設定する
        /// @param color 縁取り色（a = 0 で無効）
        /// @param widthEm 太さ（em 単位＝フォントサイズに対する割合）
        /// @note 距離場が持つ情報量で上限が決まる（GetMaxOutlineWidth）
        void SetOutline(const Vector4& color, float widthEm);
        Vector4 GetOutlineColor() const { return style_.outlineColor; }
        float GetOutlineWidth() const { return style_.outlineWidthEm; }

        /// @brief 文字の太さ調整（em 単位。正で太く）
        void SetWeight(float weightEm) { style_.weightEm = weightEm; }
        float GetWeight() const { return style_.weightEm; }

        /// @brief 縁取りとして表現できる最大の太さ（em 単位）
        float GetMaxOutlineWidth() const;

        /// @brief 兄弟の TransformComponent（位置・回転・スケールの出どころ）
        TransformComponent* GetTransformComponent() const { return ResolveTransform(); }

        // ===== シリアライズ =====

        /// @brief フォント名・文字列・組版・見た目・ビルボード・深度を書き出す
        json OnSerialize() const override;

        /// @brief OnSerialize が書いた値を読む（フォントを先に解決する）
        void OnDeserialize(const json& j) override;

    private:
        /// @brief transform_ が未取得なら兄弟から取る
        TransformComponent* ResolveTransform() const;

        /// @brief RenderManager から Text3D パスのレンダラーを取る
        void ResolveRenderer();

        /// @brief フォントが未指定なら既定フォントを取る（1 回だけ試す）
        void EnsureFont();

        /// @brief 文字列からグリフのクワッド列（CPU 側・em 単位・Y 上正）を組み立てる
        void RebuildGeometry();

        /// @brief em → ワールドの変換行列を作る
        /// @param viewMatrix ビルボードの姿勢を取るビュー行列（ビルボード無効なら参照しない）
        Matrix4x4 BuildWorldMatrix(const Matrix4x4& viewMatrix) const;

        /// @brief 頂点をレンダラーのバッチへ積む
        void SubmitToRenderer(const Matrix4x4& viewMatrix, const Matrix4x4& viewProjection);

        Text3DRenderer* renderer_ = nullptr;
        mutable TransformComponent* transform_ = nullptr;
        MsdfFont* font_ = nullptr;
        /// 使用フォントの登録名。シーン JSON にはこれだけを書く
        std::string fontName_;
        /// 既定フォントの取得を試したか
        bool defaultFontRequested_ = false;

        std::string textUtf8_;
        /// 1em の大きさ（ワールド単位）
        float fontSize_ = 1.0f;
        float lineSpacing_ = 1.0f;
        float wrapWidth_ = 0.0f;

        bool fieldAutoFit_ = true;
        Vector2 fieldSize_ = { 0.0f, 0.0f };
        TextAlignH alignH_ = TextAlignH::Center;
        TextAlignV alignV_ = TextAlignV::Middle;
        Vector2 pivot_ = { 0.5f, 0.5f };

        Text3DBillboard billboard_ = Text3DBillboard::None;
        Text3DDepthMode depthMode_ = Text3DDepthMode::Test;

        /// 文字列を囲む矩形（em 単位）
        Vector2 measuredSizeEm_ = { 0.0f, 0.0f };
        uint32_t lineCount_ = 0;

        /// CPU 側の頂点（em 単位・Y 上正）。描画時にレンダラーのバッチへ積む
        std::vector<TextGlyphVertex> glyphVertices_;

        /// 色・縁取り・太さ。頂点へ焼き込まれる
        Text3DDrawStyle style_;

        bool geometryDirty_ = true;
        /// グリフ数上限の警告を 1 回だけ出すためのフラグ
        bool glyphLimitWarned_ = false;

        /// @brief 最後に組んだときのフォント側のグリフ世代
        /// @details 実行時ベイクでグリフ表が更新されると進む。
        ///          変化を検出したら頂点を組み直し、□ が本来の字へ差し替わる
        uint32_t lastGlyphGeneration_ = 0;
    };
}
