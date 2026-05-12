#pragma once
#include <d3d12.h>

namespace CoreEngine
{
class DirectXCommon;
class PostEffectBase;
class Render;
class RenderTarget;

/// @brief ポストエフェクト用 Ping-Pong バッファ管理クラス
/// @details Offscreen0/1 を交互に入出力として使い、複数エフェクトを連鎖適用する
class PingPongBuffer {
public:
    PingPongBuffer(DirectXCommon* dxCommon, Render* render);

    /// @brief エフェクトを適用し、バッファを切り替える
    /// @param effect 適用するエフェクト
    /// @return 適用されたかどうか
    bool ApplyEffect(PostEffectBase* effect);

    /// @brief 現在の出力 SRV ハンドルを取得
    D3D12_GPU_DESCRIPTOR_HANDLE GetCurrentOutput() const;

    /// @brief 入力をリセット
    void Reset(D3D12_GPU_DESCRIPTOR_HANDLE input);

private:
    DirectXCommon* dxCommon_;
    Render* render_;
    D3D12_GPU_DESCRIPTOR_HANDLE currentInput_;
    int currentOutputIndex_;

    RenderTarget* GetRenderTarget(int index) const;
};
}
