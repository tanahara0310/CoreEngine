#pragma once

#include "Camera/Shake/CameraShakeTypes.h"

#include <cstddef>
#include <vector>

/// @file
/// @brief カメラシェイクのランタイム（複数の揺れを合成して 1 つのオフセットにする）

namespace CoreEngine
{
    /// @brief 揺れの評価に必要な外界の情報
    /// @details Camera を知らずに済ませるための受け口。空間減衰の基準位置と時間だけを渡す。
    struct CameraShakeContext {
        /// @brief 空間減衰を測る基準（＝カメラのワールド位置）
        Vector3 listenerPosition = { 0.0f, 0.0f, 0.0f };

        /// @brief Time::DeltaTime()（ShakeTimeMode::Scaled の揺れが使う）
        float scaledDeltaTime = 0.0f;

        /// @brief Time::UnscaledDeltaTime()（ShakeTimeMode::Unscaled の揺れが使う）
        float unscaledDeltaTime = 0.0f;
    };

    /// @brief 複数の揺れを同時に走らせ、1 つの加算オフセットへ合成する
    ///
    /// @details
    /// **Camera にもエンジンにも依存しない**のがこのクラスの要点。入力は
    /// 「経過時間」と「リスナーのワールド位置」だけで、出力は CameraShakeOffset。
    /// そのため 3D カメラ・2D カメラ・UI・オブジェクトのどれにでも同じものを流用できるし、
    /// 単体でテストもできる。カメラへ実際に反映するのは CameraShakeFeature の仕事。
    ///
    /// 合成規則は「加算 → 上限でクランプ」。max で合成すると同時多発しても揺れが増えず、
    /// 加算だけだと爆発の連鎖で画面が壊れるため、その中間を取っている。
    ///
    /// @note メインスレッド専用。
    class CameraShaker {
    public:
        // ──────────────────────────────────────────────────────────
        // 再生
        // ──────────────────────────────────────────────────────────

        /// @brief 揺れを 1 件再生する
        /// @param params 揺れの定義
        /// @return 停止・照会に使うハンドル（duration が有限なら終了後は無効になる）
        ShakeHandle Play(const CameraShakeParams& params);

        /// @brief 発生源つきで揺れを再生する（爆発・着弾など）
        /// @param params 揺れの定義（useWorldFalloff を true にすると距離で弱まる）
        /// @param worldOrigin 発生源のワールド座標
        /// @note params.space が World・directionality > 0・direction がゼロのときは、
        ///       「発生源 → カメラ」を押される向きとして自動で使う。
        ShakeHandle Play(const CameraShakeParams& params, const Vector3& worldOrigin);

        // ──────────────────────────────────────────────────────────
        // trauma（蓄積型の揺れ）
        // ──────────────────────────────────────────────────────────

        /// @brief trauma を蓄積する（合計は 0..1 で飽和する）
        /// @details 小さいヒットが連続すると自然に強くなり、上限で頭打ちになる挙動を作る。
        ///          duration ベースの Play() では表現できないので別チャンネルとして持っている。
        void AddTrauma(float amount);

        /// @brief trauma チャンネルが使う揺れの定義を差し替える
        void SetTraumaParams(const CameraShakeParams& params) { traumaParams_ = params; }
        const CameraShakeParams& GetTraumaParams() const { return traumaParams_; }

        /// @brief 現在の trauma 量（0..1）
        float GetTrauma() const { return trauma_; }

        /// @brief trauma が 1 秒あたりに減る量（既定 1.0 = 1 秒で満タンから 0 へ）
        void SetTraumaRecovery(float perSecond);

        /// @brief trauma を振幅へ写すときの指数（既定 2.0）
        /// @note 1 より大きくすると「少し溜まった程度では揺れない」当たりの強い曲線になる。
        void SetTraumaExponent(float exponent);

        // ──────────────────────────────────────────────────────────
        // 停止・照会
        // ──────────────────────────────────────────────────────────

