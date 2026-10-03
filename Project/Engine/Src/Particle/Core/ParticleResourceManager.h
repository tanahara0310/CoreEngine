#pragma once

#include <d3d12.h>
#include <cstdint>
#include "Graphics/RHI/Resource/PerFrameStructuredBuffer.h"
#include "Math/MathCore.h"

namespace CoreEngine
{
// 前方宣言
class GraphicsCore;

/// @brief GPU送信用パーティクルデータ
struct ParticleForGPU {
    Matrix4x4 WVP;
    Matrix4x4 World;
    Vector4 color;
};

/// @brief パーティクルシステムのリソース管理クラス
/// @details 粒ごとの描画データ（インスタンシングの StructuredBuffer）を持ち、
///          GPU へは SRV を引いたフレームの置き場へ写す
class ParticleResourceManager {
public:
    /// @brief 初期化
    /// @param dxCommon GraphicsCore
    /// @param maxInstances 最大インスタンス数
    void Initialize(GraphicsCore* dxCommon, uint32_t maxInstances);

    /// @brief インスタンシングデータの書き込み先を取得（最大インスタンス数ぶん）
    /// @note 書いた数は SetInstanceCount で決める。GPU へはその数だけ写す
    ParticleForGPU* GetInstancingData() { return instancing_.Write(instancing_.Capacity()).data(); }

    /// @brief 書き込んだインスタンスの数を決める
    void SetInstanceCount(uint32_t count) { instancing_.SetCount(count); }

    /// @brief SRVのGPUハンドルを取得（そのフレームの記録中だけ今の値を保つ）
    /// @return SRVのGPUディスクリプタハンドル
    D3D12_GPU_DESCRIPTOR_HANDLE GetSrvHandleGPU() const { return instancing_.Srv(); }

private:
    PerFrameStructuredBuffer<ParticleForGPU> instancing_;
};

} // namespace CoreEngine
