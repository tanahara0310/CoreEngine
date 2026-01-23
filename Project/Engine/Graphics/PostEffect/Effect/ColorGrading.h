#pragma once

#include "../PostEffectBase.h"
#include <wrl.h>
#include <d3d12.h>


namespace CoreEngine
{
class ColorGrading : public PostEffectBase {
public:
	/// @brief カラーグレーディングパラメータ構造体
	struct ColorGradingParams {
		float hue = 0.0f;          // 色相調整 (-1.0 to 1.0)
		float saturation = 1.0f;   // 彩度調整 (0.0 to 3.0)
		float value = 1.0f;        // 明度調整 (0.0 to 3.0)
		float contrast = 1.0f;     // コントラスト (0.0 to 3.0)
		
		float gamma = 1.0f;        // ガンマ補正 (0.1 to 3.0)
		float temperature = 0.0f;  // 色温度 (-1.0 to 1.0, 負値=寒色, 正値=暖色)
		float tint = 0.0f;         // ティント (-1.0 to 1.0, 負値=緑, 正値=マゼンタ)
		float exposure = 0.0f;     // 露出調整 (-3.0 to 3.0)
		
		// Shadow, Midtone, Highlight調整
		float shadowLift[3] = { 0.0f, 0.0f, 0.0f };   // RGB Shadow Lift
		float midtoneGamma[3] = { 1.0f, 1.0f, 1.0f }; // RGB Midtone Gamma
		float highlightGain[3] = { 1.0f, 1.0f, 1.0f }; // RGB Highlight Gain
		float padding = 0.0f;      // パディング（16バイトアライメント）
	};

public:
	ColorGrading() = default;
	~ColorGrading() = default;

	/// @brief 初期化
	void Initialize(class DirectXCommon* dxCommon);

	/// @brief ImGuiでパラメータを調整
	void DrawImGui() override;

	/// @brief パラメータを取得
	/// @return パラメータ構造体の参照
	const ColorGradingParams& GetParams() const { return params_; }

	/// @brief パラメータを設定して定数バッファを更新
	/// @param params 新しいパラメータ
	void SetParams(const ColorGradingParams& params);

	/// @brief プリセット適用関数
	void ApplyPreset(int presetIndex);

protected:
	const std::wstring& GetPixelShaderPath() const override
	{
		static const std::wstring pixelShaderPath = L"Assets/Shaders/PostProcess/ColorGrading.PS.hlsl";
		return pixelShaderPath;
	}

	void BindOptionalCBVs(ID3D12GraphicsCommandList* commandList) override;

private:
	/// @brief 定数バッファの作成
	void CreateConstantBuffer();
	
	/// @brief 定数バッファの更新
	void UpdateConstantBuffer();

private:
	ColorGradingParams params_;
	Microsoft::WRL::ComPtr<ID3D12Resource> constantBuffer_;
	ColorGradingParams* mappedData_ = nullptr;
};
}
