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

    /// @brief 海の範囲マップ（WaterSeaMapRayTracingManager が水面の範囲全体を測った地図）の結線
    /// @details 地図は (水深, 上の物までの高さ, 粗い格子用の水深, 粗い格子用の上の物までの高さ) [m]。
    ///          波の変位をこの 2 つの高さで弱める（WaterSeaMap.hlsli）
    struct WaterSeaMapBinding {
        uint32_t srvHeapIndex = 0;          ///< 地図の SRV のシェーダー可視ヒープの中の番号（RT が ResourceDescriptorHeap で引く）
        uint32_t resolution[2] = { 0, 0 };  ///< 地図の X・Z のテクセル数（0 なら地図なし）
        float originXZ[2] = { 0.0f, 0.0f }; ///< 地図の範囲の XZ の最小の角 [m]
        float texelsPerMeter = 0.0f;        ///< 1 m あたりのテクセル数
        float depthOffset = 0.0f;           ///< 今の水面の高さ − 測ったときの水面の高さ [m]
        float invWaveHeight = 0.0f;         ///< 波の高さ（有義波高）の逆数 [1/m]
    };

    /// @brief 濡れ跡の表の標本数（WetSurface.hlsli の kWetSurfaceSampleCount と同じ）
    static constexpr uint32_t kWaterWetnessSampleCount = 64;

    /// @brief 水が引いたあとの濡れ跡の表（WaterWetnessHistory が作る）
    /// @details 今の水面から上へ等間隔に、その高さの面の濡れ具合 [0,1] を並べる
    struct WaterWetnessBinding {
        uint32_t enabled = 0;         ///< 1 = 濡れ跡を描く
        float bottomHeight = 0.0f;    ///< 表の下端（今の水面の高さ）[m]
        float samplesPerMeter = 0.0f; ///< 1 m あたりの標本の数
        float wetness[kWaterWetnessSampleCount] = {}; ///< 下端から上へ並べた濡れ具合
    };

    /// @brief 水面の形状を再現するのに必要な状態（水面高さ・時刻・波パラメータ）
    /// @note 水面パスと RT パス・水中判定が同じ波面を見るための単一の情報源
    struct WaterSurfaceData {
        float waterHeight = 0.0f;
        float tideHeight = 0.0f; ///< waterHeight のうち、潮でトランスフォームの高さから上下させた分 [m]
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

        // 岸の細かい格子（WaterSurfaceMeshGenerator）。範囲は海底の高さと同じで、
        // 範囲の中はこの格子の三角形で、範囲の外は粗い格子の三角形で水面を描く
        uint32_t shoreMeshEnabled = 0;
        float shoreMeshOriginXZ[2] = { 0.0f, 0.0f }; ///< 範囲の XZ の最小の角 [m]
        float shoreMeshSize = 0.0f;                  ///< 範囲の一辺の長さ [m]
        float shoreMeshCellSize = 0.0f;              ///< 1 マスの幅 [m]
        float shoreMeshBlendWidth = 0.0f;            ///< 範囲の端から内側へ、粗い格子の形から移す幅 [m]

        // 海の範囲マップ。水深と上の物までの高さで波の変位を弱める
        WaterSeaMapBinding seaMap{};

        // 水が引いたあとの濡れ跡
        WaterWetnessBinding wetness{};
    };

    /// @brief 水の光学特性（屈折率・吸収・散乱）
    struct WaterOpticalProperties {
        float refractiveIndex = 1.333f;
        float absorptionCoeff = 0.3f;
        float scatteringCoeff = 0.0f;
        float anisotropy = 0.0f;
    };

}
