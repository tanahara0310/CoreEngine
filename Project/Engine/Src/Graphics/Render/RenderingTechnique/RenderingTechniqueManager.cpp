#include "RenderingTechniqueManager.h"
#include "Graphics/Common/DirectXCommon.h"
#include "RenderingTechniqueNames.h"
#include "SSAO/SSAOTechnique.h"
#include "SSAO/SSAOBlurTechnique.h"
#include "Lighting/DeferredLightingTechnique.h"
#include <cassert>

#ifdef USE_IMGUI
#include "Utility/Debug/ImGui/ImguiManager.h"
#endif

namespace CoreEngine
{
    void RenderingTechniqueManager::Initialize(DirectXCommon* dxCommon)
    {
        assert(dxCommon);
        directXCommon_ = dxCommon;

        RegisterAllTechniques();
    }

    void RenderingTechniqueManager::RegisterAllTechniques()
    {
        // Deferred Lightingの登録（常時有効）
        RegisterTechnique<DeferredLightingTechnique>(RenderingTechniqueNames::DeferredLighting, true);

        // SSAO系の登録
        RegisterTechnique<SSAOTechnique>(RenderingTechniqueNames::SSAO, true);
        RegisterTechnique<SSAOBlurTechnique>(RenderingTechniqueNames::SSAOBlur, true);

        // 将来的な実装例:
        // RegisterTechnique<TAATechnique>(RenderingTechniqueNames::TAA, true);
    }

    bool RenderingTechniqueManager::HasTechnique(const std::string& name) const
    {
        return techniques_.find(name) != techniques_.end();
    }

    void RenderingTechniqueManager::Update(float deltaTime)
    {
        for (auto& [name, technique] : techniques_) {
            if (technique && technique->IsEnabled()) {
                technique->Update(deltaTime);
            }
        }
    }

    void RenderingTechniqueManager::OnResize(uint32_t width, uint32_t height)
    {
        for (auto& [name, technique] : techniques_) {
            if (technique) {
                technique->OnResize(width, height);
            }
        }
    }

    void RenderingTechniqueManager::DrawImGui()
    {
#ifdef USE_IMGUI
        for (auto& [name, technique] : techniques_) {
            if (technique) {
                // 常時有効な技術は無効化できないようにする
                if (technique->IsAlwaysEnabled()) {
                    bool enabled = technique->IsEnabled();
                    ImGui::Checkbox(name.c_str(), &enabled);
                    ImGui::SameLine();
                    ImGui::TextDisabled("(Always Enabled)");
                } else {
                    bool enabled = technique->IsEnabled();
                    if (ImGui::Checkbox(name.c_str(), &enabled)) {
                        technique->SetEnabled(enabled);
                    }
                }

                // パラメータ調整UI
                if (technique->IsEnabled()) {
                    ImGui::Indent();
                    technique->DrawImGui();
                    ImGui::Unindent();
                }
                ImGui::Separator();
            }
        }
#endif
    }
}
