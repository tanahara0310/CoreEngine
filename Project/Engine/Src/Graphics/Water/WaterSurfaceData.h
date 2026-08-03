#pragma once

#include "Graphics/Water/Surface/WaterSurfaceTypes.h"

namespace CoreEngine
{
    static constexpr uint32_t kMaxWaterSurfaceWaveCount = kMaxWaterWaveCount;
    static constexpr uint32_t kWaterSurfaceModelTypeGerstner = 0;
    static constexpr uint32_t kWaterSurfaceModelTypeFFTOcean = 1;

    // 波 1 本分のパラメータは WaveParams（WaterSurfaceTypes.h）に一本化した。
    // 以前はここに同一レイアウト 32B の WaterWaveParam が重複定義されていた。
    using WaterWaveParam = ::WaveParams;

    struct WaterSurfaceData {
        float waterHeight = 0.0f;
        uint32_t activeWaveCount = 0;
        float time = 0.0f;
        uint32_t simulationType = kWaterSurfaceModelTypeGerstner;
        WaveParams waves[kMaxWaterSurfaceWaveCount]{};

        // 水面メッシュのワールドXZ範囲（AABB）。コースティクスは解析的な無限水面として
        // 評価されるため、この矩形で受光側をマスクしないと「水面高さより低い場所すべて」
        // （無限市松床など水域の外）にも集光模様が投影されてしまう。
        // regionValid == 0 の場合は範囲制限なし（従来挙動）。
        float regionCenterXZ[2] = { 0.0f, 0.0f };
        float regionHalfExtentXZ[2] = { 0.0f, 0.0f };
        uint32_t regionValid = 0;

        // 水面メッシュの頂点グリッド分割数。RTコースティクスの coverage 判定は
        // 「実際にラスタライザが描く三角形メッシュ」と同一基準で波面を評価する必要があり
        // （波打ち際バグ⑨）、その格子の再現に使う。以前はシェーダー側に 256 が
        // ハードコードされ、シーン側のメッシュ変更で静かに壊れる構造だった。
        float meshSubdivisions = 256.0f;
    };

    struct WaterOpticalProperties {
        float refractiveIndex = 1.333f;
        float absorptionCoeff = 0.3f;
        float scatteringCoeff = 0.0f;
        float anisotropy = 0.0f;
    };

}
