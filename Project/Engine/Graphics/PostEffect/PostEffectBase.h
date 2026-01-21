#pragma once
#include <d3d12.h>
#include <string>
#include <wrl.h>

#include "Engine/Graphics/Common/DirectXCommon.h"
#include "Engine/Graphics/PipelineStateManager.h"
#include "Engine/Graphics/RootSignatureManager.h"
#include "Engine/Graphics/Shader/ShaderCompiler.h"


namespace CoreEngine
{
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

protected:
    virtual const std::wstring& GetPixelShaderPath() const = 0;
    virtual void BindOptionalCBVs(ID3D12GraphicsCommandList*/* commandList*/) { }

protected:
    DirectXCommon* directXCommon_ = nullptr;

    Microsoft::WRL::ComPtr<IDxcBlob> fullscreenVertexShaderBlob_;
    Microsoft::WRL::ComPtr<IDxcBlob> pixelShaderBlob_;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    PipelineStateManager pipelineStateManager_;

    bool enabled_ = true; // デフォルトで有効
};
}
