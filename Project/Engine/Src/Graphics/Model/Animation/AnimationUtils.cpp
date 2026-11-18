#include "pch.h"
#include "AnimationUtils.h"
#include <Math/MathCore.h>
#include <cassert>
#include <algorithm>


namespace CoreEngine
{
namespace AnimationUtils {

namespace {
    /// @brief 2 つのキーフレーム間の補間係数（0.0 ~ 1.0）を計算
    inline float CalculateInterpolationFactor(float t1, float t2, float currentTime) {
        return (currentTime - t1) / (t2 - t1);
    }

    /// @brief 位置とスケールのキーの間を線形補間する
    Vector3 InterpolateKey(const Vector3& from, const Vector3& to, float t) {
        return from + (to - from) * t;
    }

    /// @brief 回転のキーの間を球面線形補間する
    Quaternion InterpolateKey(const Quaternion& from, const Quaternion& to, float t) {
        return MathCore::QuaternionMath::Slerp(from, to, t);
    }

    /// @brief キーフレーム配列から任意の時刻の値を、前後のキーの補間で求める
    /// @details 範囲外の時刻は先頭か末尾のキーの値を返す。
    template <typename T>
    T Sample(const std::vector<Keyframe<T>>& keyframes, float time) {
        assert(!keyframes.empty());

        // 単一キーフレームまたは範囲外の場合
        if (keyframes.size() == 1 || time <= keyframes.front().time) {
            return keyframes.front().value;
        }
        if (time >= keyframes.back().time) {
            return keyframes.back().value;
        }

        // 二分探索で効率的に検索
        auto it = std::lower_bound(
            keyframes.begin(),
            keyframes.end(),
            time,
            [](const Keyframe<T>& kf, float t) { return kf.time < t; }
        );

        if (it == keyframes.begin() || it == keyframes.end()) {
            return keyframes.front().value;
        }

        size_t nextIndex = std::distance(keyframes.begin(), it);
        size_t index = nextIndex - 1;

        float t = CalculateInterpolationFactor(
            keyframes[index].time,
            keyframes[nextIndex].time,
            time
        );

        return InterpolateKey(keyframes[index].value, keyframes[nextIndex].value, t);
    }
}

Vector3 CalculateVector3(const std::vector<Keyframe<Vector3>>& keyframes, float time) {
    return Sample(keyframes, time);
}

Quaternion CalculateQuaternion(const std::vector<Keyframe<Quaternion>>& keyframes, float time) {
    return Sample(keyframes, time);
}

} // namespace AnimationUtils
}
