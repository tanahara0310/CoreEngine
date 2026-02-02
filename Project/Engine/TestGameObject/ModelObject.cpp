#include "ModelObject.h"
#include <EngineSystem.h>
#include "Engine/Camera/ICamera.h"

namespace CoreEngine
{
	void ModelObject::Initialize(const std::string& modelPath) {
		// 必須コンポーネントの取得
		auto engine = GetEngineSystem();

		auto dxCommon = engine->GetComponent<DirectXCommon>();
		auto modelManager = engine->GetComponent<ModelManager>();

		if (!dxCommon || !modelManager) {
			return;
		}

		// 静的モデルとして作成
		model_ = modelManager->CreateStaticModel(modelPath);

		// トランスフォームの初期化
		transform_.Initialize(dxCommon->GetDevice());

		// デフォルトテクスチャの読み込み
		auto& textureManager = TextureManager::GetInstance();
		texture_ = textureManager.Load("Texture/white1x1.png");

		// アクティブ状態に設定
		SetActive(true);
	}

	void ModelObject::Update() {
		if (!IsActive() || !model_) {
			return;
		}

		// トランスフォームの更新
		transform_.TransferMatrix();
	}

	void ModelObject::Draw(const ICamera* camera) {
		if (!camera || !model_) return;

		// モデルの描画
		model_->Draw(transform_, camera, texture_.gpuHandle);
	}

	void ModelObject::SetPBRParameters(float metallic, float roughness, float ao) {
		if (!model_) return;

		auto* materialManager = model_->GetMaterialManager();
		if (materialManager) {
			materialManager->SetPBRParameters(metallic, roughness, ao);
		}
	}

	void ModelObject::SetPBREnabled(bool enable) {
		if (!model_) return;

		auto* materialManager = model_->GetMaterialManager();
		if (materialManager) {
			materialManager->SetEnablePBR(enable);
		}
	}

	void ModelObject::SetEnvironmentMapEnabled(bool enable) {
		if (!model_) return;

		auto* materialManager = model_->GetMaterialManager();
		if (materialManager) {
			materialManager->SetEnableEnvironmentMap(enable);
		}
	}

	void ModelObject::SetEnvironmentMapIntensity(float intensity) {
		if (!model_) return;

		auto* materialManager = model_->GetMaterialManager();
		if (materialManager) {
			materialManager->SetEnvironmentMapIntensity(intensity);
		}
	}

	void ModelObject::SetMaterialColor(const Vector4& color) {
		if (!model_) return;

		auto* materialManager = model_->GetMaterialManager();
		if (materialManager) {
			materialManager->SetColor(color);
		}
	}

	void ModelObject::SetIBLEnabled(bool enable) {
		if (!model_) return;

		auto* materialManager = model_->GetMaterialManager();
		if (materialManager) {
			materialManager->SetEnableIBL(enable);
		}
	}

	void ModelObject::SetIBLIntensity(float intensity) {
		if (!model_) return;

		auto* materialManager = model_->GetMaterialManager();
		if (materialManager) {
			materialManager->SetIBLIntensity(intensity);
		}
	}

	void ModelObject::SetEnvironmentRotationY(float rotationY) {
		if (!model_) return;

		auto* materialManager = model_->GetMaterialManager();
		if (materialManager) {
			materialManager->SetEnvironmentRotationY(rotationY);
		}
	}
}
