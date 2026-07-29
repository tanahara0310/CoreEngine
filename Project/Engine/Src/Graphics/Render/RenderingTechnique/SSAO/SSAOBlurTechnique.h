#pragma once
#include "../RenderingTechniqueBase.h"
#include <wrl.h>
#include <d3d12.h>
#include <string>

namespace CoreEngine
{
/// @brief SSAOブラー レンダリング技術
/// @details SSAOの出力にバイラテラルブラーをかけてノイズを除去
class SSAOBlurTechnique : public RenderingTechniqueBase {
public:
    struct SSAOBlurParams {
        float invViewProjMatrix[16] = {}; // WorldPosition ターゲット廃止に伴う深度復元用
        float screenWidth   = 1280.0f;
        float screenHeight  = 720.0f;
        float depthThreshold = 0.5f;
        float _pad0          = 0.0f;
    };

public:
    SSAOBlurTechnique() = default;
    ~SSAOBlurTechnique() = default;

    void Initialize(DirectXCommon* dxCommon) override;
    void Execute(const RenderContext& context, D3D12_GPU_DESCRIPTOR_HANDLE& outputSrvHandle) override;
    void OnResize(uint32_t width, uint32_t height) override;

    const SSAOBlurParams& GetParams() const { return params_; }
    void SetParams(const SSAOBlurParams& params) { params_ = params; }

    /// @brief 入力レンダーターゲット名を設定する
    /// @details テンポラル蓄積の有無で入力が変わるため、SSAOPass が毎フレーム設定する
    void SetInputTargetName(const std::string& name) { inputTargetName_ = name; }

protected:
    std::string GetTechniqueName() const override { return "SSAOBlur"; }
    const std::wstring& GetPixelShaderPath() const override;

private:
    SSAOBlurParams params_;
    // invViewProj がジッタで毎フレーム変わるため、フレームオーバーラップ対応のリングで運ぶ
    FrameRingConstantBuffer cbRing_;
    std::string inputTargetName_; ///< 空ならデフォルト（RenderTargetNames::SSAOBuffer）
};
}
