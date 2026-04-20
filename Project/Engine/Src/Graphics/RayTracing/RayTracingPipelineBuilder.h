#pragma once

#include <d3d12.h>
#include <dxcapi.h>
#include <wrl.h>
#include <vector>

namespace CoreEngine
{
    /// @brief DXRヒットグループの設定
    struct DXRHitGroupDesc {
        const wchar_t* name = nullptr; ///< ヒットグループ名（HLSL export 名）
        const wchar_t* closestHitShader = nullptr; ///< Closest Hit シェーダー名
        const wchar_t* anyHitShader = nullptr; ///< Any Hit シェーダー名（省略可）
        const wchar_t* intersectionShader = nullptr; ///< Intersection シェーダー名（省略可）
        D3D12_HIT_GROUP_TYPE type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
    };

    /// @brief DXR レイトレーシングパイプライン（State Object）ビルダー
    /// @details DXIL Library / Hit Group / Shader Config / Global Root Signature / Pipeline Config
    ///          の各サブオブジェクトを宣言的に追加し Build() で ID3D12StateObject を生成する。
    ///
    /// @code
    ///   RayTracingPipelineBuilder builder;
    ///   builder.SetDXILLibrary(shaderBlob)
    ///          .AddHitGroup({ L"HitGroup", L"ClosestHit" })
    ///          .SetShaderConfig(sizeof(MyPayload))
    ///          .SetGlobalRootSignature(rootSig)
    ///          .SetMaxRecursionDepth(1);
    ///   builder.Build(device, stateObject, stateObjectProps);
    /// @endcode
    class RayTracingPipelineBuilder {
    public:
        /// @brief DXILライブラリ（コンパイル済みシェーダーバイトコード）を設定する
        RayTracingPipelineBuilder& SetDXILLibrary(IDxcBlob* shaderBlob);

        /// @brief ヒットグループを追加する
        RayTracingPipelineBuilder& AddHitGroup(const DXRHitGroupDesc& desc);

        /// @brief ペイロードサイズと交差属性サイズを設定する
        /// @param maxPayloadSizeInBytes    ペイロード最大バイト数
        /// @param maxAttributeSizeInBytes  交差属性最大バイト数（デフォルト: float2 = 8 bytes）
        RayTracingPipelineBuilder& SetShaderConfig(
            UINT maxPayloadSizeInBytes,
            UINT maxAttributeSizeInBytes = sizeof(float) * 2);

        /// @brief グローバルルートシグネチャを設定する
        RayTracingPipelineBuilder& SetGlobalRootSignature(ID3D12RootSignature* rootSignature);

        /// @brief TraceRay の最大再帰深度を設定する（デフォルト: 1）
        RayTracingPipelineBuilder& SetMaxRecursionDepth(UINT depth);

        /// @brief State Object を構築する
        /// @param device          D3D12 デバイス（内部で ID3D12Device5 へ QI する）
        /// @param outStateObject  構築された State Object
        /// @param outProperties   State Object Properties（ShaderIdentifier 取得用）
        /// @return 成功した場合 true
        bool Build(
            ID3D12Device* device,
            Microsoft::WRL::ComPtr<ID3D12StateObject>& outStateObject,
            Microsoft::WRL::ComPtr<ID3D12StateObjectProperties>& outProperties);

    private:
        IDxcBlob* shaderBlob_ = nullptr;
        ID3D12RootSignature* globalRootSig_ = nullptr;
        UINT maxPayloadSize_ = 0;
        UINT maxAttributeSize_ = sizeof(float) * 2;
        UINT maxRecursionDepth_ = 1;

        std::vector<DXRHitGroupDesc> hitGroups_;
    };
}
