#include "pch.h"
#include "PostEffectBase.h"
#include "Graphics/Shader/ShaderReflectionData.h"
#include "Graphics/PostEffect/Graph/PostEffectGraphBuilder.h"


namespace CoreEngine
{
    int PostEffectBase::GetRootParamIndex(const std::string& resourceName) const {
        if (!reflectionData_) {
            return -1;
        }
        return reflectionData_->GetRootParameterIndexByName(resourceName);
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
}
