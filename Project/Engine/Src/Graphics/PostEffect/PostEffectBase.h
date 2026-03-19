#pragma once
#include <d3d12.h>
#include <string>
#include <wrl.h>
#include <memory>

#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Pipeline/PipelineStateManager.h"
#include "Graphics/RootSignature/RootSignatureManager.h"
#include "Graphics/Shader/ShaderCompiler.h"
#include "Graphics/Shader/ShaderReflectionBuilder.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"


namespace CoreEngine
{
class ShaderReflectionData;

class PostEffectBase {
public:
    virtual ~PostEffectBase() = default;
    virtual void Initialize(DirectXCommon* dxCommon);
    virtual void Draw(D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle);

    /// @brief ImGuiでパラメータを調整する関数
    virtual void DrawImGui() {}

    /// @brief 更新処理（デフォルトは空実装）
    /// @param deltaTime フレーム時間
    virtual void Update(float /*deltaTime*/) {}

    /// @brief エフェクトの有効/無効を設定
    /// @param enabled 有効にするかどうか
    void SetEnabled(bool enabled) { enabled_ = enabled; }

    /// @brief エフェクトが有効かどうかを取得
    /// @return 有効ならtrue
    bool IsEnabled() const { return enabled_; }

    /// @brief シェーダーリソース名からルートパラメータインデックスを取得
    int GetRootParamIndex(const std::string& resourceName) const;

protected:
    virtual const std::wstring& GetPixelShaderPath() const = 0;
    virtual std::string GetEffectName() const { return "PostEffect"; }
    virtual void BindOptionalCBVs(ID3D12GraphicsCommandList*/* commandList*/) { }

    /// @brief ルートシグネチャ構築前に呼ばれる設定フック
    /// @details サブクラスでオーバーライドすることで追加サンプラーなどを設定できる
    /// @param config 構築中のルートシグネチャ設定
    virtual void OnConfigureRootSignature(RootSignatureConfig& /*config*/) {}

protected:
    DirectXCommon* directXCommon_ = nullptr;

    Microsoft::WRL::ComPtr<IDxcBlob> fullscreenVertexShaderBlob_;
    Microsoft::WRL::ComPtr<IDxcBlob> pixelShaderBlob_;

    std::unique_ptr<RootSignatureManager> rootSignatureManager_;
    PipelineStateManager pipelineStateManager_;

    // シェーダーリフレクションデータ
    std::unique_ptr<ShaderReflectionData> reflectionData_;

    bool enabled_ = true; // デフォルトで有効
};
}
