#include "pch.h"
#include "CameraShaker.h"

#include "Math/MathCore.h"

#include <algorithm>
#include <cmath>

namespace CoreEngine
{
    namespace
    {
        /// @brief 32bit 整数ハッシュ（乱数の種から再現可能な値を作るだけのもの）
        std::uint32_t HashUint(std::uint32_t x)
        {
            x ^= x >> 16;
            x *= 0x7feb352du;
            x ^= x >> 15;
            x *= 0x846ca68bu;
            x ^= x >> 16;
            return x;
        }

        /// @brief ハッシュ値を -1..1 の実数へ写す
        float HashToSigned(std::uint32_t x)
        {
            // 上位 24bit を 0..1 へ写してから -1..1 へ広げる
            return static_cast<float>(HashUint(x) >> 8) * (1.0f / 8388608.0f) - 1.0f;
        }

        /// @brief 格子番号（負の位相でも破綻しないように整数へ落とす）
        std::uint32_t CellIndex(float floored)
        {
            return static_cast<std::uint32_t>(static_cast<std::int32_t>(floored));
        }

        /// @brief 1 次元の勾配ノイズ（おおむね -1..1・滑らか）
        float GradientNoise(float t, std::uint32_t seed)
        {
            const float floored = std::floor(t);
            const std::uint32_t cell = CellIndex(floored);
            const float frac = t - floored;

            const float gradient0 = HashToSigned(cell ^ seed);
            const float gradient1 = HashToSigned((cell + 1u) ^ seed);

            // 5 次のフェード関数（Perlin と同じ形。2 階微分まで連続になる）
            const float fade = frac * frac * frac * (frac * (frac * 6.0f - 15.0f) + 10.0f);

            // 勾配ノイズの振幅は最大でも 0.5 程度なので 2 倍して -1..1 に揃える
            return 2.0f * MathCore::Lerp(gradient0 * frac, gradient1 * (frac - 1.0f), fade);
        }

        /// @brief 1 次元の値ノイズ（-1..1・折れ目が残る粗い揺れ）
        /// @note 「毎フレーム乱数」に近い見た目を、フレームレート非依存のまま作るためのもの。
        ///       値を取り直す間隔は 1 / frequency 秒に固定される。
        float ValueNoise(float t, std::uint32_t seed)
        {
            const float floored = std::floor(t);
            const std::uint32_t cell = CellIndex(floored);
            const float frac = t - floored;

            return MathCore::Lerp(HashToSigned(cell ^ seed), HashToSigned((cell + 1u) ^ seed), frac);
        }

        /// @brief 1 軸ぶんの波形を評価する（戻り値はおおむね -1..1）
        /// @param phase 経過時間 × 周波数（＝何周期進んだか）
        float SampleAxis(ShakeWaveform waveform, float phase, std::uint32_t seed)
        {
            switch (waveform) {
            case ShakeWaveform::Random:
                return ValueNoise(phase, seed);

            case ShakeWaveform::Sine:
                // 位相をシードでずらす。軸ごとに揃うと真っ直ぐな往復に見えるため
                return std::sin((phase + HashToSigned(seed)) * MathCore::Constants::kTwoPi);

            case ShakeWaveform::Kick:
                // 減衰振動。最初の一振りが最も大きく、4 周期ほどで収まる。
                // 減衰は「周期数」で効くので、Kick を使う揺れは
                // frequency × duration が 2〜4 になるよう低めの周波数にすること
                return std::sin(phase * MathCore::Constants::kTwoPi) * std::exp(-0.9f * phase);

            case ShakeWaveform::Perlin:
            default:
                return GradientNoise(phase, seed);
            }
        }

        /// @brief 成分ごとの積
        Vector3 Scale(const Vector3& value, const Vector3& scale)
        {
            return { value.x * scale.x, value.y * scale.y, value.z * scale.z };
        }

        /// @brief ベクトルの長さを上限で丸める
        void ClampLength(Vector3& value, float maxLength)
        {
            if (maxLength <= 0.0f) {
                return;
            }
            const float lengthSquared = LengthSquared(value);
            if (lengthSquared > maxLength * maxLength) {
                value = value * (maxLength / std::sqrt(lengthSquared));
            }
        }
    }

    CameraShakeParams CameraShaker::MakeDefaultTraumaParams()
    {
        CameraShakeParams params;
        params.rotationAmplitude = { 1.2f, 1.2f, 2.0f };
        params.frequency = 22.0f;
        params.duration = 0.0f;   // 包絡は trauma 量が担うので無限扱いにする
        params.waveform = ShakeWaveform::Perlin;
        params.space = ShakeSpace::CameraLocal;
        return params;
    }

    ShakeHandle CameraShaker::AcquireHandle()
    {
        // 0 は無効値なので飛ばす（32bit を一周したときだけ効く）
        if (nextHandle_ == 0) {
            nextHandle_ = 1;
        }
        return nextHandle_++;
    }

