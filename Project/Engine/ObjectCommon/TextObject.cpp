#include "TextObject.h"
#include "Engine/Graphics/Font/Font.h"
#include "Engine/Graphics/Font/Glyph.h"
#include "Engine/Graphics/Font/FontManager.h"
#include "Engine/Graphics/Font/TextRenderer.h"
#include "Engine/Graphics/Render/RenderManager.h"
#include "Engine/EngineSystem/EngineSystem.h"
#include "Engine/Graphics/Common/DirectXCommon.h"
#include "Engine/Graphics/Resource/ResourceFactory.h"
#include "Engine/Graphics/Structs/VertexData.h"
#include "Engine/Graphics/Structs/SpriteMaterial.h"
#include "Engine/Utility/Logger/Logger.h"

#ifdef _DEBUG
#include <imgui.h>
#endif


namespace CoreEngine
{
	void TextObject::Initialize(const std::string& fontFilePath, uint32_t fontSize, const std::string& name) {
		auto* fontManager = &FontManager::GetInstance();
		font_ = fontManager->LoadFont(fontFilePath, fontSize);

		if (!font_) {
			Logger::GetInstance().Log("Failed to load font: " + fontFilePath);
		}

		name_ = name.empty() ? "TextObject" : name;
		text_ = "Sample Text";
		transform_.translate = { 100.0f, 100.0f, 0.0f };
		isDirty_ = true;
	}

	void TextObject::SetText(const std::string& text) {
		if (text_ != text) {
			text_ = text;
			isDirty_ = true;
		}
	}

	void TextObject::Update() {
		// テキストが変更された場合のみメッシュを再構築
		if (isDirty_) {
			BuildTextMesh();
			isDirty_ = false;
		}
	}

	void TextObject::BuildTextMesh() {
		if (!font_ || text_.empty()) {
			glyphDrawInfos_.clear();
			return;
		}

		glyphDrawInfos_.clear();
		glyphDrawInfos_.reserve(text_.size());

		float cursorX = 0.0f;
		float cursorY = 0.0f;

		// グリフ情報を収集
		size_t index = 0;
		while (index < text_.size()) {
			uint32_t charCode = GetUTF32CharCode(index);

			if (charCode == '\n') {
				cursorX = 0.0f;
				cursorY += font_->GetLineHeight() * scale_;
				continue;
			}

			const Glyph* glyph = font_->GetGlyph(charCode);
			if (!glyph || !glyph->texture) {
				cursorX += (glyph ? glyph->advance >> 6 : 0) * scale_;
				continue;
			}

			GlyphDrawInfo info;
			info.glyph = glyph;

			// ベースラインを基準とした配置
			// ベースライン = transform_.translate.y + cursorY（この位置を基準線とする）
			// bearingY = ベースラインから文字の上端までの距離（上向きが正）
			// 
			// DirectXの画面座標系（Y軸下向き）での計算：
			// 文字の上端Y座標 = ベースライン - bearingY
			// （ベースラインより上に行くので、bearingYを引く）

			float baselineY = transform_.translate.y + cursorY;
			info.xPos = transform_.translate.x + cursorX + glyph->bearingX * scale_;
			info.yPos = baselineY - glyph->bearingY * scale_;
			info.width = glyph->width * scale_;
			info.height = glyph->height * scale_;

			glyphDrawInfos_.push_back(info);

			cursorX += (glyph->advance >> 6) * scale_;
		}

	// 必要なバッファサイズを計算
	size_t requiredVertexCount = glyphDrawInfos_.size() * 4;
	size_t requiredIndexCount = glyphDrawInfos_.size() * 6;

	auto* engineSystem = GetEngineSystem();
	if (!engineSystem) return;

	auto* dxCommon = engineSystem->GetComponent<DirectXCommon>();
	auto* resourceFactory = engineSystem->GetComponent<ResourceFactory>();

		if (!dxCommon || !resourceFactory) return;

		// バッファが足りない場合は再作成
		if (currentVertexCapacity_ < requiredVertexCount) {
			currentVertexCapacity_ = requiredVertexCount * 2; // 余裕を持たせる

			vertexResource_ = resourceFactory->CreateBufferResource(
				dxCommon->GetDevice(),
				sizeof(VertexData) * currentVertexCapacity_);

			vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
			vertexBufferView_.SizeInBytes = static_cast<UINT>(sizeof(VertexData) * currentVertexCapacity_);
			vertexBufferView_.StrideInBytes = sizeof(VertexData);
		}

		if (currentIndexCapacity_ < requiredIndexCount) {
			currentIndexCapacity_ = requiredIndexCount * 2; // 余裕を持たせる

			indexResource_ = resourceFactory->CreateBufferResource(
				dxCommon->GetDevice(),
				sizeof(uint32_t) * currentIndexCapacity_);

			indexBufferView_.BufferLocation = indexResource_->GetGPUVirtualAddress();
			indexBufferView_.SizeInBytes = static_cast<UINT>(sizeof(uint32_t) * currentIndexCapacity_);
			indexBufferView_.Format = DXGI_FORMAT_R32_UINT;
		}

		// 頂点データを書き込み
		if (!glyphDrawInfos_.empty()) {
			VertexData* vertexData = nullptr;
			vertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));

