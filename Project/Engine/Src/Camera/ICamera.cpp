#include "pch.h"
#include "ICamera.h"
#include "CameraStructs.h"

namespace CoreEngine
{

// デフォルト実装: パラメータ取得
CameraParameters ICamera::GetParameters() const
{
    // デフォルトではデフォルトパラメータを返す
    return CameraParameters::Default();
}

// デフォルト実装: パラメータ設定（何もしない）
void ICamera::SetParameters(const CameraParameters& params)
{
    // デフォルトでは何もしない
    // 各カメラクラスでオーバーライドして実装する
    (void)params;  // 未使用警告を抑制
}

void ICamera::SetProjectionJitter(float ndcX, float ndcY)
{
    const Matrix4x4* baseProjection = GetUnjitteredProjectionMatrix();
    if (!baseProjection) {
        // ジッタ非対応のカメラ（2D など）。状態を残さず無効のままにする
        projectionJitterActive_ = false;
        projectionJitterX_ = 0.0f;
        projectionJitterY_ = 0.0f;
        return;
    }

    projectionJitterX_ = ndcX;
    projectionJitterY_ = ndcY;
    projectionJitterActive_ = (ndcX != 0.0f) || (ndcY != 0.0f);

    if (!projectionJitterActive_) {
        return;
    }

    // 行ベクトル規約（clip = v * P）。透視射影では clip.w にビュー空間 z が入るため、
    // m[2][0] / m[2][1] への加算は「clip.xy += ndc * clip.w」と等価になり、
    // 深度に関わらず一定のサブピクセルずれになる。
    jitteredProjectionMatrix_ = *baseProjection;
    jitteredProjectionMatrix_.m[2][0] += ndcX;
    jitteredProjectionMatrix_.m[2][1] += ndcY;
}

}
