#pragma once

namespace CoreEngine
{
    class RenderDomainContext;
    class WaterWaveModel;
    struct WaterFrameBinding;
    struct WaterSettings;
    struct WaterSurfaceData;

    /// @brief 海の範囲マップ（水面の範囲全体の水深と上の物までの高さ）を水面へつなぐ
    class WaterSeaMapModule {
    public:
        /// @brief 今の地図と波の高さを snapshot.seaMap へ書く（地図が無ければ空にする）
        /// @param drawnWaves 水面が描いている波のモデル（波の高さを聞く）
        /// @note snapshot の範囲・水面の高さ・波を書いた後に呼ぶ
        void Capture(
            RenderDomainContext& domain, const WaterSettings& settings, const WaterWaveModel& drawnWaves,
            WaterSurfaceData& snapshot) const;

        /// @brief 地図の SRV と範囲を結線へ書く
        void Bind(RenderDomainContext& domain, const WaterSurfaceData& snapshot, WaterFrameBinding& binding) const;
    };
}
