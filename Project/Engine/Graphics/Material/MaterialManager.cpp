#include "MaterialManager.h"

namespace CoreEngine
{

	using namespace CoreEngine::MathCore;

	void MaterialManager::Initialize(ID3D12Device* device, ResourceFactory* resourceFactory)
	{
		// マテリアル用のリソースを作る
		materialResource_ = resourceFactory->CreateBufferResource(device, sizeof(Material));
		// マテリアルのデータを書き込む
		materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialData_));

		// 初期値の設定 (白・ライティング有効・単位行列)
		materialData_->color = { 1.0f, 1.0f, 1.0f, 1.0f }; // 色を白に設定
		SetEnableLighting(true); // ライティングを有効にする
		materialData_->uvTransform = Matrix::Identity(); // UVの変換行列を単位行列にする
		materialData_->shininess = 64.0f; // シェーダーの光沢度を設定
		materialData_->shadingMode = 2; // シェーディングモードをHalf-Lambertに設定
		materialData_->toonThreshold = 0.5f; // トゥーンシェーディングの閾値
		materialData_->toonSmoothness = 0.1f; // トゥーンシェーディングの滑らかさ
	materialData_->enableDithering = 1; // ディザリング有効
	materialData_->ditheringScale = 1.0f; // ディザリングスケール
	materialData_->enableEnvironmentMap = 0; // 環境マップ無効（デフォルト）
	materialData_->environmentMapIntensity = 0.3f; // 環境マップ反射強度（デフォルト: 0.3）
	
	// IBLパラメータのデフォルト値
	materialData_->enableIBL = 0; // IBL無効（デフォルト）
	materialData_->iblIntensity = 1.0f; // IBL強度（デフォルト: 1.0）
}
}