    ShakeHandle CameraShaker::Play(const CameraShakeParams& params)
    {
        ActiveShake shake;
        shake.params = params;
        shake.handle = AcquireHandle();

        // シード未指定なら再生ごとにばらす。同じ揺れを連続で撃っても波形が重ならない
        shake.seed = (params.seed != 0) ? params.seed : HashUint(shake.handle * 2654435761u + 1u);

        active_.push_back(shake);
        return shake.handle;
    }

    ShakeHandle CameraShaker::Play(const CameraShakeParams& params, const Vector3& worldOrigin)
    {
        const ShakeHandle handle = Play(params);
        if (handle != 0 && !active_.empty()) {
            active_.back().origin = worldOrigin;
            active_.back().hasOrigin = true;
        }
        return handle;
    }

    void CameraShaker::AddTrauma(float amount)
    {
        trauma_ = MathCore::Saturate(trauma_ + amount);
    }

    void CameraShaker::SetTraumaRecovery(float perSecond)
    {
        traumaRecovery_ = (std::max)(0.0f, perSecond);
    }

    void CameraShaker::SetTraumaExponent(float exponent)
    {
        traumaExponent_ = (std::max)(0.01f, exponent);
    }

    void CameraShaker::Stop(ShakeHandle handle, float fadeOutSeconds)
    {
        if (handle == 0) {
            return;
        }

        const auto it = std::find_if(active_.begin(), active_.end(),
            [handle](const ActiveShake& shake) { return shake.handle == handle; });
        if (it == active_.end()) {
            return;
        }

        if (fadeOutSeconds <= 0.0f) {
            active_.erase(it);
            return;
        }

        it->fadingOut = true;
        it->fadeOutDuration = fadeOutSeconds;
        it->fadeOutElapsed = 0.0f;
    }

    void CameraShaker::StopAll(float fadeOutSeconds)
    {
        trauma_ = 0.0f;

        if (fadeOutSeconds <= 0.0f) {
            active_.clear();
            offset_ = CameraShakeOffset{};
            return;
        }

        for (ActiveShake& shake : active_) {
            if (shake.fadingOut) {
                continue;
            }
            shake.fadingOut = true;
            shake.fadeOutDuration = fadeOutSeconds;
            shake.fadeOutElapsed = 0.0f;
        }
    }

    bool CameraShaker::IsPlaying(ShakeHandle handle) const
    {
        if (handle == 0) {
            return false;
        }
        return std::any_of(active_.begin(), active_.end(),
            [handle](const ActiveShake& shake) { return shake.handle == handle; });
    }

    void CameraShaker::SetGlobalScale(float scale)
    {
        globalScale_ = (std::max)(0.0f, scale);
    }

    void CameraShaker::SetLimits(float maxPositionMeters, float maxRotationDegrees, float maxFovDegrees)
    {
        maxPositionOffset_ = (std::max)(0.0f, maxPositionMeters);
        maxRotationOffset_ = (std::max)(0.0f, maxRotationDegrees);
        maxFovOffset_ = (std::max)(0.0f, maxFovDegrees);
    }

    void CameraShaker::Reset()
    {
        active_.clear();
        trauma_ = 0.0f;
        traumaTime_ = 0.0f;
        offset_ = CameraShakeOffset{};
    }

    float CameraShaker::EvaluateEnvelope(const ActiveShake& shake)
    {
        const CameraShakeParams& params = shake.params;
        float envelope = 1.0f;

        // 立ち上がり
        if (params.attack > 0.0f) {
            envelope *= MathCore::Saturate(shake.elapsed / params.attack);
        }

        // 減衰（duration が 0 以下なら無限＝減衰しない）
        if (params.duration > 0.0f) {
            const float attack = (std::min)((std::max)(params.attack, 0.0f), params.duration);
            const float decayDuration = params.duration - attack;
            if (decayDuration <= 0.0f) {
                envelope *= (shake.elapsed >= params.duration) ? 0.0f : 1.0f;
            } else {
                const float progress = MathCore::Saturate((shake.elapsed - attack) / decayDuration);
                envelope *= 1.0f - EasingUtil::Apply(progress, params.decayEase);
            }
        }

        // フェードアウト（Stop / StopAll 由来）
        if (shake.fadingOut) {
            envelope *= (shake.fadeOutDuration > 0.0f)
                ? MathCore::Saturate(1.0f - shake.fadeOutElapsed / shake.fadeOutDuration)
                : 0.0f;
        }

        return envelope;
    }

    float CameraShaker::EvaluateFalloff(const ActiveShake& shake, const Vector3& listenerPosition)
    {
        const CameraShakeParams& params = shake.params;
        if (!params.useWorldFalloff || !shake.hasOrigin) {
            return 1.0f;
        }

        const float outer = (std::max)(params.outerRadius, 1.0e-4f);
        const float inner = MathCore::Clamp(params.innerRadius, 0.0f, outer);
        const float distance = Distance(listenerPosition, shake.origin);

        if (distance <= inner) {
            return 1.0f;
        }
        if (distance >= outer || outer <= inner) {
            return 0.0f;
        }

        const float progress = (distance - inner) / (outer - inner);
        return 1.0f - EasingUtil::Apply(progress, params.falloffEase);
    }

