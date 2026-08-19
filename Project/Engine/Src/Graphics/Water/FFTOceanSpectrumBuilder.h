#pragma once

#include <cstddef>
#include <cstdint>

namespace CoreEngine
{
    /// @brief FFT Ocean のスペクトル生成責務を担当するビルダー
    /// @details 設定のサニタイズ、Phillips Spectrum 生成、統計情報算出を分離して管理する。
    class FFTOceanSpectrumBuilder {
    public:
        /// @brief スペクトル生成のパラメータ（解像度・パッチ長・風・うねり）
        struct Settings {
            uint32_t resolution = 256;
            float patchLength = 96.0f;
            float amplitudeScale = 1.0f;
            float windDirection[2] = { 0.92f, 0.38f };
            float windSpeed = 24.0f;
            // 吹送距離（フェッチ）[m]。風が同じ向きに吹き続けた距離で、風速と並んで
            // JONSWAP のピーク周波数と有義波高を決める。短い＝若く尖った風波、
            // 長い＝完全発達したうねり。完全発達を超えては育たないのでクランプされる。
            float fetchMeters = 200000.0f;
            float choppiness = 1.35f;
            uint32_t activeComponentCount = 32;
            float gravity = 9.81f;

            // ガウス乱数のシード。カスケードごとに必ず変えること。
            // 全カスケードが同一シードだと位相パターンが完全相関し、
            // 「同じ波の配置が縮尺違いで全域に繰り返される」自己相似アーティファクトになる
            // （2026-07-21 に実際に発生した既知バグ）。
            uint32_t randomSeed = 20260626u;

            // 生成後の波高RMS（メートル）をこの値へ正規化する（0 以下で正規化なし）。
            // 素の Phillips スペクトルは離散化の Δk 正規化を持たず、波高が
            // patchLength × windSpeed² にほぼ比例して無制限に成長する
            // （L=340m/風速24m/s で RMS≈20m の「水の山」になる既知バグ）。
            // 呼び出し側が物理的に妥当な目標RMS（例: Pierson-Moskowitz の Hs/4）を
            // 渡すことで、パッチ長に依存しない現実的な波高に較正する。
            float targetRmsHeight = 0.0f;

            // ---- 帯域制限（カスケードごとの担当波数帯）----
            // 境界は全カスケード共通の 2 値で、cascadeIndex に応じて相補的な窓
            // （パワーの分割・和は厳密に 1）を作る。数値の情報源は
            // Shaders/Water/Common/FFTOceanCascadeValues.hlsli。
            // bandBoundaryLowK <= 0 のときは帯域制限なし（従来動作）。
            float bandBoundaryLowK = 0.0f;   ///< カスケード 0|1 の境界 k1 [rad/m]
            float bandBoundaryHighK = 0.0f;  ///< カスケード 1|2 の境界 k2 [rad/m]
            float bandLogHalfWidth = 0.35f;  ///< 境界のクロスフェード半値幅（ln k 上）
            uint32_t cascadeIndex = 0;       ///< このカスケードの番号（0 が最大パッチ）

            // ---- うねり（swell）成分 ----
            // 実海は「今の風が立てる風波（wind sea）」と「遠方の低気圧から到達した
            // 長周期のうねり」の重ね合わせで、両者は向きも周期も別。単一スペクトルでは
            // 「一様な風波」にしかならず、斜交する波列（クロスシー）も
            // 「凪でもうねりだけ残る」状態も表現できない。
            // うねりは発生源から離れて分散した結果なので、周波数・方向ともに狭帯域。
            float swellPeakAngularFrequency = 0.0f; ///< うねりのピーク角周波数 [rad/s]（0 で無効）
            float swellDirection[2] = { 1.0f, 0.0f }; ///< うねりの進行方向（正規化済み）
            float swellRelativeWidth = 0.06f;       ///< 周波数の相対バンド幅（ωs に対する比）
            float swellSpreadExponent = 24.0f;      ///< 指向の鋭さ s（大きいほど一方向）

            // ---- 成分別の分散重み（呼び出し側の較正が設定する）----
            // 密度に掛かる（＝分散の重み）。振幅は sqrt が掛かる。
            // 風波だけ / うねりだけ の生RMSを測ってから、両者の目標波高比になるよう
            // 重みを決めて本生成する 3 パス方式で使う。
            float windSeaVarianceWeight = 1.0f;
            float swellVarianceWeight = 0.0f;
        };

        /// @brief 波数 1 本分のスペクトル（複素振幅 h0 / h0Minus と波数ベクトル・角周波数）
        struct SpectrumSample {
            float h0[2] = {};
            float h0Minus[2] = {};
            float waveVector[2] = {};
            float angularFrequency = 0.0f;
            float directionalWeight = 0.0f;
            float padding[2] = {};
        };

        /// @brief 生成結果の統計（較正とデバッグ表示に使う）
        struct BuildStats {
            uint32_t activeSpectrumSampleCount = 0;
            float averageSpectralAmplitude = 0.0f;
            float maxSpectralAmplitude = 0.0f;
            float maxAngularFrequency = 0.0f;
            uint32_t probeIndex = 0;
            SpectrumSample probeSample{};

            // 正規化前の推定波高RMS（m）と、targetRmsHeight 適用時のスケール係数。
            float measuredRmsHeight = 0.0f;
            float appliedHeightScale = 1.0f;
        };

        /// @brief FFT Ocean 設定値を有効範囲へ正規化する
        static void SanitizeSettings(Settings& settings, uint32_t maxSpectrumComponents);

        /// @brief 指定バッファへスペクトルサンプルを生成する
        /// @param settings 生成設定
        /// @param outSpectrumSamples 出力先
        /// @param outSampleCount 出力バッファ要素数
        /// @return 生成統計情報
        static BuildStats BuildSpectrum(
            const Settings& settings,
            SpectrumSample* outSpectrumSamples,
            size_t outSampleCount);

        /// @brief 生成済みスペクトルの複素振幅を一様スケールする
        /// @details 波高の較正を全カスケード共通の 1 スケールで行うために使う。
        /// @note h0 と h0Minus の両方に掛けること（時間発展が両者を使う）
        static void ScaleSpectrum(
            SpectrumSample* spectrumSamples,
            size_t sampleCount,
            float scale);
    };
}
