#include "pch.h"
#include "RootSignatureManager.h"
#include "Graphics/Shader/ShaderReflectionData.h"

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
                lastBuildResult_.resourceToRootSlot);
        }

        return lastBuildResult_;
    }

    RootSlot RootSignatureManager::GetRootSlot(const std::string& resourceName) const {
        auto it = lastBuildResult_.resourceToRootSlot.find(resourceName);
        if (it != lastBuildResult_.resourceToRootSlot.end()) {
            return it->second;
        }
        return RootSlot{};  // kind == None
    }

    int RootSignatureManager::GetRootParameterIndex(const std::string& resourceName) const {
        const RootSlot slot = GetRootSlot(resourceName);
        return slot.IsValid() ? static_cast<int>(slot.index) : -1;
    }

    void RootSignatureManager::Clear() {
        rootSignature_.Reset();
        lastBuildResult_ = RootSignatureBuildResult{};
    }
}
