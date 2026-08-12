#pragma once

#include "../PostEffectComputeBase.h"
#include <wrl.h>
#include <d3d12.h>


namespace CoreEngine
{
/// @brief フィルムグレイン（CS方式・常時薄く乗せる）
/// @details 実写には必ず粒子かセンサーノイズがあり、それが無い画像は「作り物」に見える。
///          平坦な面に質感を与えるのが役目。
///
///          演出用の `Random` エフェクトとは別物として分けている。あちらは
///          グリッチ等で強くノイズを乗せる用途で、寿命も調整軸も違うため、
///          共有すると片方の既定値を変えたときにもう片方が壊れる。
///
///          パラメータは CVar（"r.FilmGrain.*"）が唯一の保持者。
///          設計: Docs/Engine/Graphics/PostProcess/PostEffect_Refactoring_Plan.md
class FilmGrain : public PostEffectComputeBase {
public:
    /// @brief グレインパラメータ構造体（GPU 定数バッファのレイアウト）
    /// @note 既定値の正は CVar の定義（FilmGrain.cpp）側。ここはレイアウト定義として持つ
    struct FilmGrainParams {
        float intensity           = 0.030f; ///< 全体強度
        float intensityShadows    = 1.0f;   ///< シャドウ帯の倍率
        float intensityMidtones   = 0.7f;   ///< 中間調帯の倍率
        float intensityHighlights = 0.2f;   ///< ハイライト帯の倍率

        float grainSize    = 1.0f;  ///< 粒の大きさ[px]
        float chromaAmount = 0.10f; ///< 色ノイズの割合
        float time         = 0.0f;  ///< 実行時値（PrepareFrame が積算する）
        float padding      = 0.0f;
    };

public:
    FilmGrain() = default;
    ~FilmGrain() = default;

    /// @brief CSエフェクト実行
    void Dispatch(
        D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE outputUavHandle,
        uint32_t width,
        uint32_t height) override;

    /// @brief ImGuiでパラメータを調整
    void DrawImGui() override;

    /// @brief 粒は記録媒体の性質なのでトーンカーブ通過後に乗せる
    PostEffectStage GetStage() const override { return PostEffectStage::PostTonemap; }

    /// @brief 粒を毎フレーム散らすための経過時間を積算する
    void PrepareFrame(const PostEffectFrameContext& ctx) override;

protected:
    /// @brief 有効/無効は CVar "r.<Effect>.Enabled" が保持する
    CVar<bool>* GetEnabledCVar() const override;

    std::string  GetEffectName()        const override { return "FilmGrain"; }
    std::wstring GetComputeShaderPath() const override { return L"FilmGrain.CS.hlsl"; }
    void OnCreateConstantBuffers() override;

private:
    void UpdateConstantBuffer();

private:
    Microsoft::WRL::ComPtr<ID3D12Resource> filmGrainParamsCB_;
    FilmGrainParams* mappedFilmGrainParams_ = nullptr;

    /// @brief 経過時間（保存対象ではない実行時値）
    float elapsedTime_ = 0.0f;
};
}
