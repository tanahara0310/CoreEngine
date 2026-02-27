#include "RootSignatureManager.h"
#include "Engine/Graphics/Shader/ShaderReflectionData.h"

namespace CoreEngine
{
    RootSignatureBuildResult RootSignatureManager::Build(
        ID3D12Device* device,
        const ShaderReflectionData& reflectionData,
        const RootSignatureConfig& config) {

        // 既存の設定をクリア
        Clear();

        // ビルダーを使用して構築
        lastBuildResult_ = builder_->Build(device, reflectionData, config);

        if (lastBuildResult_.success) {
            rootSignature_ = lastBuildResult_.rootSignature;
            
            // ShaderReflectionDataにマッピングを設定
            const_cast<ShaderReflectionData&>(reflectionData).SetRootParameterMapping(
                lastBuildResult_.resourceToRootParamIndex);
        }

        return lastBuildResult_;
    }

    int RootSignatureManager::GetRootParameterIndex(const std::string& resourceName) const {
        auto it = lastBuildResult_.resourceToRootParamIndex.find(resourceName);
        if (it != lastBuildResult_.resourceToRootParamIndex.end()) {
            return static_cast<int>(it->second);
        }
        return -1;
    }

    void RootSignatureManager::Clear() {
        rootSignature_.Reset();
        lastBuildResult_ = RootSignatureBuildResult{};
    }
}
