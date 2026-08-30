#include "pch.h"
#include "CloudPipelines.h"

#include "Graphics/Cloud/Shader/CloudBindings.h"
#include "Graphics/Shader/ICustomShaderProvider.h"
#include "Graphics/Shader/ShaderCompiler.h"
#include "Graphics/Shader/ShaderReflectionBuilder.h"
#include "Graphics/Shader/ShaderReflectionData.h"
#include "Utility/Logger/Logger.h"

#include <exception>
#include <string>
#include <utility>

namespace CoreEngine
{
    namespace
    {
        /// @brief パスを実行時に受け取る CS 専用のシェーダ供給
        class ComputeShaderProvider final : public ICustomShaderProvider {
        public:
            explicit ComputeShaderProvider(const wchar_t* path) : path_(path) {}
            std::wstring GetComputeShaderPath() const override { return path_; }

        private:
            std::wstring path_;
        };

        /// @brief 1 パス分の定義（シェーダーのパスと宣言表の対応の単一情報源）
        struct CloudPassDesc {
            const wchar_t* shaderPath;
            const ShaderBindingDecl* decls;
            size_t declCount;
            const char* name;
        };

        template <size_t N>
        constexpr CloudPassDesc MakeDesc(const wchar_t* path, const ShaderBindingDecl (&decls)[N],
                                         const char* name)
        {
            return { path, decls, N, name };
        }

        const CloudPassDesc kPassTable[static_cast<size_t>(CloudPass::Count)] = {
            MakeDesc(L"CloudBaseShapeNoise.CS.hlsl", CloudNoiseBind::kDecls,           "BaseShapeNoise"),
            MakeDesc(L"CloudDetailNoise.CS.hlsl",    CloudNoiseBind::kDecls,           "DetailNoise"),
            MakeDesc(L"CloudWeatherMap.CS.hlsl",     CloudNoiseBind::kDecls,           "WeatherMap"),
            MakeDesc(L"CloudNoiseMip3D.CS.hlsl",     CloudNoiseMipBind::kDecls,        "NoiseMip3D"),
            MakeDesc(L"CloudRayMarch.CS.hlsl",       CloudRayMarchBind::kDecls,        "RayMarch"),
            MakeDesc(L"CloudComposite.CS.hlsl",      CloudCompositeBind::kDecls,       "Composite"),
            MakeDesc(L"CloudCubemapCapture.CS.hlsl", CloudCubemapCaptureBind::kDecls,  "CloudCubemapCapture"),
            MakeDesc(L"CloudShadowMap.CS.hlsl",      CloudShadowMapBind::kDecls,       "CloudShadowMap"),
            MakeDesc(L"GodRayMarch.CS.hlsl",         GodRayMarchBind::kDecls,          "GodRayMarch"),
            MakeDesc(L"GodRayComposite.CS.hlsl",     GodRayCompositeBind::kDecls,      "GodRayComposite"),
        };
    }

    ShaderBinder CloudComputePass::Begin(ID3D12GraphicsCommandList* cmdList) const
    {
        cmdList->SetPipelineState(pipeline.GetComputePSO());
        cmdList->SetComputeRootSignature(pipeline.GetComputeRootSignature());
        return ShaderBinder(cmdList, ShaderBinder::Pipeline::Compute);
    }

    bool CloudPipelines::BuildRange(ID3D12Device* device, CloudPass first, CloudPass lastInclusive)
    {
        if (!device) {
            return false;
        }

        // DXC は 1 回の構築につき 1 個で足りる
        ShaderCompiler shaderCompiler;
        shaderCompiler.Initialize();

        ShaderReflectionBuilder reflectionBuilder;
        reflectionBuilder.Initialize(shaderCompiler.GetDxcUtils());

        for (size_t i = static_cast<size_t>(first); i <= static_cast<size_t>(lastInclusive); ++i) {
            const CloudPassDesc& desc = kPassTable[i];
            CloudComputePass& pass = passes_[i];

            const ComputeShaderProvider provider(desc.shaderPath);
            if (!pass.pipeline.Build(device, shaderCompiler, reflectionBuilder, provider)
                || !pass.pipeline.HasComputePSO()) {
                Logger::GetInstance().Warnf(LogCategory::Graphics,
                    "CloudPipelines: {} コンピュートパイプラインの構築に失敗", desc.name);
                return false;
            }

            const ShaderReflectionData* reflection = pass.pipeline.GetComputeReflection();
            if (!reflection) {
                Logger::GetInstance().Warnf(LogCategory::Graphics,
                    "CloudPipelines: {} のリフレクションを取得できません", desc.name);
                return false;
            }

            try {
                pass.bindings = BindingTable::Resolve(
                    *reflection, desc.decls, desc.declCount, desc.name);
            }
            catch (const std::exception&) {
                // 違反の内訳は BindingTable::Resolve が error ログへ出している
                return false;
            }
        }
        return true;
    }

    bool CloudPipelines::BuildNoisePasses(ID3D12Device* device)
    {
        return BuildRange(device, CloudPass::BaseShapeNoise, CloudPass::NoiseMip3D);
    }

    bool CloudPipelines::BuildRenderPasses(ID3D12Device* device)
    {
        return BuildRange(device, CloudPass::RayMarch, CloudPass::CubemapCapture);
    }

    bool CloudPipelines::BuildGodRayPasses(ID3D12Device* device)
    {
        return BuildRange(device, CloudPass::CloudShadowMap, CloudPass::GodRayComposite);
    }
}
