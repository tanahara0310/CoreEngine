#include "BaseParticleRenderer.h"
#include "Engine/Particle/ParticleSystem.h"
#include "Engine/Graphics/Resource/ResourceFactory.h"
#include "Engine/Camera/ICamera.h"
#include <cassert>


namespace CoreEngine
{
void BaseParticleRenderer::Initialize(ID3D12Device* device) {
	device_ = device;

	// リソースファクトリが設定されているか確認
	assert(resourceFactory_ != nullptr && "ResourceFactory must be set before initialization");

	// パイプラインとシェーダーマネージャーの初期化
	pipelineMg_ = std::make_unique<PipelineStateManager>();
	rootSignatureMg_ = std::make_unique<RootSignatureManager>();
	shaderCompiler_ = std::make_unique<ShaderCompiler>();

	// シェーダーコンパイラの初期化
	shaderCompiler_->Initialize();

	// ルートシグネチャの作成（共通処理）
	CreateRootSignature();

	// パイプラインステートオブジェクトの作成（派生クラスで実装）
	CreatePSO();
}

void BaseParticleRenderer::BeginPass(ID3D12GraphicsCommandList* cmdList, BlendMode blendMode) {
	cmdList_ = cmdList;

	// ルートシグネチャとパイプラインステートを設定
	cmdList_->SetGraphicsRootSignature(rootSignatureMg_->GetRootSignature());
	cmdList_->SetPipelineState(pipelineMg_->GetPipelineState(blendMode));

	// プリミティブトポロジを設定
	cmdList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// 派生クラスでの追加処理
	OnBeginPass();
}

void BaseParticleRenderer::EndPass() {
	cmdList_ = nullptr;
}

void BaseParticleRenderer::SetCamera(const ICamera* camera) {
	camera_ = camera;
}

void BaseParticleRenderer::CreateRootSignature() {
	// Root Parameter 0: インスタンシング用SRV (t0, Vertex Shader)
	RootSignatureManager::DescriptorRangeConfig instanceRange;
	instanceRange.type = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	instanceRange.numDescriptors = 1;
	instanceRange.baseShaderRegister = 0;  // t0
	rootSignatureMg_->AddDescriptorTable({ instanceRange }, D3D12_SHADER_VISIBILITY_VERTEX);

	// Root Parameter 1: テクスチャ用ディスクリプタテーブル (t0, Pixel Shader)
	RootSignatureManager::DescriptorRangeConfig textureRange;
	textureRange.type = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	textureRange.numDescriptors = 1;
	textureRange.baseShaderRegister = 0;  // t0
	rootSignatureMg_->AddDescriptorTable({ textureRange }, D3D12_SHADER_VISIBILITY_PIXEL);

	// Static Sampler (s0, Pixel Shader)
	rootSignatureMg_->AddDefaultLinearSampler(0, D3D12_SHADER_VISIBILITY_PIXEL);

	// RootSignature の作成
	rootSignatureMg_->Create(device_);
}

bool BaseParticleRenderer::ValidateDrawCall(ParticleSystem* particle) const {
	if (!cmdList_ || !particle || !particle->IsActive()) {
		return false;
	}

	uint32_t instanceCount = particle->GetInstanceCount();
	if (instanceCount == 0) {
		return false;
	}

	return true;
}

void BaseParticleRenderer::SetupCommonResources(ParticleSystem* particle, D3D12_GPU_DESCRIPTOR_HANDLE textureHandle) {
	// インスタンシングリソースを設定（Root Parameter 0）
	cmdList_->SetGraphicsRootDescriptorTable(0, particle->GetInstancingSrvHandleGPU());
	
	// テクスチャを設定（Root Parameter 1）
	cmdList_->SetGraphicsRootDescriptorTable(1, textureHandle);
}
}
