#pragma once

#include <d3d12.h>
#include <cstdint>

namespace CoreEngine
{
    /// @brief RT ディスパッチの結果状態（全 RT パス共通）
    enum class RayTracingDispatchStatus : uint32_t {
        None = 0,                ///< 今フレームまだ呼ばれていない
        NotInitialized,          ///< マネージャが未初期化
        RayTracingUnsupported,   ///< DXR 非対応 GPU
        NoBLAS,                  ///< BLAS が 1 つも無い（TLAS が空）
        InvalidCommandList,      ///< コマンドリストが null
        CommandList4Unavailable, ///< ID3D12GraphicsCommandList4 が取得できない
        OutputAllocationFailed,  ///< 出力テクスチャの確保に失敗
        Skipped,                 ///< 条件により意図的にスキップ（ライト無効など）
        Dispatched,              ///< 正常にディスパッチした
    };

    /// @brief 状態の表示名を返す
    const char* ToString(RayTracingDispatchStatus status);

    /// @brief パス固有の補足値（ラベル + 数値）
    /// @details 水面なら waterHeight、影なら lightIndex のような「そのパスにしか無い値」を、
    ///          デバッグ UI 側がパスの種類を知らなくても表として出せるようにするための入れ物。
    struct RayTracingDispatchExtra {
        const char* label = nullptr;
        float value = 0.0f;
    };

    /// @brief RT ディスパッチ 1 回分の診断情報（全 RT パス共通の型）
    /// @details Stage 0 の目的はこの「共通の型」を先に決めることにある。
    ///          デバッグ UI がこの型だけを読むようにしておけば、Stage 2 で
    ///          RayTracingShadowManager を共通基盤（RayTracingPassBase）へ載せ替えても
    ///          UI 側を書き直す必要が無い。
    ///          設計書: Docs/Engine/RayTracing/RayTracing_Refactoring_Review.md
    struct RayTracingDispatchInfo {
        /// @brief extras に格納できる最大数
        static constexpr uint32_t kMaxExtras = 6;

        const char* passName = "";                                  ///< 表示名（マネージャ名など）
        RayTracingDispatchStatus status = RayTracingDispatchStatus::None;
        uint32_t viewIndex = 0;      ///< GameView / ReflectionView などのビュー添字
        uint32_t slotIndex = 0;      ///< ビュー内の細分（影ならライト番号。水面は 0）
        UINT width = 0;              ///< ディスパッチ解像度
        UINT height = 0;
        UINT blasCount = 0;          ///< ディスパッチ時点の BLAS 数
        uint64_t rayCount = 0;       ///< このディスパッチが発射したレイ本数
        uint64_t outputSrvPtr = 0;   ///< 出力テクスチャの SRV GPU ハンドル（0 = 未確保）

        RayTracingDispatchExtra extras[kMaxExtras]{};
        uint32_t extraCount = 0;

        /// @brief 補足値を追加する（上限を超えた分は捨てる）
        void AddExtra(const char* label, float value)
        {
            if (extraCount >= kMaxExtras) {
                return;
            }
            extras[extraCount].label = label;
            extras[extraCount].value = value;
            ++extraCount;
        }

        bool WasDispatched() const { return status == RayTracingDispatchStatus::Dispatched; }
    };
}
