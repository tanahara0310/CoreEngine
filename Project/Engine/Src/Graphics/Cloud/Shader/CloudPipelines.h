#pragma once

#include "Graphics/Pipeline/CustomShaderPipeline.h"
#include "Graphics/RootSignature/ShaderBinder.h"
#include "Graphics/Shader/ShaderBindingContract.h"

#include <array>
#include <cstddef>
#include <d3d12.h>

namespace CoreEngine
{
    /// @brief 雲のコンピュートパス識別子（CloudPipelines の添字）
    enum class CloudPass : size_t {
        BaseShapeNoise,
        DetailNoise,
        WeatherMap,
        NoiseMip3D,
        RayMarch,
        Composite,
        CubemapCapture,
        CloudShadowMap,
        GodRayMarch,
        GodRayComposite,
        Count
    };

    /// @brief PSO と、その PSO に対して解決済みのバインド契約の対
    struct CloudComputePass {
        CustomShaderPipeline pipeline{};
        BindingTable bindings{};

        /// @brief PSO とルートシグネチャを差し、このパス用のバインダーを返す
        ShaderBinder Begin(ID3D12GraphicsCommandList* cmdList) const;
    };

    /// @brief 雲の全コンピュートパイプラインの構築と保持
    /// @details ShaderCompiler は構築 1 回につき 1 個だけ作る。
    ///          シェーダーのパスと宣言表の対応は .cpp のパス表が単一情報源。
    class CloudPipelines {
    public:
        /// @brief ノイズ生成 3 本を構築する
        bool BuildNoisePasses(ID3D12Device* device);

        /// @brief レイマーチ・合成・キューブマップ焼き込みを構築する
        bool BuildRenderPasses(ID3D12Device* device);

        /// @brief ゴッドレイ 3 本を構築する
        bool BuildGodRayPasses(ID3D12Device* device);

        const CloudComputePass& operator[](CloudPass pass) const
        {
            return passes_[static_cast<size_t>(pass)];
        }

    private:
        /// @brief パス表の指定範囲を構築する
        bool BuildRange(ID3D12Device* device, CloudPass first, CloudPass lastInclusive);

        std::array<CloudComputePass, static_cast<size_t>(CloudPass::Count)> passes_{};
    };
}
