#include "SphereObject.h"
#include <EngineSystem.h>
#include "Engine/Camera/ICamera.h"

#ifdef _DEBUG
#include "externals/imgui/imgui.h"
#endif


namespace CoreEngine
{
	void SphereObject::Initialize() {
		// 必須コンポーネントの取得
		auto engine = GetEngineSystem();

		auto dxCommon = engine->GetComponent<DirectXCommon>();
		auto modelManager = engine->GetComponent<ModelManager>();

		if (!dxCommon || !modelManager) {
			return;
		}

		// 静的モデルとして作成
		model_ = modelManager->CreateStaticModel("SampleAssets/Sphere/sphere.obj");

		// トランスフォームの初期化
		transform_.Initialize(dxCommon->GetDevice());

		// テクスチャの読み込み
		auto& textureManager = TextureManager::GetInstance();
		texture_ = textureManager.Load("Texture/white1x1.png");

		// アクティブ状態に設定
		SetActive(true);
	}

	void SphereObject::Update() {
		if (!IsActive() || !model_) {
			return;
		}

		// トランスフォームの更新
		transform_.TransferMatrix();
	}

	void SphereObject::Draw(const ICamera* camera) {
		if (!camera || !model_) return;

		// モデルの描画
		model_->Draw(transform_, camera, texture_.gpuHandle);
	}

	void SphereObject::SetPBRParameters(float metallic, float roughness, float ao) {
		if (!model_) return;

		auto* materialManager = model_->GetMaterialManager();
		if (materialManager) {
			materialManager->SetPBRParameters(metallic, roughness, ao);
		}
	}

	void SphereObject::SetPBREnabled(bool enable) {
		if (!model_) return;

		auto* materialManager = model_->GetMaterialManager();
		if (materialManager) {
			materialManager->SetEnablePBR(enable);
		}
	}

	void SphereObject::SetEnvironmentMapEnabled(bool enable) {
		if (!model_) return;

		auto* materialManager = model_->GetMaterialManager();
		if (materialManager) {
			materialManager->SetEnableEnvironmentMap(enable);
		}
	}

	void SphereObject::SetEnvironmentMapIntensity(float intensity) {
		if (!model_) return;

		auto* materialManager = model_->GetMaterialManager();
		if (materialManager) {
			materialManager->SetEnvironmentMapIntensity(intensity);
		}
	}

	void SphereObject::SetMaterialColor(const Vector4& color) {
		if (!model_) return;

		auto* materialManager = model_->GetMaterialManager();
		if (materialManager) {
			materialManager->SetColor(color);
		}
	}

	void SphereObject::SetIBLEnabled(bool enable) {
		if (!model_) return;

		auto* materialManager = model_->GetMaterialManager();
		if (materialManager) {
			materialManager->SetEnableIBL(enable);
		}
	}

	void SphereObject::SetIBLIntensity(float intensity) {
		if (!model_) return;

		auto* materialManager = model_->GetMaterialManager();
		if (materialManager) {
			materialManager->SetIBLIntensity(intensity);
		}
	}

	void SphereObject::SetEnvironmentRotationY(float rotationY) {
		if (!model_) return;

		auto* materialManager = model_->GetMaterialManager();
		if (materialManager) {
			materialManager->SetEnvironmentRotationY(rotationY);
		}
	}

	void SphereObject::SetNormalMap(const std::string& texturePath) {
		if (!model_) return;

		auto* materialManager = model_->GetMaterialManager();
		if (materialManager) {
			materialManager->SetNormalMap(texturePath);
		}
	}

	void SphereObject::SetNormalMapEnabled(bool enable) {
		if (!model_) return;

		auto* materialManager = model_->GetMaterialManager();
		if (materialManager) {
			materialManager->SetUseNormalMap(enable);
		}
	}

	void SphereObject::SetAlbedoTexture(const std::string& texturePath) {
		// テクスチャを読み込み
		auto& textureManager = TextureManager::GetInstance();
		texture_ = textureManager.Load(texturePath);
	}

