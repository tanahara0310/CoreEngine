#pragma once

#include <d3d12.h>
#include <dxcapi.h>
#include <memory>
#include <string>
#include <wrl.h>

namespace CoreEngine
{
    class RootSignatureConfig;
    class RootSignatureManager;
    class ShaderCompiler;
    class ShaderReflectionBuilder;
    class ShaderReflectionData;
}

namespace CoreEngine::GraphicsPipelineUtil
{
    /// @brief コンパイル済みの頂点シェーダーとピクセルシェーダー
    struct ShaderPair {
        Microsoft::WRL::ComPtr<IDxcBlob> vs;
        Microsoft::WRL::ComPtr<IDxcBlob> ps;
    };

    /// @brief 頂点・ピクセルシェーダーをコンパイルし、2 本をまとめたリフレクションを作る
    /// @param reflectionName リフレクションに付ける名前
    /// @param reflection     作ったリフレクションの入れ先
    /// @note コンパイラとリフレクションビルダーは初期化してから渡すこと。コンパイルの失敗は assert で止める
    ShaderPair CompileShaders(
        ShaderCompiler& compiler,
        ShaderReflectionBuilder& reflectionBuilder,
        const std::wstring& vsPath,
        const std::wstring& psPath,
        const std::string& reflectionName,
        std::unique_ptr<ShaderReflectionData>& reflection);

    /// @brief リフレクションと設定からルートシグネチャを作る
    /// @param label 失敗したときの例外に入れる名前（"Failed to create <label> Root Signature: <理由>"。空なら名前を入れない）
    /// @throws std::runtime_error ルートシグネチャを作れなかったとき
    void BuildRootSignature(
        RootSignatureManager& rootSignature,
        ID3D12Device* device,
        const ShaderReflectionData& reflection,
        const RootSignatureConfig& config,
        const std::string& label);

} // namespace CoreEngine::GraphicsPipelineUtil
