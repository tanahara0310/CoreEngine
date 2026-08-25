#pragma once

#include "Graphics/RHI/Resource/GpuResource.h"

#include "Graphics/Water/FFTOceanSpectrumBuilder.h"

#include <cstdint>
#include <d3d12.h>
#include <wrl.h>

namespace CoreEngine
{
    /// @brief FFT 海面のデバッグ計測（サマリログ・GPU リードバック統計・スペクトル CPU プローブ）
    /// @details CVar "d.FFTOcean.DebugProbe"（既定 off）でオプトイン。
    ///          無効時はリードバックバッファも確保せず、コピー・バリア・CPU 走査を一切行わない。
    class FFTOceanDebugProbe
    {
    public:
        using SpectrumSample = FFTOceanSpectrumBuilder::SpectrumSample;

        /// @brief CVar によるオプトイン状態（"d.FFTOcean.DebugProbe"）
        static bool IsEnabled();

        /// @brief フレーム先頭で呼ぶ: 前回積んだリードバック結果が揃っていればログへ掃き出す
        void FlushPendingLogs(uint32_t resolution);

        /// @brief Dispatch 先頭: 120 フレームごとにサマリログとスペクトル CPU プローブを出す
        /// @param samples UPLOAD ヒープのマップ先（write-combined 読みになるため有効時のみ触る）
        void LogDispatchSummary(
            float timeSeconds,
            float amplitudeScale,
            float windSpeed,
            float choppiness,
            uint32_t activeComponentCount,
            const SpectrumSample* samples,
            uint32_t resolution);

        /// @brief カスケード0の時間発展直後: 中間スペクトルのリードバックを 120 フレームごとに積む
        /// @details コピー後、両テクスチャは UNORDERED_ACCESS へ戻す（時間発展直後の状態を維持）
        void ScheduleEvolutionReadback(
            ID3D12GraphicsCommandList* cmdList,
            GpuResource& spectrumA,
            GpuResource& spectrumB);

        /// @brief カスケード0の IFFT 完了後: 完了ログと IFFT 結果のリードバックを 120 フレームごとに積む
        /// @details コピー後、両テクスチャは NON_PIXEL_SHADER_RESOURCE へ戻す（Finalize が読むため）
        void OnIFFTCompleted(
            ID3D12GraphicsCommandList* cmdList,
            uint32_t finalSpectrumAIndex,
            uint32_t finalSpectrumBIndex,
            uint32_t resolution,
            uint32_t log2Resolution,
            GpuResource& spectrumA,
            GpuResource& spectrumB);

        /// @brief フレーム末尾: 最終出力（変位/法線）のリードバックを 120 フレームごとに積む
        /// @details コピー後、両テクスチャは NON_PIXEL_SHADER_RESOURCE へ戻す
        void ScheduleSurfaceReadback(
            ID3D12GraphicsCommandList* cmdList,
            GpuResource& displacement,
            GpuResource& normal);

    private:
        /// @brief 1 本分のリードバック先（バッファ＋レイアウト＋ペア管理）
        struct ReadbackTarget {
            Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
            D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{};
            UINT64 totalBytes = 0;
        };

        /// @brief 対象テクスチャに合うリードバックバッファを（未生成なら）作る
        static bool EnsureTarget(ReadbackTarget& target, ID3D12Resource* sourceTexture, const char* debugName);

        // ペア単位のリードバック（surface = 変位+法線 / evolution・ifft = スペクトルA+B）
        ReadbackTarget surfaceDisplacement_{};
        ReadbackTarget surfaceNormal_{};
        ReadbackTarget evolutionA_{};
        ReadbackTarget evolutionB_{};
        ReadbackTarget ifftA_{};
        ReadbackTarget ifftB_{};

        bool surfacePending_ = false;
        bool evolutionPending_ = false;
        bool ifftPending_ = false;
        uint64_t surfaceSequence_ = 0;
        uint64_t evolutionSequence_ = 0;
        uint64_t ifftSequence_ = 0;

        // 120 フレーム間引き用カウンタ（以前は関数ローカル static で、
        // 複数インスタンス間で共有される欠陥があった）
        uint32_t summaryCounter_ = 0;
        uint32_t evolutionCounter_ = 0;
        uint32_t ifftCounter_ = 0;
        uint32_t surfaceCounter_ = 0;
        float previousLoggedTime_ = 0.0f;
    };
}
