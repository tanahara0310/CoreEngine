#pragma once

#include "ShaderReflectionData.h"
#include <d3d12.h>
#include <d3d12shader.h>
#include <dxcapi.h>
#include <wrl.h>
#include <memory>

namespace CoreEngine
{
    /// @brief シェーダーリフレクションを実行してShaderReflectionDataを構築するクラス
    class ShaderReflectionBuilder {
    public:
        ShaderReflectionBuilder() = default;
        ~ShaderReflectionBuilder() = default;

        /// @brief 初期化（DXCユーティリティの設定）
        /// @param dxcUtils DXCユーティリティインターフェース
        void Initialize(IDxcUtils* dxcUtils);

        /// @brief 頂点シェーダーとピクセルシェーダーからリフレクションデータを構築
        /// @param vertexShaderBlob 頂点シェーダーのコンパイル済みBlob
        /// @param pixelShaderBlob ピクセルシェーダーのコンパイル済みBlob
        /// @param shaderName シェーダー識別名（ログ出力用）
        /// @return リフレクション結果のデータ
        std::unique_ptr<ShaderReflectionData> BuildFromShaders(
            IDxcBlob* vertexShaderBlob,
            IDxcBlob* pixelShaderBlob,
            const std::string& shaderName = "");

        /// @brief 単一のシェーダーからリフレクションデータを構築
        /// @param shaderBlob シェーダーのコンパイル済みBlob
        /// @param visibility シェーダーの可視性（VS, PS, ALL）
        /// @param shaderName シェーダー識別名（ログ出力用）
        /// @return リフレクション結果のデータ
        std::unique_ptr<ShaderReflectionData> BuildFromShader(
            IDxcBlob* shaderBlob,
            D3D12_SHADER_VISIBILITY visibility,
            const std::string& shaderName = "");

        /// @brief コンピュートシェーダーからリフレクションデータを構築
        /// @param computeShaderBlob コンピュートシェーダーのコンパイル済みBlob
        /// @param shaderName シェーダー識別名（ログ出力用）
        /// @return リフレクション結果のデータ
        std::unique_ptr<ShaderReflectionData> BuildFromComputeShader(
            IDxcBlob* computeShaderBlob,
            const std::string& shaderName = "");

    private:
        /// @brief シェーダーリフレクションを実行
        /// @param shaderBlob シェーダーのコンパイル済みBlob
        /// @param visibility シェーダーの可視性
        /// @param outData 出力先のリフレクションデータ
        void ReflectShader(
            IDxcBlob* shaderBlob,
            D3D12_SHADER_VISIBILITY visibility,
            ShaderReflectionData& outData);

        /// @brief 定数バッファをリフレクション
        /// @param reflection シェーダーリフレクションインターフェース
        /// @param visibility シェーダーの可視性
        /// @param outData 出力先のリフレクションデータ
        void ReflectConstantBuffers(
            ID3D12ShaderReflection* reflection,
            D3D12_SHADER_VISIBILITY visibility,
            ShaderReflectionData& outData);

        /// @brief バウンドリソースをリフレクション（SRV, UAV, Sampler）
        /// @param reflection シェーダーリフレクションインターフェース
        /// @param visibility シェーダーの可視性
        /// @param outData 出力先のリフレクションデータ
        void ReflectBoundResources(
            ID3D12ShaderReflection* reflection,
            D3D12_SHADER_VISIBILITY visibility,
            ShaderReflectionData& outData);

        /// @brief 入力レイアウトをリフレクション（頂点シェーダーのみ）
        /// @param reflection シェーダーリフレクションインターフェース
        /// @param outData 出力先のリフレクションデータ
        void ReflectInputLayout(
            ID3D12ShaderReflection* reflection,
            ShaderReflectionData& outData);

        /// @brief D3D11のシェーダー入力型をD3D12形式に変換
        /// @param componentType D3D11のコンポーネント型
        /// @param mask 使用されているコンポーネントのマスク
        /// @return DXGI_FORMAT
        DXGI_FORMAT GetDXGIFormat(
            D3D_REGISTER_COMPONENT_TYPE componentType,
            BYTE mask);

        // DXCユーティリティ（外部から注入）
        IDxcUtils* dxcUtils_ = nullptr;
    };
}