        /// @brief 指定の揺れを止める
        /// @param handle Play() が返したハンドル
        /// @param fadeOutSeconds 0 なら即時、正なら振幅を線形に絞って止める
        void Stop(ShakeHandle handle, float fadeOutSeconds = 0.0f);

        /// @brief すべての揺れを止める（trauma も 0 に戻す）
        void StopAll(float fadeOutSeconds = 0.0f);

        /// @brief 指定の揺れがまだ生きているか
        bool IsPlaying(ShakeHandle handle) const;

        /// @brief 再生中の件数（デバッグ表示用）
        std::size_t GetActiveCount() const { return active_.size(); }

        // ──────────────────────────────────────────────────────────
        // 全体設定
        // ──────────────────────────────────────────────────────────

        /// @brief 全体強度（0 で完全に無効）
        /// @note 画面揺れは 3D 酔いの原因になる。0 にできる口を必ず設定画面へ出すこと。
        void SetGlobalScale(float scale);
        float GetGlobalScale() const { return globalScale_; }

        /// @brief 合成結果の上限（暴走防止のクランプ）
        /// @param maxPositionMeters 平行移動の上限（メートル）
        /// @param maxRotationDegrees 回転の上限（度）
        /// @param maxFovDegrees 画角の上限（度）
        void SetLimits(float maxPositionMeters, float maxRotationDegrees, float maxFovDegrees);

        // ──────────────────────────────────────────────────────────
        // 更新
        // ──────────────────────────────────────────────────────────

        /// @brief 1 フレーム進めて合成結果を作り直す
        void Update(const CameraShakeContext& context);

        /// @brief 直近の Update() が作った合成結果
        const CameraShakeOffset& GetOffset() const { return offset_; }

        /// @brief すべての状態を初期化する（シーン遷移時）
        void Reset();

    private:
        /// @brief 再生中の揺れ 1 件
        struct ActiveShake {
            CameraShakeParams params{};
            ShakeHandle handle = 0;
            std::uint32_t seed = 0;
            float elapsed = 0.0f;

            Vector3 origin = { 0.0f, 0.0f, 0.0f };
            bool hasOrigin = false;

            bool fadingOut = false;
            float fadeOutDuration = 0.0f;
            float fadeOutElapsed = 0.0f;
        };

        /// @brief 次のハンドルを採番する（0 は返さない）
        ShakeHandle AcquireHandle();

        /// @brief 時間包絡（attack → 減衰 → フェードアウト）を評価する
        static float EvaluateEnvelope(const ActiveShake& shake);

        /// @brief 発生源からの距離による減衰を評価する
        static float EvaluateFalloff(const ActiveShake& shake, const Vector3& listenerPosition);

        /// @brief 揺れ 1 件ぶんの寄与を合成結果へ足す
        void Accumulate(
            const CameraShakeParams& params,
            float time,
            std::uint32_t seed,
            float weight,
            const Vector3& listenerPosition,
            const Vector3& origin,
            bool hasOrigin,
            CameraShakeOffset& out) const;

        /// @brief 合成結果を上限で丸める
        void ClampOffset(CameraShakeOffset& offset) const;

        /// @brief trauma チャンネルの既定パラメータ
        static CameraShakeParams MakeDefaultTraumaParams();

        std::vector<ActiveShake> active_;
        ShakeHandle nextHandle_ = 1;

        // trauma チャンネル
        CameraShakeParams traumaParams_ = MakeDefaultTraumaParams();
        float trauma_ = 0.0f;
        float traumaTime_ = 0.0f;
        float traumaRecovery_ = 1.0f;
        float traumaExponent_ = 2.0f;
        std::uint32_t traumaSeed_ = 0x9E3779B9u;

        CameraShakeOffset offset_{};

        float globalScale_ = 1.0f;
        float maxPositionOffset_ = 2.0f;   // メートル
        float maxRotationOffset_ = 15.0f;  // 度（内部ではラジアンへ直して使う）
        float maxFovOffset_ = 30.0f;       // 度
    };
}
