#include "TextureViewer.h"
#include "Engine/Graphics/TextureManager.h"

#include <imgui.h>
#include <algorithm>


namespace CoreEngine
{
void TextureViewer::DrawTextureViewer(bool isOpen)
{
	if (!isOpen) {
		return;
	}

	if (ImGui::Begin("Texture Viewer")) {
		auto& textureManager = TextureManager::GetInstance();

		if (!textureManager.IsInitialized()) {
			ImGui::Text("TextureManager is not initialized.");
			ImGui::End();
			return;
		}

		// サムネイルサイズのスライダー
		ImGui::SliderFloat("Thumbnail Size", &thumbnailSize_, 64.0f, 512.0f);
		ImGui::Separator();

		// テクスチャキャッシュの取得
		const auto& textureCache = textureManager.GetTextureCache();

		if (textureCache.empty()) {
			ImGui::Text("No textures loaded.");
			ImGui::End();
			return;
		}

		ImGui::Text("Loaded Textures: %zu", textureCache.size());
		ImGui::Separator();

		// 左側：テクスチャリスト
		ImGui::BeginChild("TextureList", ImVec2(300, 0), true);
		{
			int index = 0;
			for (const auto& [path, texture] : textureCache) {
				// ファイル名のみを抽出
				std::string displayName = path;
				size_t lastSlash = path.find_last_of("/\\");
				if (lastSlash != std::string::npos) {
					displayName = path.substr(lastSlash + 1);
				}

				bool isSelected = (index == selectedTextureIndex_);
				if (ImGui::Selectable(displayName.c_str(), isSelected)) {
					selectedTextureIndex_ = index;
					selectedTexturePath_ = path;
				}

				index++;
			}
		}
		ImGui::EndChild();

		ImGui::SameLine();

		// 右側：テクスチャプレビュー
		ImGui::BeginChild("TexturePreview", ImVec2(0, 0), true);
		{
			if (selectedTextureIndex_ >= 0 && !selectedTexturePath_.empty()) {
				auto it = textureCache.find(selectedTexturePath_);
				if (it != textureCache.end()) {
					const auto& texture = it->second;
					
					ImGui::Text("Path: %s", selectedTexturePath_.c_str());
					
					// メタデータの取得と表示
					try {
						auto metadata = textureManager.GetMetadata(selectedTexturePath_);
						ImGui::Text("Size: %zu x %zu", metadata.width, metadata.height);
						ImGui::Text("Mip Levels: %zu", metadata.mipLevels);
						ImGui::Text("Format: %d", static_cast<int>(metadata.format));
						
						ImGui::Separator();
						
						// テクスチャプレビュー
						ImGui::Text("Preview:");
						
						// アスペクト比を保持してサイズを計算
						float aspectRatio = static_cast<float>(metadata.width) / static_cast<float>(metadata.height);
						ImVec2 imageSize;
						if (aspectRatio > 1.0f) {
							imageSize = ImVec2(thumbnailSize_, thumbnailSize_ / aspectRatio);
						} else {
							imageSize = ImVec2(thumbnailSize_ * aspectRatio, thumbnailSize_);
						}
						
						// テクスチャを表示
						ImGui::Image(
							(ImTextureID)texture.gpuHandle.ptr,
							imageSize,
							ImVec2(0, 0),
							ImVec2(1, 1),
							ImVec4(1, 1, 1, 1),
							ImVec4(0.5f, 0.5f, 0.5f, 1.0f)
						);
						
						// フルサイズ表示オプション
						if (ImGui::Button("View Full Size")) {
							ImGui::OpenPopup("FullSizePreview");
						}
						
						// フルサイズプレビューポップアップ
						if (ImGui::BeginPopupModal("FullSizePreview", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
							ImGui::Text("Full Size Preview: %s", selectedTexturePath_.c_str());
							ImGui::Separator();
							
							ImVec2 fullSize(static_cast<float>(metadata.width), static_cast<float>(metadata.height));
							
							// ウィンドウサイズを考慮したスケーリング
							ImVec2 availableSize = ImGui::GetContentRegionAvail();
							if (fullSize.x > availableSize.x || fullSize.y > availableSize.y) {
								float scaleX = (availableSize.x - 20.0f) / fullSize.x;
								float scaleY = (availableSize.y - 60.0f) / fullSize.y;
								float scale = (scaleX < scaleY) ? scaleX : scaleY;
								fullSize.x *= scale;
								fullSize.y *= scale;
							}
							
							ImGui::Image(
								(ImTextureID)texture.gpuHandle.ptr,
								fullSize
							);
							
							if (ImGui::Button("Close", ImVec2(120, 0))) {
								ImGui::CloseCurrentPopup();
							}
							
							ImGui::EndPopup();
						}
						
					} catch (const std::exception& e) {
						ImGui::TextColored(ImVec4(1, 0, 0, 1), "Error: %s", e.what());
					}
				} else {
					ImGui::Text("Texture not found in cache.");
				}
			} else {
				ImGui::Text("Select a texture from the list.");
			}
		}
		ImGui::EndChild();
	}
	ImGui::End();
}
}
