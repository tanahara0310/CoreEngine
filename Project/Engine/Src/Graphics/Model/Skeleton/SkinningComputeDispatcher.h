#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <memory>
#include <string>

#include "Graphics/Shader/ShaderCompiler.h"
#include "Graphics/Shader/ShaderReflectionBuilder.h"
#include "Graphics/RootSignature/RootSignatureManager.h"

namespace CoreEngine {
    class ShaderReflectionData;
    struct SkinCluster;
}

namespace CoreEngine
{
    /// @brief スキニング計算をComputeShaderで実行するディスパッチャー
    /// @details 頂点シェーダーで行っていた4影響ボーン線形ブレンドスキニングをCSへ移し、
    ///          Forward/GBuffer/Shadowの各パスで同一モデルを描画する際に
    ///          スキニング計算をフレームに1回だけ実行できるようにする。
    class SkinningComputeDispatcher {
    public:
        /// @brief CSシェーダーコンパイル・リフレクション・RootSignature・PSOを構築
        /// @param device デバイス
        void Initialize(ID3D12Device* device);

        /// @brief スキニング計算を実行する
        /// @param cmdList コマンドリスト
        /// @param srvHeap CS実行前にバインドするSRV/UAV用ディスクリプタヒープ
        /// @param skinCluster 対象のスキンクラスター（元頂点SRV・Influence SRV・Palette SRV・出力UAVを保持）
        /// @param vertexCount 頂点数
        void Dispatch(
            ID3D12GraphicsCommandList* cmdList,
            ID3D12DescriptorHeap* srvHeap,
            SkinCluster& skinCluster,
            UINT vertexCount);

    private:
        std::unique_ptr<ShaderCompiler> shaderCompiler_ = std::make_unique<ShaderCompiler>();
        std::unique_ptr<ShaderReflectionBuilder> reflectionBuilder_ = std::make_unique<ShaderReflectionBuilder>();
        std::unique_ptr<RootSignatureManager> rootSignatureMg_ = std::make_unique<RootSignatureManager>();
        std::unique_ptr<ShaderReflectionData> reflectionData_;

        Microsoft::WRL::ComPtr<ID3D12PipelineState> computePso_;

        // キャッシュ済みルートパラメータインデックス
        int sourceVerticesIdx_ = -1;
        int influencesIdx_ = -1;
        int matrixPaletteIdx_ = -1;
        int outputVerticesIdx_ = -1;
        int skinningParamsIdx_ = -1;
    };
}
