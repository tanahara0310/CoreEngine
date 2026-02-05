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

}