			for (size_t i = 0; i < glyphDrawInfos_.size(); ++i) {
				const auto& info = glyphDrawInfos_[i];
				const auto* glyph = info.glyph;
				size_t vIdx = i * 4;

				// グリフのテクスチャサイズを取得
				float texWidth = static_cast<float>(glyph->width);
				float texHeight = static_cast<float>(glyph->height);

				// UV座標を計算（実際のグリフ領域のみを使用）
				float uMin = 0.0f;
				float vMin = 0.0f;
				float uMax = 1.0f;
				float vMax = 1.0f;

				// グリフが空でない場合のみ描画
				if (texWidth > 0 && texHeight > 0) {
					// 左下
					vertexData[vIdx + 0].position = { info.xPos, info.yPos + info.height, 0.0f, 1.0f };
					vertexData[vIdx + 0].texcoord = { uMin, vMin };
					vertexData[vIdx + 0].normal = { 0.0f, 0.0f, -1.0f };

					// 左上
					vertexData[vIdx + 1].position = { info.xPos, info.yPos, 0.0f, 1.0f };
					vertexData[vIdx + 1].texcoord = { uMin, vMax };
					vertexData[vIdx + 1].normal = { 0.0f, 0.0f, -1.0f };

					// 右下
					vertexData[vIdx + 2].position = { info.xPos + info.width, info.yPos + info.height, 0.0f, 1.0f };
					vertexData[vIdx + 2].texcoord = { uMax, vMin };
					vertexData[vIdx + 2].normal = { 0.0f, 0.0f, -1.0f };

					// 右上
					vertexData[vIdx + 3].position = { info.xPos + info.width, info.yPos, 0.0f, 1.0f };
					vertexData[vIdx + 3].texcoord = { uMax, vMax };
					vertexData[vIdx + 3].normal = { 0.0f, 0.0f, -1.0f };
				}
			}

			vertexResource_->Unmap(0, nullptr);

			// インデックスデータを書き込み
			uint32_t* indexData = nullptr;
			indexResource_->Map(0, nullptr, reinterpret_cast<void**>(&indexData));

			for (size_t i = 0; i < glyphDrawInfos_.size(); ++i) {
				uint32_t baseVertex = static_cast<uint32_t>(i * 4);
				size_t iIdx = i * 6;

				indexData[iIdx + 0] = baseVertex + 0;
				indexData[iIdx + 1] = baseVertex + 1;
				indexData[iIdx + 2] = baseVertex + 2;
				indexData[iIdx + 3] = baseVertex + 1;
				indexData[iIdx + 4] = baseVertex + 3;
				indexData[iIdx + 5] = baseVertex + 2;
			}

