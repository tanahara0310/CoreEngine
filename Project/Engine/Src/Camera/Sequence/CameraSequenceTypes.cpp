#include "pch.h"
#include "CameraSequenceTypes.h"

#include <algorithm>

namespace CoreEngine
{
    void CameraSequenceAsset::SortKeyframes()
    {
        std::sort(keyframes.begin(), keyframes.end(),
            [](const CameraSequenceKeyframe& lhs, const CameraSequenceKeyframe& rhs) {
                return lhs.time < rhs.time;
            });
    }

    void CameraSequenceAsset::SortEvents()
    {
        std::sort(events.begin(), events.end(),
            [](const CameraSequenceEvent& lhs, const CameraSequenceEvent& rhs) {
                return lhs.time < rhs.time;
            });
    }

    void CameraSequenceAsset::Sanitize()
    {
        if (timelineLength < kMinTimelineLength) {
            timelineLength = kMinTimelineLength;
        }

        for (auto& key : keyframes) {
            key.time = std::clamp(key.time, 0.0f, timelineLength);
        }

        for (auto& event : events) {
            event.time = std::clamp(event.time, 0.0f, timelineLength);
            if (event.duration < 0.0f) {
                event.duration = 0.0f;
            }
        }

        // 開始をタイムライン終端まで許すと、終了を開始より後に置く余地が無くなる。
        // 先に開始の上限を「終端 - 最小長」まで引いておく。
        const float maxShotStart = (std::max)(timelineLength - kMinShotDuration, 0.0f);

        for (auto& shot : shots) {
            shot.startTime = std::clamp(shot.startTime, 0.0f, maxShotStart);
            shot.endTime = std::clamp(shot.endTime, 0.0f, timelineLength);

            // 終了が開始以下だと区間の長さがゼロ以下になり、
            // 「この時刻はどのショットか」の判定とブレンド長のクランプが破綻する。
            if (shot.endTime <= shot.startTime) {
                shot.endTime = (std::min)(shot.startTime + kMinShotDuration, timelineLength);
            }

            if (shot.blendDuration < 0.0f) {
                shot.blendDuration = 0.0f;
            }
        }
    }
}