	void SphereObject::SetMetallicMap(const std::string& texturePath) {
		if (!model_) return;

		auto* materialManager = model_->GetMaterialManager();
		if (materialManager) {
			materialManager->SetMetallicMap(texturePath);
		}
	}

	void SphereObject::SetRoughnessMap(const std::string& texturePath) {
		if (!model_) return;

		auto* materialManager = model_->GetMaterialManager();
		if (materialManager) {
			materialManager->SetRoughnessMap(texturePath);
		}
	}

	void SphereObject::SetAOMap(const std::string& texturePath) {
		if (!model_) return;

		auto* materialManager = model_->GetMaterialManager();
		if (materialManager) {
			materialManager->SetAOMap(texturePath);
		}
	}

#ifdef _DEBUG
	bool SphereObject::DrawImGuiExtended() {
		bool changed = false;

		if (!model_) return changed;

		auto* materialManager = model_->GetMaterialManager();
		if (!materialManager) return changed;

		// === PBR Settings ===
		if (ImGui::CollapsingHeader("PBR Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
			// PBR有効/無効
			bool enablePBR = materialManager->IsEnablePBR();
			if (ImGui::Checkbox("Enable PBR", &enablePBR)) {
				SetPBREnabled(enablePBR);
				changed = true;
			}

			if (enablePBR) {
				// Metallic
				float metallic = materialManager->GetMetallic();
				if (ImGui::SliderFloat("Metallic", &metallic, 0.0f, 1.0f)) {
					materialManager->SetMetallic(metallic);
					changed = true;
				}

				// Roughness
				float roughness = materialManager->GetRoughness();
				if (ImGui::SliderFloat("Roughness", &roughness, 0.0f, 1.0f)) {
					materialManager->SetRoughness(roughness);
					changed = true;
				}

				// AO (Ambient Occlusion)
				float ao = materialManager->GetAO();
				if (ImGui::SliderFloat("AO", &ao, 0.0f, 1.0f)) {
					materialManager->SetAO(ao);
					changed = true;
				}
			}
		}

		// === Environment Map Settings ===
		if (ImGui::CollapsingHeader("Environment Map", ImGuiTreeNodeFlags_DefaultOpen)) {
			// 環境マップ有効/無効
			bool enableEnvMap = materialManager->IsEnableEnvironmentMap();
			if (ImGui::Checkbox("Enable Environment Map", &enableEnvMap)) {
				SetEnvironmentMapEnabled(enableEnvMap);
				changed = true;
			}

			if (enableEnvMap) {
				// 環境マップ強度
				float envIntensity = materialManager->GetEnvironmentMapIntensity();
				if (ImGui::SliderFloat("Env Intensity", &envIntensity, 0.0f, 2.0f)) {
					SetEnvironmentMapIntensity(envIntensity);
					changed = true;
				}
			}
		}

		// === IBL Settings ===
		if (ImGui::CollapsingHeader("IBL (Image-Based Lighting)", ImGuiTreeNodeFlags_DefaultOpen)) {
			// IBL有効/無効
			bool enableIBL = materialManager->IsEnableIBL();
			if (ImGui::Checkbox("Enable IBL", &enableIBL)) {
				SetIBLEnabled(enableIBL);
				changed = true;
			}

			if (enableIBL) {
				// IBL強度
				float iblIntensity = materialManager->GetIBLIntensity();
				if (ImGui::SliderFloat("IBL Intensity", &iblIntensity, 0.0f, 2.0f)) {
					SetIBLIntensity(iblIntensity);
					changed = true;
				}
			}
		}

		// === Material Color ===
		if (ImGui::CollapsingHeader("Material Color")) {
			Vector4 color = materialManager->GetColor();
			float colorArray[4] = { color.x, color.y, color.z, color.w };
			if (ImGui::ColorEdit4("Color", colorArray)) {
				SetMaterialColor(Vector4(colorArray[0], colorArray[1], colorArray[2], colorArray[3]));
				changed = true;
			}
		}

		return changed;
	}
#endif
}
