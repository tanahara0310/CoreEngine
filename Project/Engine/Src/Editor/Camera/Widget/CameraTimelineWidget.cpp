#include "pch.h"
#include "CameraTimelineWidget.h"

#ifdef USE_IMGUI

#include "Editor/ImGui/ImGuiAll.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace CoreEngine
{
    namespace
    {
        // ===== 寸法 =====
        constexpr float kGutterWidth = 62.0f;   // レーン名を出す左端の幅
        constexpr float kRulerHeight = 20.0f;
        constexpr float kLaneHeight = 30.0f;
        constexpr int   kLaneCount = 3;         // キー / ショット / イベント
        constexpr float kKeyRadius = 6.0f;
        constexpr float kGrabPixels = 8.0f;     // キーを掴める距離
        constexpr float kMinViewDuration = 0.2f;

        // ===== 色 =====
        // 役割ごとに色を分ける。形だけだと、詰まったときに何のレーンか読めない。
        const ImU32 kColorBackground = IM_COL32(16, 16, 16, 255);
        const ImU32 kColorLaneAlt = IM_COL32(26, 26, 26, 255);
        const ImU32 kColorBorder = IM_COL32(58, 58, 66, 255);
        const ImU32 kColorGrid = IM_COL32(44, 48, 56, 255);
        const ImU32 kColorGridMajor = IM_COL32(70, 76, 88, 255);
        const ImU32 kColorText = IM_COL32(150, 156, 166, 255);
        const ImU32 kColorLaneLabel = IM_COL32(120, 126, 136, 255);
        const ImU32 kColorKey = IM_COL32(66, 150, 250, 255);
        const ImU32 kColorKeySelected = IM_COL32(240, 180, 64, 255);
        const ImU32 kColorKeyOutline = IM_COL32(13, 13, 13, 255);
        const ImU32 kColorShot = IM_COL32(185, 116, 58, 255);
        const ImU32 kColorShotAlt = IM_COL32(138, 106, 166, 255);
        const ImU32 kColorShotDisabled = IM_COL32(80, 80, 84, 255);
        const ImU32 kColorEvent = IM_COL32(217, 86, 78, 255);
        const ImU32 kColorEventDisabled = IM_COL32(90, 78, 78, 255);
        const ImU32 kColorPlayhead = IM_COL32(232, 97, 90, 255);

        /// @brief 目盛りの間隔を、画面上で詰まりすぎない値から選ぶ
        float ChooseTickStep(float viewDuration, float pixelWidth)
        {
            // 目盛り 1 本あたり最低 60px は空けたい。刻みは 1/2/5 系列で選ぶ。
            constexpr float kMinPixelsPerTick = 60.0f;
            const float minStep = viewDuration * (kMinPixelsPerTick / (std::max)(pixelWidth, 1.0f));

            constexpr float kSteps[] = {
                0.01f, 0.02f, 0.05f, 0.1f, 0.2f, 0.5f,
                1.0f, 2.0f, 5.0f, 10.0f, 20.0f, 30.0f, 60.0f, 120.0f, 300.0f
            };
            for (const float step : kSteps) {
                if (step >= minStep) {
                    return step;
                }
            }
            return kSteps[sizeof(kSteps) / sizeof(kSteps[0]) - 1];
        }

        /// @brief 値をスナップ間隔へ丸める
        float ApplySnap(float value, float snapSeconds)
        {
            if (snapSeconds <= 0.0f) {
                return value;
            }
            return std::round(value / snapSeconds) * snapSeconds;
        }
    }

    void CameraTimelineWidget::ResetView()
    {
        viewStart_ = 0.0f;
        viewDuration_ = 0.0f;
    }

    void CameraTimelineWidget::ClampView(float duration)
    {
        if (viewDuration_ <= 0.0f) {
            // 全体表示
            viewStart_ = 0.0f;
            return;
        }

        viewDuration_ = std::clamp(viewDuration_, kMinViewDuration, duration);
        viewStart_ = std::clamp(viewStart_, 0.0f, (std::max)(duration - viewDuration_, 0.0f));
    }

    CameraTimelineWidget::Result CameraTimelineWidget::Draw(CameraSequenceAsset& sequence,
        float& playhead, int selectedKeyframe, int selectedShot, int selectedEvent,
        float snapSeconds)
    {
        Result result{};
        result.selectedKeyframe = selectedKeyframe;
        result.selectedShot = selectedShot;
        result.selectedEvent = selectedEvent;

        const float duration = (std::max)(sequence.timelineLength, CameraSequenceAsset::kMinTimelineLength);
        ClampView(duration);

        const float visibleStart = (viewDuration_ > 0.0f) ? viewStart_ : 0.0f;
        const float visibleDuration = (viewDuration_ > 0.0f) ? viewDuration_ : duration;

        const float totalHeight = kRulerHeight + kLaneHeight * kLaneCount;
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const float width = (std::max)(ImGui::GetContentRegionAvail().x, 240.0f);

        ImGui::InvisibleButton("##CameraTimeline", ImVec2(width, totalHeight),
            ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);

        const bool hovered = ImGui::IsItemHovered();
        ImDrawList* draw = ImGui::GetWindowDrawList();

        const float trackLeft = origin.x + kGutterWidth;
        const float trackRight = origin.x + width;
        const float trackWidth = (std::max)(trackRight - trackLeft, 1.0f);

        // 時刻 ⇔ 画面座標
        const auto timeToX = [&](float time) {
            const float normalized = (time - visibleStart) / visibleDuration;
            return trackLeft + normalized * trackWidth;
        };
        const auto xToTime = [&](float x) {
            const float normalized = (x - trackLeft) / trackWidth;
            return visibleStart + normalized * visibleDuration;
        };

        // ===== 背景 =====
        draw->AddRectFilled(origin, ImVec2(trackRight, origin.y + totalHeight), kColorBackground, 3.0f);

        const float rulerBottom = origin.y + kRulerHeight;
        for (int lane = 0; lane < kLaneCount; ++lane) {
            // 1 本おきに明度を変えて、レーンの境目を線ではなく面で示す
            if (lane % 2 == 0) {
                continue;
            }
            const float top = rulerBottom + kLaneHeight * lane;
            draw->AddRectFilled(ImVec2(origin.x, top), ImVec2(trackRight, top + kLaneHeight), kColorLaneAlt);
        }

        // ===== 目盛り =====
        const float tickStep = ChooseTickStep(visibleDuration, trackWidth);
        const float firstTick = std::ceil(visibleStart / tickStep) * tickStep;
        for (float t = firstTick; t <= visibleStart + visibleDuration + 1.0e-4f; t += tickStep) {
            const float x = timeToX(t);
            if (x < trackLeft - 1.0f || x > trackRight + 1.0f) {
                continue;
            }

            draw->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + totalHeight), kColorGrid, 1.0f);

            char label[32]{};
            // 刻みが細かいときだけ小数を出す。1 秒以上なら整数のほうが読みやすい。
            std::snprintf(label, sizeof(label), (tickStep < 1.0f) ? "%.2f" : "%.0f", t);
            draw->AddText(ImVec2(x + 3.0f, origin.y + 3.0f), kColorText, label);
        }

        // 端（0 と終端）は必ず濃い線で出す。どこが範囲外か分かるように。
        draw->AddLine(ImVec2(timeToX(0.0f), origin.y), ImVec2(timeToX(0.0f), origin.y + totalHeight), kColorGridMajor, 1.5f);
        draw->AddLine(ImVec2(timeToX(duration), origin.y), ImVec2(timeToX(duration), origin.y + totalHeight), kColorGridMajor, 1.5f);

        // ===== レーン名 =====
        draw->AddRectFilled(origin, ImVec2(trackLeft, origin.y + totalHeight), kColorBackground);
        static const char* kLaneNames[kLaneCount] = { "キー", "ショット", "イベント" };
        for (int lane = 0; lane < kLaneCount; ++lane) {
            const float centerY = rulerBottom + kLaneHeight * lane + kLaneHeight * 0.5f;
            draw->AddText(ImVec2(origin.x + 6.0f, centerY - 7.0f), kColorLaneLabel, kLaneNames[lane]);
        }
        draw->AddLine(ImVec2(trackLeft, origin.y), ImVec2(trackLeft, origin.y + totalHeight), kColorBorder, 1.0f);

        // ===== ショットレーン =====
        const float shotTop = rulerBottom + kLaneHeight;
        for (int i = 0; i < static_cast<int>(sequence.shots.size()); ++i) {
            const CameraSequenceShot& shot = sequence.shots[i];
            const float left = (std::max)(timeToX(shot.startTime), trackLeft);
            const float right = (std::min)(timeToX(shot.endTime), trackRight);
            if (right <= left) {
                continue;
            }

            const ImU32 color = !shot.enabled ? kColorShotDisabled
                : ((i % 2 == 0) ? kColorShot : kColorShotAlt);
            draw->AddRectFilled(ImVec2(left, shotTop + 5.0f), ImVec2(right, shotTop + kLaneHeight - 5.0f), color, 2.0f);

            if (i == selectedShot) {
                draw->AddRect(ImVec2(left, shotTop + 5.0f), ImVec2(right, shotTop + kLaneHeight - 5.0f),
                    kColorKeySelected, 2.0f, 0, 2.0f);
            }

            // ブレンド区間は帯の中に濃い縁で示す（どこで繋がるのかを帯だけで読ませる）
            if (shot.transitionType == CameraSequenceTransitionType::Blend && shot.blendDuration > 0.0f) {
                const float blendRight = (std::min)(timeToX(shot.startTime + shot.blendDuration), right);
                if (blendRight > left) {
                    draw->AddRectFilled(ImVec2(left, shotTop + 5.0f), ImVec2(blendRight, shotTop + kLaneHeight - 5.0f),
                        IM_COL32(0, 0, 0, 90), 2.0f);
                }
            }

            if (!shot.name.empty() && (right - left) > 24.0f) {
                draw->PushClipRect(ImVec2(left + 3.0f, shotTop), ImVec2(right - 2.0f, shotTop + kLaneHeight), true);
                draw->AddText(ImVec2(left + 5.0f, shotTop + 8.0f), IM_COL32(26, 18, 8, 255), shot.name.c_str());
                draw->PopClipRect();
            }
        }

        // ===== イベントレーン =====
        const float eventTop = rulerBottom + kLaneHeight * 2.0f;
        const float eventCenterY = eventTop + kLaneHeight * 0.5f;
        for (int i = 0; i < static_cast<int>(sequence.events.size()); ++i) {
            const CameraSequenceEvent& event = sequence.events[i];
            const float x = timeToX(event.time);
            if (x < trackLeft - 6.0f || x > trackRight + 6.0f) {
                continue;
            }

            const ImU32 color = event.enabled ? kColorEvent : kColorEventDisabled;
            // 上下に伸びる旗の形にして、菱形のキーと一目で見分けられるようにする
            draw->AddLine(ImVec2(x, eventTop + 4.0f), ImVec2(x, eventTop + kLaneHeight - 4.0f), color, 2.0f);
            draw->AddTriangleFilled(
                ImVec2(x, eventTop + 4.0f),
                ImVec2(x + 10.0f, eventTop + 9.0f),
                ImVec2(x, eventTop + 14.0f), color);

            if (i == selectedEvent) {
                draw->AddCircle(ImVec2(x, eventCenterY), 8.0f, kColorKeySelected, 0, 2.0f);
            }
        }

        // ===== キーレーン =====
        const float keyCenterY = rulerBottom + kLaneHeight * 0.5f;
        for (int i = 0; i < static_cast<int>(sequence.keyframes.size()); ++i) {
            const float x = timeToX(sequence.keyframes[i].time);
            if (x < trackLeft - kKeyRadius || x > trackRight + kKeyRadius) {
                continue;
            }

            const bool selected = (i == selectedKeyframe);
            const float radius = selected ? kKeyRadius + 1.5f : kKeyRadius;
            const ImU32 color = selected ? kColorKeySelected : kColorKey;

            // 菱形（回転した四角）。丸だとイベントの旗と紛らわしい。
            const ImVec2 diamond[4] = {
                ImVec2(x, keyCenterY - radius), ImVec2(x + radius, keyCenterY),
                ImVec2(x, keyCenterY + radius), ImVec2(x - radius, keyCenterY)
            };
            draw->AddConvexPolyFilled(diamond, 4, color);
            draw->AddPolyline(diamond, 4, kColorKeyOutline, ImDrawFlags_Closed, 1.5f);
        }

        // ===== 再生ヘッド =====
        {
            const float x = timeToX(playhead);
            if (x >= trackLeft - 6.0f && x <= trackRight + 6.0f) {
                draw->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + totalHeight), kColorPlayhead, 2.0f);
                draw->AddTriangleFilled(
                    ImVec2(x - 6.0f, origin.y), ImVec2(x + 6.0f, origin.y),
                    ImVec2(x, origin.y + 8.0f), kColorPlayhead);
            }
        }

        draw->AddRect(origin, ImVec2(trackRight, origin.y + totalHeight), kColorBorder, 3.0f);

        // ===== 入力 =====
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const bool inTrack = hovered && mouse.x >= trackLeft;

        // ホイールでズーム（カーソル位置の時刻を固定したまま拡大縮小する）
        if (inTrack && ImGui::GetIO().MouseWheel != 0.0f) {
            const float anchorTime = xToTime(mouse.x);
            const float zoom = (ImGui::GetIO().MouseWheel > 0.0f) ? 0.8f : 1.25f;

            const float current = (viewDuration_ > 0.0f) ? viewDuration_ : duration;
            float next = std::clamp(current * zoom, kMinViewDuration, duration);

            const float ratio = std::clamp((mouse.x - trackLeft) / trackWidth, 0.0f, 1.0f);
            viewDuration_ = next;
            viewStart_ = anchorTime - ratio * next;
            ClampView(duration);
        }

        // 中ドラッグで横スクロール
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Middle) && viewDuration_ > 0.0f) {
            const float deltaTime = ImGui::GetIO().MouseDelta.x / trackWidth * viewDuration_;
            viewStart_ -= deltaTime;
            ClampView(duration);
        }

        // ダブルクリックで全体表示へ戻す
        if (inTrack && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            ResetView();
        }

        // 押した瞬間に「何を掴んだか」を決める
        if (inTrack && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            const float clickTime = xToTime(mouse.x);

            // キーレーンの上ならキーを掴む
            int grabbed = -1;
            if (mouse.y >= rulerBottom && mouse.y < rulerBottom + kLaneHeight) {
                float best = kGrabPixels;
                for (int i = 0; i < static_cast<int>(sequence.keyframes.size()); ++i) {
                    const float distance = std::fabs(timeToX(sequence.keyframes[i].time) - mouse.x);
                    if (distance < best) {
                        best = distance;
                        grabbed = i;
                    }
                }
            }

            if (grabbed >= 0) {
                draggingKeyframe_ = grabbed;
                result.selectedKeyframe = grabbed;
                result.selectionChanged = true;
                result.keyDragStarted = true;
            } else if (mouse.y >= rulerBottom + kLaneHeight && mouse.y < rulerBottom + kLaneHeight * 2.0f) {
                // ショットレーン
                for (int i = 0; i < static_cast<int>(sequence.shots.size()); ++i) {
                    if (clickTime >= sequence.shots[i].startTime && clickTime <= sequence.shots[i].endTime) {
                        result.selectedShot = i;
                        result.selectionChanged = true;
                        break;
                    }
                }
            } else if (mouse.y >= rulerBottom + kLaneHeight * 2.0f) {
                // イベントレーン
                float best = kGrabPixels + 4.0f;
                for (int i = 0; i < static_cast<int>(sequence.events.size()); ++i) {
                    const float distance = std::fabs(timeToX(sequence.events[i].time) - mouse.x);
                    if (distance < best) {
                        best = distance;
                        result.selectedEvent = i;
                        result.selectionChanged = true;
                    }
                }
            } else {
                // 目盛りの上は再生ヘッドの操作
                draggingPlayhead_ = true;
            }
        }

        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            draggingKeyframe_ = -1;
            draggingPlayhead_ = false;
        }

        // キーのドラッグ
        if (draggingKeyframe_ >= 0 && draggingKeyframe_ < static_cast<int>(sequence.keyframes.size())
            && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            const float next = std::clamp(ApplySnap(xToTime(mouse.x), snapSeconds), 0.0f, duration);
            if (next != sequence.keyframes[draggingKeyframe_].time) {
                sequence.keyframes[draggingKeyframe_].time = next;
                result.keyTimeChanged = true;
                result.selectedKeyframe = draggingKeyframe_;
            }
            // 動かしているキーの時刻へ再生ヘッドも連れて行く。構図を見ながら詰められる。
            playhead = next;
            result.playheadChanged = true;
        }

        // 再生ヘッドのドラッグ（押した位置から連続でスクラブできる）
        if (draggingPlayhead_ && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const float next = std::clamp(ApplySnap(xToTime(mouse.x), snapSeconds), 0.0f, duration);
            if (next != playhead) {
                playhead = next;
                result.playheadChanged = true;
            }
        }

        return result;
    }
}

#endif // USE_IMGUI
