#include "pch.h"
#include "WaterWetnessHistory.h"

#include <algorithm>
#include <cmath>

namespace CoreEngine
{
    namespace {
        /// @brief 濡れ具合がこの値を下回った高さは乾いたものとして忘れる
        constexpr double kForgetWetness = 0.004;
        /// @brief 水面が下がるときに印を残す高さの間隔 [m]
        constexpr float kMinMarkSpacing = 0.001f;
        /// @brief 表が覆う高さの幅の下限 [m]
        constexpr float kMinTableSpan = 0.005f;
        /// @brief 乾く時間の下限 [s]
        constexpr float kMinDrySeconds = 0.1f;

        double ClampDrySeconds(float drySeconds)
        {
            return static_cast<double>((std::max)(drySeconds, kMinDrySeconds));
        }
    }

    void WaterWetnessHistory::Record(float waterHeight, float deltaSeconds, float drySeconds)
    {
        now_ += static_cast<double>((std::max)(deltaSeconds, 0.0f));
        if (marks_.empty()) {
            marks_.push_back({ waterHeight, now_ });
            return;
        }

        if (waterHeight >= marks_.back().height) {
            // 水面が上がった。今の水面より下の印は今浸かっている。今の水面より上の時刻は変えないので、
            // 今の水面の高さで切った点を残す
            const size_t upper = FindUpperMark(waterHeight);
            const double boundaryTime = upper < marks_.size() ? LastSubmergedTime(upper, waterHeight) : now_;
            while (!marks_.empty() && marks_.back().height <= waterHeight) {
                marks_.pop_back();
            }
            if (!marks_.empty()) {
                marks_.push_back({ waterHeight, boundaryTime });
            }
            marks_.push_back({ waterHeight, now_ });
        } else {
            // 水面が下がった。直前の印との間隔が狭いときは、直前の印を今の水面へずらす
            const size_t count = marks_.size();
            if (count >= 2 && marks_[count - 2].height - waterHeight < kMinMarkSpacing
                && marks_[count - 2].height > marks_[count - 1].height) {
                marks_.back() = { waterHeight, now_ };
            } else {
                marks_.push_back({ waterHeight, now_ });
            }
        }

        // 乾ききった高さの印を忘れる（2 番目の印が乾いていれば、それより上はどこも乾いている）
        const double forgetBefore = now_ - ClampDrySeconds(drySeconds) * -std::log(kForgetWetness);
        size_t dropCount = 0;
        while (dropCount + 1 < marks_.size() && marks_[dropCount + 1].time < forgetBefore) {
            ++dropCount;
        }
        if (dropCount > 0) {
            marks_.erase(marks_.begin(), marks_.begin() + static_cast<std::ptrdiff_t>(dropCount));
        }
    }

    void WaterWetnessHistory::Clear()
    {
        marks_.clear();
        now_ = 0.0;
    }

    WaterWetnessBinding WaterWetnessHistory::BuildBinding(float drySeconds) const
    {
        WaterWetnessBinding binding{};
        if (marks_.empty()) {
            return binding;
        }

        const float bottom = marks_.back().height;
        const float top = (std::max)(marks_.front().height, bottom + kMinTableSpan);
        const double invDrySeconds = 1.0 / ClampDrySeconds(drySeconds);
        binding.enabled = 1;
        binding.bottomHeight = bottom;
        binding.samplesPerMeter = static_cast<float>(kWaterWetnessSampleCount - 1) / (top - bottom);

        // 標本は下から上へ、印は後ろから前へ進む
        size_t upper = marks_.size() - 1;
        for (uint32_t sample = 0; sample < kWaterWetnessSampleCount; ++sample) {
            const float height = bottom + static_cast<float>(sample) / binding.samplesPerMeter;
            if (height > marks_.front().height) {
                binding.wetness[sample] = 0.0f;
                continue;
            }
            while (marks_[upper].height < height) {
                --upper;
            }
            const double elapsed = now_ - LastSubmergedTime(upper, height);
            binding.wetness[sample] = static_cast<float>(std::exp(-elapsed * invDrySeconds));
        }
        return binding;
    }

    double WaterWetnessHistory::LastSubmergedTime(size_t upper, float height) const
    {
        if (upper + 1 >= marks_.size()) {
            return marks_[upper].time;
        }
        // upper と次の印の間を、水面は高さに比例した時刻で下がったとみなす
        const Mark& above = marks_[upper];
        const Mark& below = marks_[upper + 1];
        const double t = static_cast<double>(above.height - height) / static_cast<double>(above.height - below.height);
        return above.time + (below.time - above.time) * t;
    }

    size_t WaterWetnessHistory::FindUpperMark(float height) const
    {
        for (size_t i = marks_.size(); i-- > 0;) {
            if (marks_[i].height > height) {
                return i;
            }
        }
        return marks_.size();
    }
}
