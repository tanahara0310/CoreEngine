#include "Vignette.h"
#include "Engine/Utility/Debug/ImGui/ImguiManager.h"
#include <cassert>


namespace CoreEngine
{
void Vignette::Initialize(DirectXCommon* dxCommon)
{
	// 基底クラスの初期化
	PostEffectBase::Initialize(dxCommon);
	
	// 定数バッファの作成
	CreateConstantBuffer();
}

void Vignette::DrawImGui()
{
#ifdef _DEBUG
	ImGui::PushID("VignetteParams");
	
	ImGui::Text("状態: %s", IsEnabled() ? "有効" : "無効");
	ImGui::Text("画像の端を暗くする効果を作成します");
	ImGui::Separator();
	
	bool paramsChanged = false;
	
	// パラメータ設定
	if (ImGui::TreeNode("パラメータ")) {
		// ヴィネット強度の調整
		paramsChanged |= ImGui::SliderFloat("強度", &params_.intensity, 0.0f, 2.0f);
		
		// 滑らかさの調整
		paramsChanged |= ImGui::SliderFloat("滑らかさ", &params_.smoothness, 0.1f, 2.0f);
		
		// サイズの調整
		paramsChanged |= ImGui::SliderFloat("サイズ", &params_.size, 1.0f, 50.0f);
		
		ImGui::TreePop();
	}
	
	// パラメータが変更された場合、即座に定数バッファを更新
	if (paramsChanged) {
		UpdateConstantBuffer();
	}
	
	ImGui::Separator();
	
	if (ImGui::Button("デフォルトに戻す")) {
		params_.intensity = 0.8f;
		params_.smoothness = 0.8f;
		params_.size = 16.0f;
		UpdateConstantBuffer();
	}
	
	if (!IsEnabled()) {
		ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "注意: エフェクトは無効ですが、パラメータは調整可能です");
	}
	
	ImGui::Separator();
	
	ImGui::PopID();
#endif // _DEBUG
}

void Vignette::SetParams(const VignetteParams& params)
{
	params_ = params;
	UpdateConstantBuffer();
}

void Vignette::ForceUpdateConstantBuffer()
{
	UpdateConstantBuffer();
}

void Vignette::BindOptionalCBVs(ID3D12GraphicsCommandList* commandList)
{
	// 定数バッファをピクセルシェーダーにバインド
	if (constantBuffer_) {
		commandList->SetGraphicsRootConstantBufferView(1, constantBuffer_->GetGPUVirtualAddress());
	}
}

void Vignette::UpdateConstantBuffer()
{
	// 定数バッファにデータをコピー
	if (mappedData_) {
		*mappedData_ = params_;
	}
}

void Vignette::CreateConstantBuffer()
{
	assert(directXCommon_);
	
	// 定数バッファのサイズを256バイトアライメントに調整
	UINT bufferSize = (sizeof(VignetteParams) + 255) & ~255;
	
	// ヒーププロパティ
	D3D12_HEAP_PROPERTIES heapProps = {};
	heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
	heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	
	// リソースデスク
	D3D12_RESOURCE_DESC resourceDesc = {};
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	resourceDesc.Width = bufferSize;
	resourceDesc.Height = 1;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.MipLevels = 1;
	resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
	
	// リソースの作成
	HRESULT hr = directXCommon_->GetDevice()->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&resourceDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&constantBuffer_)
	);
	assert(SUCCEEDED(hr));
	
	// マッピング
	hr = constantBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&mappedData_));
	assert(SUCCEEDED(hr));
	
	// 初期値で更新
	UpdateConstantBuffer();
}
}
