#pragma once

#include "Camera/Shake/CameraShakeTypes.h"

/// @file
/// @brief よく使う揺れの既定値

namespace CoreEngine
{
    /// @brief ゲームを問わず使える揺れのプリセット
    /// @details 「どのゲームでも使える」を実際に成立させるのはクラスの表現力ではなく、
    ///          ここを丸ごと差し替えられることのほう。値はそのまま使うより、
    ///          コピーして 1〜2 個のフィールドを上書きする使い方を想定している。
    /// @code
    ///     CameraShakeParams params = CameraShakePresets::Hit();
    ///     params.rotationAmplitude = params.rotationAmplitude * 2.0f;  // 強めの被弾
    ///     CameraShake::Play(params);
    /// @endcode
    namespace CameraShakePresets
    {
        /// @brief 被弾・小さな衝撃。短く粗い揺れ
        inline CameraShakeParams Hit()
        {
            CameraShakeParams params;
            params.rotationAmplitude = { 0.5f, 0.5f, 0.9f };
            params.frequency = 24.0f;
            params.duration = 0.22f;
            params.decayEase = EasingUtil::Type::EaseOutQuad;
            params.waveform = ShakeWaveform::Random;
            return params;
        }

        /// @brief 大きな被弾・重い一撃
        inline CameraShakeParams HeavyHit()
        {
            CameraShakeParams params = Hit();
            params.positionAmplitude = { 0.06f, 0.06f, 0.0f };
            params.rotationAmplitude = { 1.4f, 1.4f, 2.4f };
            params.frequency = 18.0f;
            params.duration = 0.45f;
            params.decayEase = EasingUtil::Type::EaseOutCubic;
            return params;
        }

        /// @brief 爆発。発生源からの距離で弱まり、爆心から押される向きに寄る
        /// @note Play(params, 爆心のワールド座標) で再生すること。
        /// @note これは低周波の「押される」揺れ。高周波のガタつきも欲しいときは
        ///       Hit() を重ねて再生する（複数の揺れは加算合成される）。
        inline CameraShakeParams Explosion()
        {
            CameraShakeParams params;
            params.positionAmplitude = { 0.18f, 0.18f, 0.18f };
            params.rotationAmplitude = { 1.6f, 1.6f, 2.2f };
            params.fovAmplitude = 1.5f;
            params.frequency = 5.0f;   // Kick は周期数で減衰する。0.7s x 5Hz = 3.5 周期
            params.duration = 0.7f;
            params.decayEase = EasingUtil::Type::EaseOutCubic;
            params.directionality = 0.6f;   // direction は空でよい（爆心 → カメラを自動で使う）
            params.useWorldFalloff = true;
            params.innerRadius = 3.0f;
            params.outerRadius = 40.0f;
            params.waveform = ShakeWaveform::Kick;
            params.space = ShakeSpace::World;
            return params;
        }

        /// @brief 着地・踏みつけ。下方向へ一度沈んで戻る
        inline CameraShakeParams Landing()
        {
            CameraShakeParams params;
            params.positionAmplitude = { 0.0f, 0.12f, 0.0f };
            params.rotationAmplitude = { 1.0f, 0.3f, 0.0f };
            params.frequency = 4.0f;   // 0.35s x 4Hz = 1.4 周期。沈んで戻るだけの形
            params.duration = 0.35f;
            params.decayEase = EasingUtil::Type::EaseOutQuad;
            params.direction = { 0.0f, -1.0f, 0.0f };
            params.directionality = 1.0f;
            params.waveform = ShakeWaveform::Kick;
            params.space = ShakeSpace::CameraLocal;
            return params;
        }

        /// @brief 発射反動。カメラが後ろへ蹴られて銃口が跳ね上がる
        inline CameraShakeParams Recoil()
        {
            CameraShakeParams params;
            params.positionAmplitude = { 0.0f, 0.0f, 0.05f };
            params.rotationAmplitude = { 0.8f, 0.2f, 0.2f };
            params.frequency = 12.0f;  // 0.16s x 12Hz = 約 2 周期
            params.duration = 0.16f;
            params.decayEase = EasingUtil::Type::EaseOutQuart;
            params.direction = { 0.0f, 0.0f, -1.0f }; // カメラローカルの後方
            params.directionality = 1.0f;
            params.waveform = ShakeWaveform::Kick;
            params.space = ShakeSpace::CameraLocal;
            return params;
        }

        /// @brief 地震。低周波でワールド軸に固定された長い揺れ
        /// @note duration が 0 なので Stop(handle) / StopAll() で止めること。
        inline CameraShakeParams Earthquake()
        {
            CameraShakeParams params;
            params.positionAmplitude = { 0.12f, 0.07f, 0.12f };
            params.rotationAmplitude = { 0.3f, 0.3f, 0.5f };
            params.frequency = 4.5f;
            params.duration = 0.0f;   // 無限
            params.attack = 0.8f;
            params.waveform = ShakeWaveform::Perlin;
            params.space = ShakeSpace::World;
            return params;
        }

        /// @brief 手持ちカメラ。常時かけっぱなしにする極小の揺らぎ
        inline CameraShakeParams Handheld()
        {
            CameraShakeParams params;
            params.rotationAmplitude = { 0.18f, 0.22f, 0.10f };
            params.frequency = 0.9f;
            params.duration = 0.0f;   // 無限
            params.attack = 0.5f;
            params.waveform = ShakeWaveform::Perlin;
            params.timeMode = ShakeTimeMode::Unscaled; // ポーズ中も止めない
            return params;
        }

        /// @brief エンジン・機械の微振動
        inline CameraShakeParams Rumble()
        {
            CameraShakeParams params;
            params.positionAmplitude = { 0.008f, 0.008f, 0.0f };
            params.rotationAmplitude = { 0.06f, 0.06f, 0.06f };
            params.frequency = 34.0f;
            params.duration = 0.0f;   // 無限
            params.attack = 0.25f;
            params.waveform = ShakeWaveform::Sine;
            return params;
        }
    }
}
