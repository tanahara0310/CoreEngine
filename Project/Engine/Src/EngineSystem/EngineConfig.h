#pragma once

#include <string>
#include <cstdint>

namespace CoreEngine
{

    /// @brief エンジン設定データ（JSONコンフィグから読み込み）
    struct EngineConfig {

        // ──────────────────────────────────────────────────────────
        // ウィンドウ設定
        // ──────────────────────────────────────────────────────────
        std::string windowTitle = "CoreEngine_Ver2.0";
        int32_t windowWidth = 1280;
        int32_t windowHeight = 720;

        // ──────────────────────────────────────────────────────────
        // デバッグ設定
        // ──────────────────────────────────────────────────────────
        bool enableDebugLayer = false;
        bool enableGPUBasedValidation = false;
        bool enablePixRuntime = false;  // PIX GPU キャプチャ DLL を起動時にロードするか（D3D12 全コマンドが計装されフレーム時間に影響）

        // ──────────────────────────────────────────────────────────
        // コマンド / バッファリング設定
        // ──────────────────────────────────────────────────────────
        uint32_t frameCount = 2; // フレームバッファリング数（2=ダブルバッファ、3=トリプルバッファ）

        // ──────────────────────────────────────────────────────────
        // ディスクリプタ設定
        // ──────────────────────────────────────────────────────────
        uint32_t maxSRVDescriptors = 65536;
        uint32_t maxRTVDescriptors = 256;
        uint32_t maxDSVDescriptors = 10;

        /// @brief ビルド構成に応じたコンフィグファイルを読み込み
        /// @return 読み込んだEngineConfig
        static EngineConfig Load();

        /// @brief PIX ランタイムの有効/無効をコンフィグに書き込み、アプリを再起動する
        /// D3D12 デバイス作成前に DLL をロードする必要があるため再起動が必要
        /// @param enable true=有効化して再起動 / false=無効化して再起動
        static void SetPixRuntimeAndRestart(bool enable);

        /// @brief ウィンドウタイトルをワイド文字列で取得
        /// @return ワイド文字列のタイトル
        std::wstring GetWindowTitleWide() const;
    };

}
