#pragma once

#include "Engine/ObjectCommon/GameObject.h"
#include "Engine/WorldTransfom/WorldTransform.h"
#include <string>
#include <d3d12.h>
#include <wrl.h>
#include <vector>

// 前方宣言
namespace CoreEngine {
	class Font;
	class TextRenderer;
	struct Glyph;
}

/// @brief テキスト描画オブジェクト - GameObject基底クラスを継承してRenderManager対応

namespace CoreEngine
{
	class TextObject : public GameObject {
	public:
		TextObject() = default;
		~TextObject() override = default;

		/// @brief 初期化
		/// @param fontFilePath フォントファイルパス
		/// @param fontSize フォントサイズ（ピクセル）
		/// @param name オブジェクト名（ImGui表示用、省略可）
		void Initialize(const std::string& fontFilePath, uint32_t fontSize, const std::string& name = "");

		/// @brief 更新
		void Update() override;

		/// @brief 描画処理
		/// @param camera カメラ
		void Draw(const ICamera* camera) override;

#ifdef _DEBUG
		/// @brief ImGuiデバッグUI描画
		/// @return 変更があった場合 true
		bool DrawImGui() override;

		/// @brief オブジェクト名を取得
		/// @return オブジェクト名
		const char* GetObjectName() const override { return "Text"; }
#endif

		/// @brief このオブジェクトの描画パスタイプを取得
		/// @return 描画パスタイプ（Text）
		RenderPassType GetRenderPassType() const override { return RenderPassType::Text; }

    /// @brief ブレンドモードを取得
    BlendMode GetBlendMode() const override { return blendMode_; }

    /// @brief ブレンドモードを設定
    void SetBlendMode(BlendMode blendMode) override { blendMode_ = blendMode; }

		/// @brief 表示テキストを設定
		void SetText(const std::string& text);
		std::string GetText() const { return text_; }

    /// @brief 色を設定
    void SetColor(const Vector4& color) { color_ = color; }
    Vector4 GetColor() const { return color_; }

    /// @brief スケールを設定
    void SetScale(float scale) { scale_ = scale; }
    float GetScale() const { return scale_; }

    /// @brief トランスフォームを取得（位置設定用）
    EulerTransform& GetTransform() { return transform_; }
    const EulerTransform& GetTransform() const { return transform_; }

	private:
		/// @brief テキストメッシュを構築
		void BuildTextMesh();

		/// @brief UTF-8からUTF-32へ変換
		uint32_t GetUTF32CharCode(size_t& index);

    /// @brief グリフ描画情報
    struct GlyphDrawInfo {
        const Glyph* glyph;
        float xPos;
        float yPos;
        float width;
        float height;
    };

private:
    Font* font_ = nullptr;
    std::string text_;
    Vector4 color_ = { 1.0f, 1.0f, 1.0f, 1.0f };
    float scale_ = 1.0f;
    BlendMode blendMode_ = BlendMode::kBlendModeNormal;

    EulerTransform transform_;

		// 頂点バッファ（動的にリサイズ）
		Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
		D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
		Microsoft::WRL::ComPtr<ID3D12Resource> indexResource_;
		D3D12_INDEX_BUFFER_VIEW indexBufferView_{};

		// グリフ描画情報のキャッシュ
		std::vector<GlyphDrawInfo> glyphDrawInfos_;

		// テキストが変更されたかどうか
		bool isDirty_ = true;

		// 現在のバッファサイズ
		size_t currentVertexCapacity_ = 0;
		size_t currentIndexCapacity_ = 0;
	};
}