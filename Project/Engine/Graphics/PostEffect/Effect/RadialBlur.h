#pragma once
#include "../PostEffectBase.h"
#include <wrl.h>
#include <d3d12.h>


namespace CoreEngine
{
class RadialBlur : public PostEffectBase {
public:
	/// @brief ラジアルブラーパラメータ構造体
	struct RadialBlurParams {
		float intensity = 0.5f;      // ブラー強度 (0.0-2.0)
		float sampleCount = 8.0f;    // サンプル数 (4.0-16.0)
		float centerX = 0.5f;        // ブラー中心のX座標 (0.0-1.0)
		float centerY = 0.5f;        // ブラー中心のY座標 (0.0-1.0)
	};

public:
	RadialBlur() = default;
	~RadialBlur() = default;

	/// @brief 初期化
	void Initialize(class DirectXCommon* dxCommon);

	/// @brief ImGuiでパラメータを調整
	void DrawImGui() override;

	/// @brief パラメータを取得
	/// @return パラメータ構造体の参照
	const RadialBlurParams& GetParams() const { return params_; }
	
	/// @brief パラメータを設定（GPU転送も含む）
	/// @param newParams 新しいパラメータ
	void SetParams(const RadialBlurParams& newParams);

protected:
	const std::wstring& GetPixelShaderPath() const override;
	void BindOptionalCBVs(ID3D12GraphicsCommandList* commandList) override;

private:
	/// @brief 定数バッファの作成
	void CreateConstantBuffer();
	
	/// @brief 定数バッファの更新
	void UpdateConstantBuffer();

private:
	RadialBlurParams params_;
	Microsoft::WRL::ComPtr<ID3D12Resource> constantBuffer_;
	RadialBlurParams* mappedData_ = nullptr;
};
}
