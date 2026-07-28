#include "pch.h"
#include "RenderingTechniqueSettingsSection.h"

#include "EngineSystem/EngineSystem.h"
#include "Graphics/Render/RenderingTechnique/RenderingTechniqueManager.h"
#include "Graphics/Render/RenderingTechnique/RenderingTechniqueNames.h"
#include "Graphics/Render/RenderingTechnique/RenderingTechniqueBase.h"
#include "Graphics/Render/RenderingTechnique/SSAO/SSAOTechnique.h"
#include "Graphics/Render/RenderingTechnique/SSAO/SSAOBlurTechnique.h"
#include "Utility/JsonManager/JsonManager.h"

namespace CoreEngine
{
    namespace
    {
        RenderingTechniqueManager* GetTechniqueManager(EngineSystem* engine)
        {
            return engine ? engine->GetComponent<RenderingTechniqueManager>() : nullptr;
        }

        // 保存対象の技術名（登録技術が増えたらここに追記する）
        constexpr const char* kTechniqueNames[] = {
            RenderingTechniqueNames::DeferredLighting,
            RenderingTechniqueNames::WaterCaustics,
            RenderingTechniqueNames::SSAO,
            RenderingTechniqueNames::SSAOBlur,
        };
    }

    void RenderingTechniqueSettingsSection::Serialize(json& out) const
    {
        auto* manager = GetTechniqueManager(engine_);
        if (!manager) {
            return;
        }

        // 有効/無効状態
        json enabledStates;
        for (const char* name : kTechniqueNames) {
            if (auto* technique = manager->GetTechnique<RenderingTechniqueBase>(name)) {
                enabledStates[name] = technique->IsEnabled();
            }
        }
        out["enabledStates"] = enabledStates;

        // SSAO のチューニング値（行列・画面サイズは毎フレーム自動設定のため保存しない）
        if (auto* ssao = manager->GetTechnique<SSAOTechnique>(RenderingTechniqueNames::SSAO)) {
            const auto& params = ssao->GetParams();
            json ssaoJson;
            ssaoJson["radius"] = params.radius;
            ssaoJson["bias"] = params.bias;
            ssaoJson["intensity"] = params.intensity;
            ssaoJson["sampleCount"] = params.sampleCount;
            ssaoJson["power"] = params.power;
            out["ssao"] = ssaoJson;
        }

        // SSAO Blur のチューニング値
        if (auto* ssaoBlur = manager->GetTechnique<SSAOBlurTechnique>(RenderingTechniqueNames::SSAOBlur)) {
            json blurJson;
            blurJson["depthThreshold"] = ssaoBlur->GetParams().depthThreshold;
            out["ssaoBlur"] = blurJson;
        }

        // WaterCaustics のパラメータは Water セクション（WaterEditorFacade）所有のため保存しない
    }

    void RenderingTechniqueSettingsSection::Deserialize(const json& in)
    {
        auto* manager = GetTechniqueManager(engine_);
        if (!manager) {
            return;
        }

        // 有効/無効状態（欠損キーは現在値を維持。常時有効の技術は変更しない）
        if (in.contains("enabledStates")) {
            const auto& enabledStates = in["enabledStates"];
            for (const char* name : kTechniqueNames) {
                auto* technique = manager->GetTechnique<RenderingTechniqueBase>(name);
                if (!technique || technique->IsAlwaysEnabled()) {
                    continue;
                }
                technique->SetEnabled(
                    JsonManager::SafeGet(enabledStates, name, technique->IsEnabled()));
            }
        }

        // SSAO（現在値を起点に、保存対象のフィールドだけ上書きする）
        if (in.contains("ssao")) {
            const auto& ssaoJson = in["ssao"];
            if (auto* ssao = manager->GetTechnique<SSAOTechnique>(RenderingTechniqueNames::SSAO)) {
                SSAOTechnique::SSAOParams params = ssao->GetParams();
                params.radius = JsonManager::SafeGet(ssaoJson, "radius", params.radius);
                params.bias = JsonManager::SafeGet(ssaoJson, "bias", params.bias);
                params.intensity = JsonManager::SafeGet(ssaoJson, "intensity", params.intensity);
                params.sampleCount = JsonManager::SafeGet(ssaoJson, "sampleCount", params.sampleCount);
                params.power = JsonManager::SafeGet(ssaoJson, "power", params.power);
                ssao->SetParams(params);
            }
        }

        // SSAO Blur
        if (in.contains("ssaoBlur")) {
            const auto& blurJson = in["ssaoBlur"];
            if (auto* ssaoBlur = manager->GetTechnique<SSAOBlurTechnique>(RenderingTechniqueNames::SSAOBlur)) {
                SSAOBlurTechnique::SSAOBlurParams params = ssaoBlur->GetParams();
                params.depthThreshold = JsonManager::SafeGet(blurJson, "depthThreshold", params.depthThreshold);
                ssaoBlur->SetParams(params);
            }
        }
    }
}
