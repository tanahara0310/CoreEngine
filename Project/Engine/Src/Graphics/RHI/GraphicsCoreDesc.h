#pragma once

#include <Windows.h>
#include <cstdint>

namespace CoreEngine
{
    /// @brief GraphicsCore の初期化パラメータ
    /// @details WinApp / EngineConfig を基盤層へ持ち込まないための値オブジェクト。
    ///          「どのウィンドウへ・どの大きさで・どの設定で」を値で受け取り、
    ///          それを埋めるのは上位（EngineSystem のファクトリ）の仕事にする。
    ///          これで依存は 上位 → 基盤 の一方向になる（基盤層が WinApp を include しない）。
    struct GraphicsCoreDesc {
        // ── 出力先ウィンドウ ──────────────────────────────────
        HWND hwnd = nullptr;
        int32_t clientWidth = 0;
        int32_t clientHeight = 0;

        // ── デバッグ ──────────────────────────────────────────
        bool enableDebugLayer = false;
        bool enableGPUBasedValidation = false;

        // ── フレーム同期 ──────────────────────────────────────
        /// CPU が GPU に先行してよいフレーム数（= per-frame リソースの本数）。
        /// スワップチェーンのバッファ枚数とは別の概念（Phase 2 参照）
        uint32_t framesInFlight = 2;

        // ── ディスクリプタ ────────────────────────────────────
        uint32_t maxSRVDescriptors = 65536;
        uint32_t maxRTVDescriptors = 256;
        uint32_t maxDSVDescriptors = 10;
    };
}
