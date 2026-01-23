#include "Bloom.h"
#include "Engine/Utility/Debug/ImGui/ImguiManager.h"
#include <cassert>


namespace CoreEngine
{
void Bloom::Initialize(DirectXCommon* dxCommon)
{
	// 基底クラスの初期化
	PostEffectBase::Initialize(dxCommon);
	
	// 定数バッファの作成
	CreateConstantBuffer();
}

const std::wstring& Bloom::GetPixelShaderPath() const
{
	static const std::wstring path = L"Assets/Shaders/PostProcess/Bloom.PS.hlsl";
	return path;
}

void Bloom::DrawImGui()
{
#ifdef _DEBUG
	ImGui::PushID("BloomParams");
	
	ImGui::Text("状態: %s", IsEnabled() ? "有効" : "無効");
	ImGui::Separator();
	
	bool paramsChanged = false;
	
	// パラメータ設定
	if (ImGui::TreeNode("パラメータ")) {
		// 輝度閾値の調整
		if (ImGui::SliderFloat("輝度しきい値", &params_.threshold, 0.0f, 2.0f)) {
			paramsChanged = true;
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("この値より明るいピクセルがブルームの対象になります");
		}
		
		// ブルーム強度の調整
		if (ImGui::SliderFloat("強度", &params_.intensity, 0.0f, 3.0f)) {
			paramsChanged = true;
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("ブルーム効果の強さを調整します");
		}
		
		// ブラー半径の調整
		if (ImGui::SliderFloat("ブラー半径", &params_.blurRadius, 0.5f, 5.0f)) {
			paramsChanged = true;
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("光の広がりの範囲を調整します");
		}
		
		// ソフトニーの調整
		if (ImGui::SliderFloat("ソフトニー", &params_.softKnee, 0.0f, 1.0f)) {
			paramsChanged = true;
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("閾値付近の滑らかさを調整します (0=ハード, 1=ソフト)");
		}
		
		ImGui::TreePop();
	}
	
	// パラメータが変更された場合、即座に定数バッファを更新
	if (paramsChanged) {
		UpdateConstantBuffer();
	}
	
	ImGui::Separator();
	
	if (ImGui::Button("デフォルトに戻す")) {
		params_.threshold = 0.8f;
		params_.intensity = 1.0f;
		params_.blurRadius = 2.0f;
		params_.softKnee = 0.5f;
		UpdateConstantBuffer();
	}
	
	if (!IsEnabled()) {
		ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "注意: エフェクトは無効ですが、パラメータは調整可能です");
	}
	
	ImGui::PopID();
#endif // _DEBUG
}

void Bloom::SetParams(const BloomParams& params)
{
	params_ = params;
	UpdateConstantBuffer();
}

void Bloom::ForceUpdateConstantBuffer()
{
	UpdateConstantBuffer();
}

void Bloom::BindOptionalCBVs(ID3D12GraphicsCommandList* commandList)
{
	// 定数バッファをピクセルシェーダーにバインド
	if (constantBuffer_) {
		commandList->SetGraphicsRootConstantBufferView(1, constantBuffer_->GetGPUVirtualAddress());
	}
}

void Bloom::UpdateConstantBuffer()
{
	// 定数バッファにデータをコピー
	if (mappedData_) {
		*mappedData_ = params_;
	}
}

void Bloom::CreateConstantBuffer()
{
	assert(directXCommon_);
	
	// 定数バッファのサイズを256バイトアライメントに調整
	UINT bufferSize = (sizeof(BloomParams) + 255) & ~255;
	
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