			indexResource_->Unmap(0, nullptr);
		}
	}

	void TextObject::Draw(const ICamera* camera) {
		if (!font_ || text_.empty() || glyphDrawInfos_.empty()) {
			return;
		}

		auto* engineSystem = GetEngineSystem();
		if (!engineSystem) return;

		auto* renderManager = engineSystem->GetComponent<RenderManager>();
		auto* textRenderer = static_cast<TextRenderer*>(renderManager->GetRenderer(RenderPassType::Text));

		if (!textRenderer) return;

		auto* dxCommon = engineSystem->GetComponent<DirectXCommon>();
		auto* cmdList = dxCommon->GetCommandList();

		// 共通の定数バッファを設定
		size_t bufferIndex = textRenderer->GetAvailableConstantBuffer();

		auto& materialData = textRenderer->GetMaterialDataPool()[bufferIndex];
		materialData->color = color_;
		materialData->uvTransform = MathCore::Matrix::Identity();

		Vector3 scale = { 1.0f, 1.0f, 1.0f };
		Vector3 rotation = { 0.0f, 0.0f, 0.0f };
		Vector3 pos = { 0.0f, 0.0f, 0.0f };

		auto& transformData = textRenderer->GetTransformDataPool()[bufferIndex];
		Matrix4x4 worldMatrix = MathCore::Matrix::MakeAffine(scale, rotation, pos);
		transformData->WVP = textRenderer->CalculateWVPMatrix(pos, scale, rotation, camera);
		transformData->world = worldMatrix;

		cmdList->SetGraphicsRootConstantBufferView(0, textRenderer->GetMaterialResource(bufferIndex)->GetGPUVirtualAddress());
		cmdList->SetGraphicsRootConstantBufferView(1, textRenderer->GetTransformResource(bufferIndex)->GetGPUVirtualAddress());

		// 頂点バッファとインデックスバッファを設定（1回のみ）
		cmdList->IASetVertexBuffers(0, 1, &vertexBufferView_);
		cmdList->IASetIndexBuffer(&indexBufferView_);

		// 各グリフを描画（テクスチャのみ切り替え）
		for (size_t i = 0; i < glyphDrawInfos_.size(); ++i) {
			const auto& info = glyphDrawInfos_[i];

			// テクスチャを設定
			cmdList->SetGraphicsRootDescriptorTable(2, info.glyph->gpuHandle);

			// このグリフの部分だけ描画（StartIndexLocationのみ使用）
			uint32_t indexOffset = static_cast<uint32_t>(i * 6);
			cmdList->DrawIndexedInstanced(6, 1, indexOffset, 0, 0);
		}
	}

	uint32_t TextObject::GetUTF32CharCode(size_t& index) {
		uint8_t byte1 = static_cast<uint8_t>(text_[index++]);

		if (byte1 < 0x80) {
			return byte1;
		} else if ((byte1 & 0xE0) == 0xC0) {
			if (index >= text_.size()) return 0;
			uint8_t byte2 = static_cast<uint8_t>(text_[index++]);
			return ((byte1 & 0x1F) << 6) | (byte2 & 0x3F);
		} else if ((byte1 & 0xF0) == 0xE0) {
			if (index + 1 >= text_.size()) return 0;
			uint8_t byte2 = static_cast<uint8_t>(text_[index++]);
			uint8_t byte3 = static_cast<uint8_t>(text_[index++]);
			return ((byte1 & 0x0F) << 12) | ((byte2 & 0x3F) << 6) | (byte3 & 0x3F);
		} else if ((byte1 & 0xF8) == 0xF0) {
			if (index + 2 >= text_.size()) return 0;
			uint8_t byte2 = static_cast<uint8_t>(text_[index++]);
			uint8_t byte3 = static_cast<uint8_t>(text_[index++]);
			uint8_t byte4 = static_cast<uint8_t>(text_[index++]);
			return ((byte1 & 0x07) << 18) | ((byte2 & 0x3F) << 12) | ((byte3 & 0x3F) << 6) | (byte4 & 0x3F);
		}

		return 0;
	}

#ifdef _DEBUG
	bool TextObject::DrawImGui() {
		bool changed = false;

		ImGui::PushID(this);

		char buffer[256];
		strncpy_s(buffer, text_.c_str(), sizeof(buffer) - 1);
		if (ImGui::InputText("Text", buffer, sizeof(buffer))) {
			SetText(buffer);  // SetTextを使用してisDirtyを設定
			changed = true;
		}

		if (ImGui::ColorEdit4("Color", &color_.x)) {
			changed = true;
		}

		if (ImGui::DragFloat("Scale", &scale_, 0.1f, 0.1f, 10.0f)) {
			isDirty_ = true;  // スケール変更時は再構築
			changed = true;
		}

		if (ImGui::DragFloat3("Position", &transform_.translate.x, 1.0f)) {
			isDirty_ = true;  // 位置変更時は再構築
			changed = true;
		}

		const char* blendModeNames[] = { "None", "Normal", "Add", "Subtract", "Multiply" };
		int currentBlendMode = static_cast<int>(blendMode_);
		if (ImGui::Combo("Blend Mode", &currentBlendMode, blendModeNames, IM_ARRAYSIZE(blendModeNames))) {
			blendMode_ = static_cast<BlendMode>(currentBlendMode);
			changed = true;
		}

		ImGui::Separator();
		ImGui::Text("Glyph Count: %zu", glyphDrawInfos_.size());

		ImGui::PopID();

		return changed;
	}
#endif
}
