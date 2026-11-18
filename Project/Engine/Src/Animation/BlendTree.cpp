#include "pch.h"
#include "BlendTree.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace CoreEngine::BlendTree
{
    namespace
    {
        /// 原点に置いたとみなす長さ
        constexpr float kOriginLength = 1e-4f;

        /// 角度の差を長さの差と比べるときの倍率
        constexpr float kAngleWeight = 2.0f;

        float LengthOf(const Vector2& v)
        {
            return std::sqrt(v.x * v.x + v.y * v.y);
        }

        /// @brief a から b への向きの角度（-π〜π）
        float SignedAngle(const Vector2& a, const Vector2& b)
        {
            const float cross = a.x * b.y - a.y * b.x;
            const float dot = a.x * b.x + a.y * b.y;
            return std::atan2(cross, dot);
        }

        /// @brief 子 i から見た、子 j との間の値の近さ（1 で i の位置、0 で j の位置）
        float PairInfluence(const Vector2& pi, const Vector2& pj, const Vector2& p)
        {
            const float li = LengthOf(pi);
            const float lj = LengthOf(pj);
            const float lp = LengthOf(p);
            const bool iAtOrigin = li < kOriginLength;
            const bool jAtOrigin = lj < kOriginLength;
            if (iAtOrigin && jAtOrigin) {
                return 1.0f;
            }
            const float mean = (li + lj) * 0.5f;

            float angleIJ = 0.0f;
            float angleIP = 0.0f;
            if (iAtOrigin) {
                angleIJ = lp < kOriginLength ? 0.0f : SignedAngle(p, pj);
            } else if (jAtOrigin) {
                angleIJ = lp < kOriginLength ? 0.0f : SignedAngle(pi, p);
                angleIP = angleIJ;
            } else {
                angleIJ = SignedAngle(pi, pj);
                angleIP = lp < kOriginLength ? 0.0f : SignedAngle(pi, p);
            }

            const float ijX = (lj - li) / mean;
            const float ijY = angleIJ * kAngleWeight;
            const float ipX = (lp - li) / mean;
            const float ipY = angleIP * kAngleWeight;
            const float lengthSq = ijX * ijX + ijY * ijY;
            if (lengthSq <= 1e-12f) {
                return 1.0f;
            }
            return 1.0f - (ipX * ijX + ipY * ijY) / lengthSq;
        }
    }

    void Compute1D(std::span<const float> thresholds, float value, std::span<float> weights)
    {
        std::fill(weights.begin(), weights.end(), 0.0f);
        const size_t count = (std::min)(thresholds.size(), weights.size());
        if (count == 0) {
            return;
        }
        int lower = -1;
        int upper = -1;
        for (size_t i = 0; i < count; ++i) {
            const float threshold = thresholds[i];
            if (threshold <= value && (lower < 0 || threshold > thresholds[static_cast<size_t>(lower)])) {
                lower = static_cast<int>(i);
            }
            if (threshold >= value && (upper < 0 || threshold < thresholds[static_cast<size_t>(upper)])) {
                upper = static_cast<int>(i);
            }
        }
        if (lower < 0) {
            weights[static_cast<size_t>(upper)] = 1.0f;
            return;
        }
        if (upper < 0) {
            weights[static_cast<size_t>(lower)] = 1.0f;
            return;
        }
        const float low = thresholds[static_cast<size_t>(lower)];
        const float high = thresholds[static_cast<size_t>(upper)];
        if (lower == upper || high <= low) {
            weights[static_cast<size_t>(lower)] = 1.0f;
            return;
        }
        const float t = (value - low) / (high - low);
        weights[static_cast<size_t>(lower)] = 1.0f - t;
        weights[static_cast<size_t>(upper)] = t;
    }

    void Compute2D(std::span<const Vector2> positions, const Vector2& value, std::span<float> weights)
    {
        std::fill(weights.begin(), weights.end(), 0.0f);
        const size_t count = (std::min)(positions.size(), weights.size());
        if (count == 0) {
            return;
        }
        if (count == 1) {
            weights[0] = 1.0f;
            return;
        }

        float total = 0.0f;
        for (size_t i = 0; i < count; ++i) {
            float influence = 1.0f;
            for (size_t j = 0; j < count; ++j) {
                if (i != j) {
                    influence = (std::min)(influence, PairInfluence(positions[i], positions[j], value));
                }
            }
            weights[i] = (std::max)(0.0f, influence);
            total += weights[i];
        }

        if (total > 1e-6f) {
            for (size_t i = 0; i < count; ++i) {
                weights[i] /= total;
            }
            return;
        }

        // どの子の帯にも入らないときは、いちばん近い子だけ
        size_t nearest = 0;
        float nearestDistance = std::numeric_limits<float>::max();
        for (size_t i = 0; i < count; ++i) {
            const float dx = positions[i].x - value.x;
            const float dy = positions[i].y - value.y;
            const float distance = dx * dx + dy * dy;
            if (distance < nearestDistance) {
                nearestDistance = distance;
                nearest = i;
            }
        }
        std::fill(weights.begin(), weights.end(), 0.0f);
        weights[nearest] = 1.0f;
    }

    std::string PrimaryClip(const AnimatorControllerData& controller, const AnimatorState& state)
    {
        if (state.motion == AnimatorMotionType::Clip || state.blend.children.empty()) {
            return state.motion == AnimatorMotionType::Clip ? state.clip : std::string{};
        }
        const auto defaultOf = [&controller](const std::string& name) {
            const AnimatorParameter* const parameter = controller.FindParameter(name);
            return parameter && parameter->type == AnimatorParameterType::Float ? parameter->defaultFloat : 0.0f;
        };

        const size_t count = state.blend.children.size();
        std::vector<float> weights(count, 0.0f);
        if (state.motion == AnimatorMotionType::Blend1D) {
            std::vector<float> thresholds(count);
            for (size_t i = 0; i < count; ++i) {
                thresholds[i] = state.blend.children[i].threshold;
            }
            Compute1D(thresholds, defaultOf(state.blend.parameterX), weights);
        } else {
            std::vector<Vector2> positions(count);
            for (size_t i = 0; i < count; ++i) {
                positions[i] = state.blend.children[i].position;
            }
            Compute2D(positions, Vector2{ defaultOf(state.blend.parameterX), defaultOf(state.blend.parameterY) }, weights);
        }
        const size_t primary = static_cast<size_t>(std::max_element(weights.begin(), weights.end()) - weights.begin());
        return state.blend.children[primary].clip;
    }

    bool UsesClip(const AnimatorState& state, const std::string& clipId)
    {
        if (state.motion == AnimatorMotionType::Clip) {
            return state.clip == clipId;
        }
        return std::any_of(state.blend.children.begin(), state.blend.children.end(),
            [&clipId](const AnimatorBlendChild& child) { return child.clip == clipId; });
    }
}
