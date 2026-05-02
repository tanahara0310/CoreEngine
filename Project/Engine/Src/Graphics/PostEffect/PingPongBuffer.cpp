#include "PingPongBuffer.h"

#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Render/Render.h"
#include "Graphics/Render/RenderTarget/RenderTarget.h"
#include "Graphics/Render/RenderTarget/RenderTargetNames.h"
#include "Graphics/PostEffect/PostEffectBase.h"

namespace CoreEngine
{
PingPongBuffer::PingPongBuffer(DirectXCommon* dxCommon, Render* render)
    : dxCommon_(dxCommon)
    , render_(render)
    , currentInput_()
    , currentOutputIndex_(1)
{
}

void PingPongBuffer::Reset(D3D12_GPU_DESCRIPTOR_HANDLE input)
{
    currentInput_ = input;
    currentOutputIndex_ = 1;
}

bool PingPongBuffer::ApplyEffect(PostEffectBase* effect)
{
    if (!effect || !effect->IsEnabled()) {
        return false;
    }

    auto* cmdList = dxCommon_->GetCommandList();
    auto* renderTarget = GetRenderTarget(currentOutputIndex_);
    if (!renderTarget) {
        return false;
    }

    // エフェクトを現在の出力バッファに描画
    renderTarget->Begin(cmdList);
    effect->Draw(currentInput_);
    renderTarget->End(cmdList);

    // 今書き込んだバッファが次の入力になる
    currentInput_ = renderTarget->GetSRVHandle();

    // 次回の出力先を切り替え（ping-pong）
    currentOutputIndex_ = (currentOutputIndex_ == 0) ? 1 : 0;

    return true;
}

D3D12_GPU_DESCRIPTOR_HANDLE PingPongBuffer::GetCurrentOutput() const
{
    return currentInput_;
}

RenderTarget* PingPongBuffer::GetRenderTarget(int index) const
{
    if (index == 0) {
        return render_->GetRenderTarget(RenderTargetNames::Offscreen0);
    } else if (index == 1) {
        return render_->GetRenderTarget(RenderTargetNames::Offscreen1);
    }
    return nullptr;
}
}
