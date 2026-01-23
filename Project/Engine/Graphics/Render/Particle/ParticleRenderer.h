#pragma once

#include "BaseParticleRenderer.h"
#include "Engine/Graphics/Structs/VertexData.h"
#include <d3d12.h>
#include <wrl.h>

// 前方宣言
class ParticleSystem;

/// @brief ビルボードパーティクル専用レンダラー

namespace CoreEngine
{
class ParticleRenderer : public BaseParticleRenderer {
public:
	ParticleRenderer() = default;
	~ParticleRenderer() override = default;

	/// @brief 初期化（基底クラス + 頂点バッファ作成）
	/// @param device D3D12デバイス
	void Initialize(ID3D12Device* device) override;

	/// @brief このレンダラーがサポートする描画タイプを取得
	/// @return 描画パスタイプ
	RenderPassType GetRenderPassType() const override { return RenderPassType::Particle; }

	/// @brief パーティクルシステムを描画
	/// @param particle パーティクルシステム
	void Draw(ParticleSystem* particle) override;

protected:
	/// @brief パイプラインステートオブジェクトの作成（ビルボード用）
	void CreatePSO() override;

	/// @brief BeginPassでの追加処理（頂点バッファ設定）
	void OnBeginPass() override;

private:
	// 頂点バッファ（全パーティクルシステムで共有）
	Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer_;
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView_ = {};

	/// @brief 共有頂点バッファの作成
	void CreateSharedVertexBuffer();
};
}
