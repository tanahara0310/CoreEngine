#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <memory>

#include "Graphics/Render/RenderTarget/OffscreenRenderTarget.h"
#include "Math/Vector/Vector4.h"
#include "Math/MathCore.h"

namespace CoreEngine
{
    class DirectXCommon;
    class ICamera;
    class RenderManager;
}

/// @brief 平面反射パスの管理クラス（Step 4: Planar Reflection）
///
/// 水面を鏡面として、水面より上のシーンを専用の RTT に描画する。
/// 生成した反射テクスチャは Water.PS.hlsl で Fresnel ブレンドに使用する。
class WaterReflectionPass
{
public:
    /// @brief 初期化
    /// @param dxCommon  DirectXCommon（オフスクリーン RTT 確保に使用）
    /// @param offscreenIndex オフスクリーン管理配列のインデックス（他のパスと衝突しない値を指定）
    void Initialize(CoreEngine::DirectXCommon* dxCommon, int offscreenIndex = 2);

    /// @brief 毎フレーム反射パスを描画する
    /// @param cmdList     コマンドリスト
    /// @param renderManager  描画キューが積まれている RenderManager
    /// @param mainCamera  通常描画で使用しているカメラ（これを水面に対して反転する）
    /// @param waterHeight 水面の Y 座標（ワールド空間）
    void Render(
        ID3D12GraphicsCommandList* cmdList,
        CoreEngine::RenderManager* renderManager,
        CoreEngine::ICamera* mainCamera,
        float waterHeight);

    /// @brief 反射テクスチャの SRV ハンドルを取得する
    D3D12_GPU_DESCRIPTOR_HANDLE GetReflectionSRV() const;

    /// @brief クリップ平面を取得する（現在の水面高さに対応した値）
    const CoreEngine::Vector4& GetClipPlane() const { return clipPlane_; }

    /// @brief クリップ平面を有効にするか（Water.VS.hlsl の SV_ClipDistance0 制御用）
    bool IsClipEnabled() const { return clipEnabled_; }
    void SetClipEnabled(bool enable) { clipEnabled_ = enable; }

private:
    /// @brief カメラ位置・向きを水面平面に対して反転したビュー行列を計算する
    /// @param mainCamera 元のカメラ
    /// @param waterHeight 水面の Y 座標
    /// @return 鏡像ビュー行列
    CoreEngine::Matrix4x4 CalcReflectedViewMatrix(
        CoreEngine::ICamera* mainCamera,
        float waterHeight) const;

    CoreEngine::DirectXCommon* dxCommon_ = nullptr;
    std::unique_ptr<CoreEngine::OffscreenRenderTarget> reflectionRT_;

    /// @brief クリップ平面パラメータ（HLSL 側 SV_ClipDistance0 に渡す: dot(worldPos, clipPlane) > 0 なら描画）
    CoreEngine::Vector4 clipPlane_ = { 0.0f, 1.0f, 0.0f, 0.0f };
    bool clipEnabled_ = false;
};
