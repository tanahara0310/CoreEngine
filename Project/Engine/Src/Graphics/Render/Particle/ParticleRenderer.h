#pragma once

#include "BaseParticleRenderer.h"
#include "Graphics/Model/VertexData.h"
#include <d3d12.h>
#include <wrl.h>

namespace CoreEngine
{
/// @brief ビルボードパーティクル専用レンダラー
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

    /// @brief 板ポリの粒を描画
    /// @param particle パーティクルのコンポーネント
    void Draw(ParticleSystemComponent* particle) override;

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
