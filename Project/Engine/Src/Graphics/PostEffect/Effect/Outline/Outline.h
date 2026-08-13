#pragma once

#include "../PostEffectComputeBase.h"
#include <wrl.h>
#include <d3d12.h>


namespace CoreEngine
{
/// @brief Depth Based Outline エフェクト（CS方式）
/// @details 深度バッファのSobelフィルタによりエッジを検出し、アウトラインを描画する。
///          調整パラメータは CVar（"r.Outline.*"）が唯一の保持者。
///          near/far クリップ距離は毎フレームカメラから設定される実行時値なので CVar 化していない。
///          ImGui と保存は CVar 側で自動生成される（Docs/Engine/Editor/CVar_Design.md）
class Outline : public PostEffectComputeBase {
public:
	/// @brief アウトラインパラメータ構造体（GPU 定数バッファのレイアウト）
	struct OutlineParams {
		float outlineColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f }; ///< アウトライン色 (RGBA)
		float depthThreshold  = 0.5f;   ///< 深度エッジ検出閾値（線形深度差、単位：m）
		float depthStrength   = 1.0f;   ///< エッジ強度乗数 (1.0 ~ 20.0)
		float outlineWidth    = 1.0f;   ///< アウトライン太さ (1.0 ~ 4.0)
		float nearPlane       = 0.1f;   ///< カメラのニアクリップ距離（実行時値）
		float farPlane        = 1000.0f; ///< カメラのファークリップ距離（実行時値）
		float pad[3]          = {};
	};

	static constexpr Cb::Field kOutlineParamsFields[] = {
	    CB_FIELD(OutlineParams, outlineColor), CB_FIELD(OutlineParams, depthThreshold),
	    CB_FIELD(OutlineParams, depthStrength), CB_FIELD(OutlineParams, outlineWidth),
	    CB_FIELD(OutlineParams, nearPlane), CB_FIELD(OutlineParams, farPlane), CB_FIELD(OutlineParams, pad),
	};
	CB_VERIFY_LAYOUT(OutlineParams, kOutlineParamsFields);
	CB_BIND_HLSL(OutlineParams, kOutlineParamsFields, "OutlineParams");

public:
	Outline() = default;
	~Outline() = default;

	/// @brief CSエフェクト実行
	/// @param inputSrvHandle カラーテクスチャのSRVハンドル
	/// @param outputUavHandle 出力テクスチャのUAVハンドル
	/// @param width 出力幅
	/// @param height 出力高さ
	void Dispatch(
		D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle,
		D3D12_GPU_DESCRIPTOR_HANDLE outputUavHandle,
		uint32_t width,
		uint32_t height) override;

	/// @brief ImGuiでパラメータを調整
	void DrawImGui() override;

	/// @brief CVar の現在値を定数バッファへ書き込む
	void UpdateConstantBuffer();

	/// @brief 今フレームのカメラからクリップ距離を取り込む（線形深度変換に使用）
	void PrepareFrame(const PostEffectFrameContext& ctx) override;

	/// @brief Sobel フィルタの入力として深度を要求する
	void DeclareExtraInputs(std::vector<PostEffectInputBinding>& out) const override;

protected:
	/// @brief 有効/無効は CVar "r.<Effect>.Enabled" が保持する
	CVar<bool>* GetEnabledCVar() const override;

	std::string  GetEffectName()        const override { return "Outline"; }
	std::wstring GetComputeShaderPath() const override { return L"Outline.CS.hlsl"; }
	void OnCreateConstantBuffers() override;

private:
	/// @brief クリップ距離を更新する（変化したときだけ定数バッファへ転送する）
	void SetCameraClipPlanes(float nearPlane, float farPlane);


private:
	Microsoft::WRL::ComPtr<ID3D12Resource> outlineParamsCB_;
	OutlineParams* mappedOutlineParams_ = nullptr;

	// カメラから毎フレーム設定される実行時値（保存対象ではない）
	float nearPlane_ = 0.1f;
	float farPlane_  = 1000.0f;
};
}