    void CameraShaker::Accumulate(
        const CameraShakeParams& params,
        float time,
        std::uint32_t seed,
        float weight,
        const Vector3& listenerPosition,
        const Vector3& origin,
        bool hasOrigin,
        CameraShakeOffset& out) const
    {
        if (weight <= 0.0f) {
            return;
        }

        // 軸ごとに周波数とシードをずらす。揃えると 1 本の線上を往復して見える
        const Vector3 noise = {
            SampleAxis(params.waveform, time * params.frequency * params.frequencyScale.x, seed + 0u),
            SampleAxis(params.waveform, time * params.frequency * params.frequencyScale.y, seed + 101u),
            SampleAxis(params.waveform, time * params.frequency * params.frequencyScale.z, seed + 211u),
        };
        const float fovNoise = SampleAxis(params.waveform, time * params.frequency, seed + 307u);

        const float directionality = MathCore::Saturate(params.directionality);

        // 位置の方向性：等方なノイズと「direction へ蹴られる揺れ」を混ぜる
        Vector3 positionNoise = noise;
        if (directionality > 0.0f) {
            Vector3 direction = params.direction;

            // 爆心 → カメラ。爆発は「押される」ので、発生源が分かれば向きは自動で決まる
            if (LengthSquared(direction) <= 1.0e-8f && hasOrigin && params.space == ShakeSpace::World) {
                const Vector3 away = listenerPosition - origin;
                if (LengthSquared(away) > 1.0e-8f) {
                    direction = Normalize(away);
                }
            }

            if (LengthSquared(direction) > 1.0e-8f) {
                const Vector3 directed = Normalize(direction) * noise.x;
                positionNoise = noise * (1.0f - directionality) + directed * directionality;
            }
        }

        // 回転の方向性：direction は平行移動の向きなので、回転へ射影しても意味を持たない
        //（射影すると direction と直交する軸の rotationAmplitude が丸ごと消えてしまう）。
        // 回転では「3 軸が同じ位相で揃って振れる」ことを方向性とみなし、
        // 軸ごとの強さは rotationAmplitude にそのまま任せる
        const Vector3 rotationNoise =
            noise * (1.0f - directionality) + Vector3{ noise.x, noise.x, noise.x } * directionality;

        const float scale = weight * globalScale_;

        // 位置：params.space の座標系へ積む
        const Vector3 position = Scale(positionNoise, params.positionAmplitude) * scale;
        if (params.space == ShakeSpace::World) {
            out.worldPosition += position;
        } else {
            out.localPosition += position;
        }

        // 回転は常にカメラローカル（ワールド軸で首を振るとカメラの向きで揺れ方が変わる）
        out.localRotation +=
            Scale(rotationNoise, params.rotationAmplitude) * (scale * MathCore::Constants::kDegToRad);

        out.fovDegrees += fovNoise * params.fovAmplitude * scale;
    }

    void CameraShaker::ClampOffset(CameraShakeOffset& offset) const
    {
        ClampLength(offset.localPosition, maxPositionOffset_);
        ClampLength(offset.worldPosition, maxPositionOffset_);
        ClampLength(offset.localRotation, maxRotationOffset_ * MathCore::Constants::kDegToRad);
        offset.fovDegrees = MathCore::Clamp(offset.fovDegrees, -maxFovOffset_, maxFovOffset_);
    }

    void CameraShaker::Update(const CameraShakeContext& context)
    {
        CameraShakeOffset offset{};

        for (ActiveShake& shake : active_) {
            const float deltaTime = (shake.params.timeMode == ShakeTimeMode::Unscaled)
                ? context.unscaledDeltaTime
                : context.scaledDeltaTime;

            shake.elapsed += deltaTime;
            if (shake.fadingOut) {
                shake.fadeOutElapsed += deltaTime;
            }

            const float weight =
                EvaluateEnvelope(shake) * EvaluateFalloff(shake, context.listenerPosition);

            Accumulate(
                shake.params, shake.elapsed, shake.seed, weight,
                context.listenerPosition, shake.origin, shake.hasOrigin, offset);
        }

        // 終わったものを外す。無限（duration <= 0）はフェードアウトでしか終わらない
        std::erase_if(active_, [](const ActiveShake& shake) {
            const bool durationOver =
                (shake.params.duration > 0.0f) && (shake.elapsed >= shake.params.duration);
            const bool fadeOver =
                shake.fadingOut && (shake.fadeOutElapsed >= shake.fadeOutDuration);
            return durationOver || fadeOver;
        });

        // trauma チャンネル（包絡は trauma 量そのもの）
        if (trauma_ > 0.0f) {
            const float deltaTime = (traumaParams_.timeMode == ShakeTimeMode::Unscaled)
                ? context.unscaledDeltaTime
                : context.scaledDeltaTime;

            traumaTime_ += deltaTime;
            trauma_ = (std::max)(0.0f, trauma_ - traumaRecovery_ * deltaTime);

            Accumulate(
                traumaParams_, traumaTime_, traumaSeed_,
                std::pow(trauma_, traumaExponent_),
                context.listenerPosition, Vector3{}, false, offset);
        } else {
            traumaTime_ = 0.0f;
        }

        ClampOffset(offset);
        offset_ = offset;
    }
}
