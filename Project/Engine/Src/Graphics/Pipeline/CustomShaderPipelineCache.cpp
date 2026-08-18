#include "pch.h"
#include "CustomShaderPipelineCache.h"
#include "Graphics/Pipeline/CustomShaderPipeline.h"
#include "Graphics/Shader/ICustomShaderProvider.h"
#include "Utility/Logger/Logger.h"

namespace CoreEngine
{
    std::string CustomShaderPipelineCache::MakeKey(const ICustomShaderProvider& provider)
    {
        // PSO 構築に影響する入力を全てキーに含める。
        // BindCustomResources のような描画時バインドはパイプライン形状に影響しないため含めない。
        auto& logger = Logger::GetInstance();
        std::string key;
        key += logger.WideToUtf8(provider.GetVertexShaderPath());
        key += '|';
        key += logger.WideToUtf8(provider.GetPixelShaderPath());
        key += '|';
        key += logger.WideToUtf8(provider.GetComputeShaderPath());
        key += "|Cull";
        key += std::to_string(static_cast<int>(provider.GetCullMode()));
        key += provider.GetDepthWriteEnable() ? "|DW1" : "|DW0";
        key += provider.WritesMotionVector() ? "|MV1" : "|MV0";
        return key;
    }

    std::shared_ptr<CustomShaderPipeline> CustomShaderPipelineCache::GetOrBuild(
        ID3D12Device* device,
        ShaderCompiler& compiler,
        ShaderReflectionBuilder& reflectionBuilder,
        const ICustomShaderProvider& provider)
    {
        const std::string key = MakeKey(provider);

        auto it = cache_.find(key);
        if (it != cache_.end()) {
            return it->second;
        }

        auto pipeline = std::make_shared<CustomShaderPipeline>();
        if (!pipeline->Build(device, compiler, reflectionBuilder, provider)) {
            // 失敗はキャッシュしない（シェーダー修正後の再試行を許す）
            return nullptr;
        }

        cache_.emplace(key, pipeline);
        Logger::GetInstance().Logf(LogLevel::Info, LogCategory::Graphics,
            "CustomShaderPipelineCache: 新規構築 key={} (キャッシュ数={})",
            key, cache_.size());
        return pipeline;
    }

    void CustomShaderPipelineCache::Clear()
    {
        cache_.clear();
    }

} // namespace CoreEngine
