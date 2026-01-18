#pragma once
#include "Engine/Graphics/Render/IRenderer.h"
#include "Engine/Graphics/PipelineStateManager.h"
#include "Engine/Graphics/RootSignatureManager.h"
#include "Engine/Graphics/Shader/ShaderCompiler.h"
#include "Engine/Graphics/Common/DirectXCommon.h"
#include "Engine/Graphics/Resource/ResourceFactory.h"
#include "MathCore.h"
#include <d3d12.h>
#include <wrl.h>
#include <memory>
#include <vector>

// Sprite用 Root Parameter インデックス定数

namespace CoreEngine
{

// 前方宣言
struct SpriteMaterial;
namespace SpriteRendererRootParam {
    static constexpr UINT kMaterial = 0;     // マテリアル用CBV (b0, PS)
    static constexpr UINT kTransform = 1;    // トランスフォーム用CBV (b1, VS)
    static constexpr UINT kTexture = 2;      // テクスチャ用SRV (t0, PS)
}

/// @brief スプライト描画用レンダラー
class SpriteRenderer : public IRenderer {
public:
    /// @brief トランスフォーム行列
    struct TransformationMatrix {
        Matrix4x4 WVP;
        Matrix4x4 world;
    };
    
    /// @brief 最大スプライト数
    static constexpr size_t kMaxSpriteCount = 1024;
    
    /// @brief フレーム数（ダブルバッファリング）
    static constexpr UINT kFrameCount = 2;
    
    // IRendererインターフェースの実装
    void Initialize(ID3D12Device* device) override;
    void BeginPass(ID3D12GraphicsCommandList* cmdList, BlendMode blendMode) override;
    void EndPass() override;
    RenderPassType GetRenderPassType() const override { return RenderPassType::Sprite; }
    void SetCamera(const ICamera* camera) override;
    
    /// @brief 初期化（DirectXCommonとResourceFactory付き）
    /// @param dxCommon DirectXCommon
    /// @param resourceFactory ResourceFactory
    void Initialize(DirectXCommon* dxCommon, ResourceFactory* resourceFactory);
    
    /// @brief ルートシグネチャを取得
    ID3D12RootSignature* GetRootSignature() const { return rootSignatureMg_->GetRootSignature(); }
    
    /// @brief 利用可能な定数バッファのインデックスを取得
    /// @return バッファインデックス
    size_t GetAvailableConstantBuffer();
    
    /// @brief WVP行列を計算
    /// @param position 位置
    /// @param scale スケール
    /// @param rotation 回転
    /// @return WVP行列
    Matrix4x4 CalculateWVPMatrix(const Vector3& position, const Vector3& scale, const Vector3& rotation) const;
    
    /// @brief WVP行列を計算（カメラ使用版）
    /// @param position 位置
    /// @param scale スケール
    /// @param rotation 回転
    /// @param camera カメラ
    /// @return WVP行列
    Matrix4x4 CalculateWVPMatrix(const Vector3& position, const Vector3& scale, const Vector3& rotation, const ICamera* camera) const;
    
    /// @brief DirectXCommonを取得
    DirectXCommon* GetDirectXCommon() { return dxCommon_; }
    
    /// @brief ResourceFactoryを取得
    ResourceFactory* GetResourceFactory() { return resourceFactory_; }
    
    /// @brief マテリアルデータプールを取得
    std::vector<SpriteMaterial*>& GetMaterialDataPool() { return materialDataPool_[currentFrameIndex_]; }
    
    /// @brief トランスフォームデータプールを取得
    std::vector<TransformationMatrix*>& GetTransformDataPool() { return transformDataPool_[currentFrameIndex_]; }
    
    /// @brief マテリアルリソースを取得
    Microsoft::WRL::ComPtr<ID3D12Resource>& GetMaterialResource(size_t index) { return materialResources_[currentFrameIndex_][index]; }
    
    /// @brief トランスフォームリソースを取得
    Microsoft::WRL::ComPtr<ID3D12Resource>& GetTransformResource(size_t index) { return transformResources_[currentFrameIndex_][index]; }
    
private:
    std::unique_ptr<RootSignatureManager> rootSignatureMg_ = std::make_unique<RootSignatureManager>();
    std::unique_ptr<PipelineStateManager> psoMg_ = std::make_unique<PipelineStateManager>();
    std::unique_ptr<ShaderCompiler> shaderCompiler_ = std::make_unique<ShaderCompiler>();
    
    ID3D12PipelineState* pipelineState_ = nullptr;
    BlendMode currentBlendMode_ = BlendMode::kBlendModeAdd;
    
    // DirectXCommonとResourceFactory
    DirectXCommon* dxCommon_ = nullptr;
    ResourceFactory* resourceFactory_ = nullptr;
    
    // 定数バッファプール（フレームごとに分離）
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> materialResources_[kFrameCount];
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> transformResources_[kFrameCount];
    std::vector<SpriteMaterial*> materialDataPool_[kFrameCount];
    std::vector<TransformationMatrix*> transformDataPool_[kFrameCount];
    
    // 現在のバッファインデックスとフレームインデックス
    size_t currentBufferIndex_ = 0;
    UINT currentFrameIndex_ = 0;
};
}
