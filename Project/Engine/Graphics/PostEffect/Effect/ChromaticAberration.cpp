#include "ChromaticAberration.h"
#include "Engine/Utility/Debug/ImGui/ImguiManager.h"
#include <cassert>


namespace CoreEngine
{
void ChromaticAberration::Initialize(DirectXCommon* dxCommon)
{
	// 基底クラスの初期化
	PostEffectBase::Initialize(dxCommon);
	
	// 定数バッファの作成
	CreateConstantBuffer();
}

void ChromaticAberration::DrawImGui()
{
#ifdef _DEBUG
	ImGui::PushID("ChromaticAberrationParams");
	
	ImGui::Text("状態: %s", IsEnabled() ? "有効" : "無効");
	ImGui::Text("カメラレンズの色収差効果をシミュレートします");
	ImGui::Separator();
	
	bool paramsChanged = false;
	
	// 基本パラメータ
	if (ImGui::TreeNode("基本パラメータ")) {
		ImGui::Text("強度: エフェクト全体の強さを制御");
		paramsChanged |= ImGui::SliderFloat("強度", &params_.intensity, 0.0f, 20.0f, "%.2f");
		
		ImGui::Text("放射係数: 距離ベースの乗数");
		paramsChanged |= ImGui::SliderFloat("放射係数", &params_.radialFactor, 0.0f, 3.0f, "%.2f");
 
		ImGui::Text("ゆがみスケール: 全体的な変位スケール");
		paramsChanged |= ImGui::SliderFloat("歪みスケール", &params_.distortionScale, 0.0f, 5.0f, "%.2f");
		
		ImGui::Text("エッジフォールオフ: エッジの強度を制御");
		paramsChanged |= ImGui::SliderFloat("エッジフォールオフ", &params_.falloff, 0.1f, 5.0f, "%.2f");
		
		ImGui::TreePop();
	}
	
	// 中心位置調整
	if (ImGui::TreeNode("中心位置")) {
		paramsChanged |= ImGui::SliderFloat("中心X", &params_.centerX, 0.0f, 1.0f, "%.3f");
		paramsChanged |= ImGui::SliderFloat("中心Y", &params_.centerY, 0.0f, 1.0f, "%.3f");
	 
		if (ImGui::Button("中心をリセット")) {
			params_.centerX = 0.5f;
			params_.centerY = 0.5f;
			paramsChanged = true;
		}
		
		ImGui::TreePop();
	}
	
	// プリセット
	if (ImGui::TreeNode("プリセット")) {
		ImGui::Text("よく使用されるケースのクイックプリセット:");
		
		if (ImGui::Button("微妙")) {
			ApplyPreset(0);
			paramsChanged = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("中程度")) {
			ApplyPreset(1);
			paramsChanged = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("強い")) {
			ApplyPreset(2);
			paramsChanged = true;
		}
		
		if (ImGui::Button("ヴィンテージレンズ")) {
			ApplyPreset(3);
			paramsChanged = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("広角レンズ")) {
			ApplyPreset(4);
			paramsChanged = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("極端")) {
			ApplyPreset(5);
			paramsChanged = true;
		}
		
		ImGui::TreePop();
	}
	
	// パラメータが変更された場合、即座に定数バッファを更新
	if (paramsChanged) {
		UpdateConstantBuffer();
	}

	ImGui::Separator();
	
	if (ImGui::Button("デフォルトに戻す")) {
		ApplyPreset(1); // 中程度プリセット
		UpdateConstantBuffer();
	}
	
	if (!IsEnabled()) {
		ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), 
			"エフェクトは無効です。色収差を確認するには有効にしてください");
	} else {
		ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), 
			"エフェクトはアクティブ - 画面端で色のずれが見えます!");
	}
	
	ImGui::Separator();
	ImGui::Text("注意: RGBチャンネルが中心から放射状に分離されます");
	ImGui::Text("赤は外側に、青は内側に移動します");
	
	ImGui::PopID();
#endif // _DEBUG
}

void ChromaticAberration::SetParams(const ChromaticAberrationParams& newParams)
{
	params_ = newParams;
	UpdateConstantBuffer();
}

void ChromaticAberration::ApplyPreset(int presetIndex)
{
	switch (presetIndex) {
	case 0: // Subtle - 控えめな効果
		params_.intensity = 1.5f;
		params_.radialFactor = 0.8f;
		params_.centerX = 0.5f;
   params_.centerY = 0.5f;
 params_.distortionScale = 1.0f;
   params_.falloff = 2.0f;
  break;
		
	case 1: // Medium - 標準的な効果
		params_.intensity = 3.0f;
 params_.radialFactor = 1.0f;
	  params_.centerX = 0.5f;
	  params_.centerY = 0.5f;
		params_.distortionScale = 1.5f;
  params_.falloff = 1.5f;
		break;
		
	case 2: // Strong - 強い効果
 params_.intensity = 5.0f;
params_.radialFactor = 1.5f;
		params_.centerX = 0.5f;
		params_.centerY = 0.5f;
		params_.distortionScale = 2.0f;
	  params_.falloff = 1.2f;
 break;
		
	case 3: // Vintage Lens - ヴィンテージレンズ風
 params_.intensity = 4.0f;
	params_.radialFactor = 2.0f;
		params_.centerX = 0.5f;
		params_.centerY = 0.5f;
		params_.distortionScale = 1.8f;
 params_.falloff = 0.8f;
		break;

	case 4: // Wide Angle - 広角レンズ風
		params_.intensity = 6.0f;
		params_.radialFactor = 1.2f;
		params_.centerX = 0.5f;
   params_.centerY = 0.5f;
		params_.distortionScale = 2.5f;
		params_.falloff = 1.0f;
	 break;
		
 case 5: // Extreme - 極端な効果
	 params_.intensity = 10.0f;
		params_.radialFactor = 2.0f;
		params_.centerX = 0.5f;
		params_.centerY = 0.5f;
		params_.distortionScale = 3.0f;
params_.falloff = 0.5f;
		break;
	}
}

void ChromaticAberration::BindOptionalCBVs(ID3D12GraphicsCommandList* commandList)
{
	// 定数バッファをピクセルシェーダーにバインド (register(b0)なのでルートパラメータインデックス1)
	if (constantBuffer_) {
		commandList->SetGraphicsRootConstantBufferView(1, constantBuffer_->GetGPUVirtualAddress());
	}
}

void ChromaticAberration::UpdateConstantBuffer()
{
	// 定数バッファにデータをコピー
	if (mappedData_) {
		*mappedData_ = params_;
	}
}

void ChromaticAberration::CreateConstantBuffer()
{
	assert(directXCommon_);
	
	// 定数バッファのサイズを256バイトアライメントに調整
	UINT bufferSize = (sizeof(ChromaticAberrationParams) + 255) & ~255;
	
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
