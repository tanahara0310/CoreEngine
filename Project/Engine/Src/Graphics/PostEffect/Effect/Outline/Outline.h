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

	/// @brief 画面サイズ定数バッファ構造体
	struct ScreenParams {
		uint32_t screenWidth  = 1280;
		uint32_t screenHeight = 720;
		float    pad[2]       = { 0.0f, 0.0f };
	};

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

	/// @brief カメラのクリップ距離を毎フレーム更新する（線形深度変換に使用）
	/// @param nearPlane ニアクリップ距離
	/// @param farPlane ファークリップ距離
	void SetCameraClipPlanes(float nearPlane, float farPlane);

protected:
	/// @brief 有効/無効は CVar "r.<Effect>.Enabled" が保持する
	CVar<bool>* GetEnabledCVar() const override;

	std::string  GetEffectName()        const override { return "Outline"; }
	std::wstring GetComputeShaderPath() const override { return L"Outline.CS.hlsl"; }
	void OnCreateConstantBuffers() override;

private:
	void UpdateScreenConstantBuffer(uint32_t width, uint32_t height);

private:
	Microsoft::WRL::ComPtr<ID3D12Resource> outlineParamsCB_;
	OutlineParams* mappedOutlineParams_ = nullptr;

	Microsoft::WRL::ComPtr<ID3D12Resource> screenParamsCB_;
	ScreenParams* mappedScreenParams_ = nullptr;

	// カメラから毎フレーム設定される実行時値（保存対象ではない）
	float nearPlane_ = 0.1f;
	float farPlane_  = 1000.0f;
};
}
