#include "pch.h"
#include "PostEffectBase.h"
#include "Graphics/RHI/Resource/UploadRing.h"
#include "Graphics/Shader/ShaderReflectionData.h"
#include "Graphics/PostEffect/Graph/PostEffectGraphBuilder.h"
#ifdef CORE_EDITOR
#include "Editor/ImGui/ImGuiAll.h"
#include "Editor/ImGui/CVarPanel.h"
#endif


namespace CoreEngine
{
    int PostEffectBase::GetRootParamIndex(const std::string& resourceName) const {
        return FindRootParamIndex(reflectionData_, resourceName);
    }

    void PostEffectBase::SetResolvedExtraInput(const char* slot, D3D12_GPU_DESCRIPTOR_HANDLE srvHandle)
    {
        if (!slot) {
            return;
        }

        // 同じスロットへの再登録は上書きする（フレーム内で二重に積まない）
        for (auto& resolved : resolvedExtraInputs_) {
            if (resolved.slot == slot) {
                resolved.srv = srvHandle;
                return;
            }
        }
        resolvedExtraInputs_.push_back({ slot, srvHandle });
    }

    void PostEffectBase::BuildPasses(PostEffectGraphBuilder& builder)
    {
        // 既定は「入力1・出力1の単一パス」。従来の Dispatch / Draw をそのまま呼ぶので、
        // これを上書きしていないエフェクトは無改修で動き続ける。
        std::vector<PostEffectInputBinding> extraInputs;
        DeclareExtraInputs(extraInputs);

        std::vector<PostEffectResourceRef> reads;
        reads.reserve(extraInputs.size() + 1);
        reads.push_back(builder.Input());
        for (const PostEffectInputBinding& binding : extraInputs) {
            reads.push_back(builder.Read(binding.logicalName));
        }

        const bool isCompute = (GetExecutionType() == PostEffectExecutionType::Compute);
        auto record = [this, isCompute](const PostEffectPassContext& passContext) {
            const D3D12_GPU_DESCRIPTOR_HANDLE input =
                passContext.reads.empty() ? D3D12_GPU_DESCRIPTOR_HANDLE{} : passContext.reads[0];
            if (isCompute) {
                Dispatch(input, passContext.output, passContext.width, passContext.height);
            } else {
                Draw(input);
            }
        };

        if (isCompute) {
            builder.AddComputePass(GetEffectName(), reads, builder.ChainOutput(), std::move(record));
        } else {
            builder.AddGraphicsPass(GetEffectName(), reads, builder.ChainOutput(), std::move(record));
        }
    }

    D3D12_GPU_DESCRIPTOR_HANDLE PostEffectBase::GetExtraInput(const char* slot) const
    {
        if (slot) {
            for (const auto& resolved : resolvedExtraInputs_) {
                if (resolved.slot == slot) {
                    return resolved.srv;
                }
            }
        }
        return {};
    }

    D3D12_GPU_VIRTUAL_ADDRESS PostEffectBase::UploadConstantBytes(const void* data, uint32_t size) const
    {
        return graphicsCore_->GetUploadRing().AllocateConstants(data, size);
    }

    D3D12_GPU_VIRTUAL_ADDRESS PostEffectBase::UploadConstantFields(
        const void* src, const Cb::Field* fields, size_t count, uint32_t hlslSize) const
    {
        const UploadAllocation allocation = graphicsCore_->GetUploadRing().Allocate(hlslSize);
        if (!allocation.IsValid()) {
            return 0;
        }
        Cb::Upload(allocation.cpu, src, fields, count);
        return allocation.gpuAddress;
    }

#ifdef CORE_EDITOR
    void PostEffectBase::BeginEffectImGui(const char* id) const
    {
        ImGui::PushID(id);
        ImGui::Text("状態: %s", IsEnabled() ? "有効" : "無効");
    }

    bool PostEffectBase::DrawResetDefaultsImGui(std::string_view cvarPrefix) const
    {
        UI::Separator();
        if (!ImGui::Button("デフォルトに戻す")) {
            return false;
        }
        CVarUI::ResetTree(cvarPrefix);
        return true;
    }

    void PostEffectBase::EndEffectImGui(bool warnWhenDisabled) const
    {
        if (warnWhenDisabled && !IsEnabled()) {
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "注意: エフェクトは無効ですが、パラメータは調整可能です");
        }
        ImGui::PopID();
    }

    void PostEffectBase::DrawCVarEffectImGui(const char* id, std::string_view cvarPrefix, bool warnWhenDisabled) const
    {
        BeginEffectImGui(id);
        UI::Separator();
        CVarUI::DrawTree(cvarPrefix);
        DrawResetDefaultsImGui(cvarPrefix);
        EndEffectImGui(warnWhenDisabled);
    }
#endif // CORE_EDITOR
}
