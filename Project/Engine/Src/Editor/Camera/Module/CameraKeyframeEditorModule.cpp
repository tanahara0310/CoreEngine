#include "pch.h"
#include "CameraKeyframeEditorModule.h"
#include "Camera/Sequence/CameraSequenceEvaluator.h"
#include "Camera/Sequence/CameraSequenceIO.h"

#ifdef USE_IMGUI

#include "Editor/ImGui/ImGuiAll.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

#include "Camera/CameraManager.h"
#include "Camera/Camera.h"
#include "Camera/Shake/CameraShake.h"
#include "GameObject/GameObject.h"
#include "GameObject/GameObjectManager.h"
#include "Editor/ImGui/Gizmo.h"
#include "Graphics/Line/LineManager.h"
#include "Utility/JsonManager/JsonManager.h"

namespace CoreEngine
{
    namespace
    {
        // 遷移方式の表示名。CameraSequenceTransitionType の値をそのまま添字に使う。
        constexpr const char* kShotTransitionLabels[] = {
            "カット",
            "ブレンド"
        };

        constexpr int kShotTransitionLabelCount = static_cast<int>(sizeof(kShotTransitionLabels) / sizeof(kShotTransitionLabels[0]));

        // 補間方式の表示名。CameraSequenceInterpolation の値をそのまま添字に使う。
        constexpr const char* kInterpolationLabels[] = {
            "ステップ (補間しない)",
            "直線",
            "スムーズ (Catmull-Rom)"
        };

        constexpr int kInterpolationLabelCount = static_cast<int>(sizeof(kInterpolationLabels) / sizeof(kInterpolationLabels[0]));

        // 向きの決め方の表示名。CameraSequenceAimMode の値をそのまま添字に使う。
        constexpr const char* kAimModeLabels[] = {
            "キーの回転を使う",
            "注視点を向く",
            "オブジェクトを向く"
        };

        constexpr int kAimModeLabelCount = static_cast<int>(sizeof(kAimModeLabels) / sizeof(kAimModeLabels[0]));

        // イベント種別の表示名。CameraSequenceEventType の値をそのまま添字に使う。
        constexpr const char* kEventTypeLabels[] = {
            "シェイク",
            "trauma 加算",
            "コールバック",
            "時間スケール"
        };

        constexpr int kEventTypeLabelCount = static_cast<int>(sizeof(kEventTypeLabels) / sizeof(kEventTypeLabels[0]));

        constexpr float kRadToDeg = 180.0f / 3.14159265358979323846f;
        constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
    }

    void CameraKeyframeEditorModule::Update(const CameraEditorContext& context)
    {
        if (!context.cameraManager || !isPlaying_ || sequence_.keyframes.size() < 2) {
            UpdateAutoKey(context);
            DrawViewportVisualization(context);
            return;
        }

        // 再生ヘッドを進め、補間結果を現在カメラへ適用する。
        playhead_ += ImGui::GetIO().DeltaTime * playbackSpeed_;

        if (playhead_ > sequence_.timelineLength) {
            if (loopPlayback_) {
                playhead_ = 0.0f;
            } else {
                playhead_ = sequence_.timelineLength;
                isPlaying_ = false;
            }
        }

        ApplyEvaluatedAt(context, playhead_);

        UpdateAutoKey(context);
        DrawViewportVisualization(context);
    }

    void CameraKeyframeEditorModule::Draw(const CameraEditorContext& context)
    {
        if (!context.cameraManager) {
            return;
        }

        // 掴んでいたウィジェットが消えた（タブを切り替えた・キーを選び直した）ときの
        // 取り残しを掃除する。ここは今フレームのウィジェットを出す前なので、
        // ActiveId は前フレームの続きを正しく指している。
        if (!ImGui::IsAnyItemActive()) {
            activeEditItemId_ = 0;
            hasPendingEditState_ = false;
        }

        // Ctrl+Z / Ctrl+Y はシーンのオブジェクト Undo と同じキー。素で拾うと両方が
        // 同じフレームで戻ってしまうので、ImGui の入力ルーティングに任せる。
        // RouteFocused はこのウィンドウにフォーカスがあるときだけ勝ち、
        // それ以外は SceneDebugEditor 側の RouteGlobal へ流れる。
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z, ImGuiInputFlags_RouteFocused)) {
            Undo();
        }
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y, ImGuiInputFlags_RouteFocused)) {
            Redo();
        }

        if (needRefreshClipFileList_) {
            RefreshClipFileList();
        }

        bool playheadChanged = false;

        DrawTransport(playheadChanged);
        DrawTimeline(playheadChanged);

        UI::Spacing();

        // 左＝キーの出し入れ、右＝選んだキーの中身。この 2 つを並べるのが要点で、
        // 「キーを打つ」と「値を直す」の往復でスクロールが要らなくなる。
        const float listWidth = 208.0f;
        if (auto list = UI::Scope::ChildScope("##KeyList", ImVec2(listWidth, 232.0f), ImGuiChildFlags_Border)) {
            DrawKeyList(context);
        }

        UI::SameLine();

        if (auto inspector = UI::Scope::ChildScope("##KeyInspector", ImVec2(0.0f, 232.0f), ImGuiChildFlags_Border)) {
            DrawKeyInspector(context, playheadChanged);
        }

        UI::Spacing();

        // 使う頻度が低いものはタブの奥へ。毎回使う操作の前に置かない。
        if (ImGui::BeginTabBar("##KeyframeSubTabs", ImGuiTabBarFlags_None)) {
            if (ImGui::BeginTabItem("ショット")) {
                DrawShotTrack();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("イベント")) {
                DrawEventTrack();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("シーケンス")) {
                DrawSequenceAssets(context);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("表示")) {
                DrawViewSettings();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        DrawStatusBar();

        // 再生停止中に再生ヘッドが動いたら、その時刻の構図を即座に見せる。
        if (!isPlaying_ && playheadChanged) {
            ApplyEvaluatedAt(context, playhead_);
        }
    }

    void CameraKeyframeEditorModule::DrawTransport(bool& playheadChanged)
    {
        // 再生と時刻。いちばん使うので最上段に固定し、他を開閉しても位置が動かないようにする。
        if (isPlaying_) {
            if (ImGui::Button("■ 停止", ImVec2(72.0f, 0.0f))) {
                isPlaying_ = false;
            }
        } else {
            if (ImGui::Button("▶ 再生", ImVec2(72.0f, 0.0f))) {
                isPlaying_ = true;
            }
        }

        UI::SameLine();
        if (ImGui::Button("|◀ 先頭")) {
            playhead_ = 0.0f;
            playheadChanged = true;
        }

        UI::SameLine();
        if (ImGui::Button("◀ 前キー")) {
            const int prev = FindPreviousKeyframeIndex(playhead_ - updateThreshold_);
            if (prev >= 0) {
                selectedIndex_ = prev;
                playhead_ = sequence_.keyframes[prev].time;
                playheadChanged = true;
            }
        }

        UI::SameLine();
        if (ImGui::Button("次キー ▶")) {
            const int next = FindNextKeyframeIndex(playhead_ + updateThreshold_);
            if (next >= 0) {
                selectedIndex_ = next;
                playhead_ = sequence_.keyframes[next].time;
                playheadChanged = true;
            }
        }

        UI::SameLine();
        ImGui::TextColored(ImVec4(0.94f, 0.71f, 0.25f, 1.0f), "%6.2f", playhead_);
        UI::SameLine();
        ImGui::TextDisabled("/ %.2f 秒", sequence_.timelineLength);

        // 2 行目：頻度は落ちるが、再生の性格を決める設定
        ImGui::SetNextItemWidth(90.0f);
        UI::DragFloat("速度", playbackSpeed_, 0.05f, 0.1f, 4.0f, "%.2fx");

        UI::SameLine();
        UI::Widgets::ToggleSwitch("ループ", &loopPlayback_);

        UI::SameLine();
        ImGui::SetNextItemWidth(90.0f);
        UI::DragFloat("スナップ", snapSeconds_, 0.01f, 0.0f, 1.0f, "%.2f 秒");
        snapSeconds_ = std::clamp(snapSeconds_, 0.0f, 1.0f);
        UI::SameLine();
        UI::Hint("キーをドラッグしたときに丸める間隔。0 で無効。");

        UI::SameLine();
        ImGui::SetNextItemWidth(100.0f);
        if (TrackEdit(UI::DragFloat("全体長", sequence_.timelineLength, 0.1f, 0.1f, 600.0f, "%.1f 秒"))) {
            sequence_.timelineLength = (std::max)(sequence_.timelineLength, CameraSequenceAsset::kMinTimelineLength);
        }

        UI::SameLine();
        if (ImGui::SmallButton("Undo")) {
            Undo();
        }
        UI::SameLine();
        if (ImGui::SmallButton("Redo")) {
            Redo();
        }
        UI::SameLine();
        UI::Widgets::ToggleSwitch("Auto Key", &autoKeyEnabled_);
        UI::SameLine();
        UI::Hint("カメラを動かすと選択中の時刻へ自動でキーを作る");
    }

    void CameraKeyframeEditorModule::DrawTimeline(bool& playheadChanged)
    {
        const CameraTimelineWidget::Result result = timeline_.Draw(
            sequence_, playhead_, selectedIndex_, selectedShotIndex_, selectedEventIndex_, snapSeconds_);

        if (result.keyDragStarted) {
            // 掴んだ瞬間に 1 回だけ履歴を積む。ドラッグ中に積むと履歴が埋まる。
            PushUndoState();
        }

        if (result.selectionChanged) {
            selectedIndex_ = result.selectedKeyframe;
            selectedShotIndex_ = result.selectedShot;
            selectedEventIndex_ = result.selectedEvent;
            editingShotNameIndex_ = -1;
            editingEventNameIndex_ = -1;
        }

        if (result.keyTimeChanged) {
            // 並べ替えると添字がずれるので、動かしているキーを時刻で追い直す。
            const float movedTime = sequence_.keyframes[selectedIndex_].time;
            sequence_.SortKeyframes();
            selectedIndex_ = FindNearestKeyframeIndex(movedTime);
        }

        if (result.playheadChanged) {
            playheadChanged = true;
        }

        UI::Hint("キーをドラッグで移動 / 目盛りをドラッグでスクラブ / ホイールでズーム / 中ドラッグで横移動 / ダブルクリックで全体表示");
    }

    void CameraKeyframeEditorModule::DrawKeyList(const CameraEditorContext& context)
    {
        // 打つ・複製・消すはキー一覧の真上。対象と操作を離さない。
        if (ImGui::Button("＋ キーを打つ", ImVec2(-1.0f, 0.0f))) {
            CameraSnapshot snapshot;
            if (CaptureFromActiveCamera(context, snapshot)) {
                PushUndoState();
                const int nearest = FindNearestKeyframeIndex(playhead_);
                if (nearest >= 0 && std::fabs(sequence_.keyframes[nearest].time - playhead_) <= updateThreshold_) {
                    sequence_.keyframes[nearest].snapshot = snapshot;
                    selectedIndex_ = nearest;
                } else {
                    CameraSequenceKeyframe key{};
                    key.time = playhead_;
                    key.snapshot = snapshot;
                    sequence_.keyframes.push_back(key);
                    sequence_.SortKeyframes();
                    selectedIndex_ = FindNearestKeyframeIndex(playhead_);
                }
            }
        }
        UI::Hint("今のカメラの構図を再生ヘッドの時刻へ記録します。");

        const bool hasSelection = (selectedIndex_ >= 0 && selectedIndex_ < static_cast<int>(sequence_.keyframes.size()));

        if (ImGui::SmallButton("複製") && hasSelection) {
            PushUndoState();
            CameraSequenceKeyframe duplicated = sequence_.keyframes[selectedIndex_];
            duplicated.time = std::clamp(duplicated.time + 0.2f, 0.0f, sequence_.timelineLength);
            sequence_.keyframes.push_back(duplicated);
            sequence_.SortKeyframes();
            selectedIndex_ = FindNearestKeyframeIndex(duplicated.time);
            playhead_ = duplicated.time;
        }

        UI::SameLine();
        if (ImGui::SmallButton("削除") && hasSelection) {
            PushUndoState();
            sequence_.keyframes.erase(sequence_.keyframes.begin() + selectedIndex_);
            selectedIndex_ = sequence_.keyframes.empty()
                ? -1
                : std::clamp(selectedIndex_, 0, static_cast<int>(sequence_.keyframes.size()) - 1);
        }

        UI::SameLine();
        if (ImGui::SmallButton("全消去") && !sequence_.keyframes.empty()) {
            PushUndoState();
            sequence_.keyframes.clear();
            selectedIndex_ = -1;
        }

        UI::Separator();

        if (sequence_.keyframes.empty()) {
            UI::Hint("キーがありません。\nカメラを構えて「キーを打つ」。");
            return;
        }

        for (int i = 0; i < static_cast<int>(sequence_.keyframes.size()); ++i) {
            const CameraSequenceKeyframe& key = sequence_.keyframes[i];

            // 名前があれば名前で、無ければ時刻で。番号を頭に出して順番を読めるようにする。
            char label[192]{};
            if (key.label.empty()) {
                std::snprintf(label, sizeof(label), "%2d  %6.2f 秒##key%d", i + 1, key.time, i);
            } else {
                std::snprintf(label, sizeof(label), "%2d  %s##key%d", i + 1, key.label.c_str(), i);
            }

            if (ImGui::Selectable(label, i == selectedIndex_)) {
                selectedIndex_ = i;
                playhead_ = key.time;
                ApplyEvaluatedAt(context, playhead_);
            }

            // 注視キーには印を付ける。一覧を見ただけで性格が分かるように。
            if (key.aimMode != CameraSequenceAimMode::Euler) {
                UI::SameLine();
                ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.45f, 1.0f), "◎");
            }
        }
    }

    void CameraKeyframeEditorModule::DrawKeyInspector(const CameraEditorContext& context, bool& playheadChanged)
    {
        if (selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(sequence_.keyframes.size())) {
            UI::Hint("キーを選ぶと、ここで構図とつなぎ方を調整できます。");
            return;
        }

        CameraSequenceKeyframe& key = sequence_.keyframes[selectedIndex_];

        // ---- 名前と時刻 ----
        if (editingKeyLabelIndex_ != selectedIndex_) {
            std::snprintf(keyLabelBuffer_, sizeof(keyLabelBuffer_), "%s", key.label.c_str());
            editingKeyLabelIndex_ = selectedIndex_;
        }
        ImGui::SetNextItemWidth(180.0f);
        if (UI::InputText("名前", keyLabelBuffer_, sizeof(keyLabelBuffer_))) {
            key.label = keyLabelBuffer_;
        }

        UI::SameLine();
        ImGui::SetNextItemWidth(110.0f);
        float keyTime = key.time;
        if (TrackEdit(UI::DragFloat("時刻", keyTime, 0.02f, 0.0f, sequence_.timelineLength, "%.2f 秒"))) {
            key.time = std::clamp(keyTime, 0.0f, sequence_.timelineLength);
            const float movedTime = key.time;
            sequence_.SortKeyframes();
            selectedIndex_ = FindNearestKeyframeIndex(movedTime);
            playhead_ = movedTime;
            playheadChanged = true;
            return;
        }

        UI::SameLine();
        if (ImGui::SmallButton("今のカメラから取得")) {
            CameraSnapshot snapshot;
            if (CaptureFromActiveCamera(context, snapshot)) {
                PushUndoState();
                key.snapshot = snapshot;
                playheadChanged = true;
            }
        }

        UI::Separator();

        if (ImGui::BeginTabBar("##KeyInspectorTabs", ImGuiTabBarFlags_None)) {
            if (ImGui::BeginTabItem("構図")) {
                DrawKeyPose(context, key, playheadChanged);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("向き")) {
                DrawKeyAim(context, key, playheadChanged);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("つなぎ")) {
                DrawKeyTransition(key, playheadChanged);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }

    void CameraKeyframeEditorModule::DrawKeyPose(const CameraEditorContext& context,
        CameraSequenceKeyframe& key, bool& playheadChanged)
    {
        // 選んだキーの値を直接いじる。別モジュールへスクロールして戻る必要をなくす。
        Vector3 position = key.snapshot.position;
        if (TrackEdit(UI::DragVec3("位置", position, 0.1f))) {
            key.snapshot.position = position;
            playheadChanged = true;
        }

        if (key.aimMode == CameraSequenceAimMode::Euler) {
            Vector3 rotationDegrees = {
                key.snapshot.rotation.x * kRadToDeg,
                key.snapshot.rotation.y * kRadToDeg,
                key.snapshot.rotation.z * kRadToDeg
            };
            if (TrackEdit(UI::DragVec3("回転 (度)", rotationDegrees, 0.5f, -360.0f, 360.0f))) {
                key.snapshot.rotation = {
                    rotationDegrees.x * kDegToRad,
                    rotationDegrees.y * kDegToRad,
                    rotationDegrees.z * kDegToRad
                };
                playheadChanged = true;
            }
        } else {
            ImGui::TextDisabled("回転は注視から自動計算されます（「向き」タブ）");
        }

        float fovDegrees = key.snapshot.parameters.GetFovDegrees();
        if (TrackEdit(UI::SliderFloat("視野角 (度)", fovDegrees, 10.0f, 120.0f, "%.1f"))) {
            key.snapshot.parameters.SetFovDegrees(fovDegrees);
            playheadChanged = true;
        }

        UI::Spacing();
        if (ImGui::Button("この構図をカメラへ")) {
            ApplyToActiveCamera(context, key.snapshot);
        }
        UI::SameLine();
        if (ImGui::Button("この時刻を見る")) {
            playhead_ = key.time;
            playheadChanged = true;
        }
    }

    void CameraKeyframeEditorModule::DrawKeyAim(const CameraEditorContext& context,
        CameraSequenceKeyframe& key, bool& playheadChanged)
    {
        int aimIndex = static_cast<int>(key.aimMode);
        if (aimIndex < 0 || aimIndex >= kAimModeLabelCount) {
            aimIndex = 0;
        }

        if (ImGui::BeginCombo("注視モード", kAimModeLabels[aimIndex])) {
            for (int i = 0; i < kAimModeLabelCount; ++i) {
                const bool selected = (i == aimIndex);
                if (ImGui::Selectable(kAimModeLabels[i], selected)) {
                    PushUndoState();
                    key.aimMode = static_cast<CameraSequenceAimMode>(i);
                    playheadChanged = true;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        if (key.aimMode == CameraSequenceAimMode::Euler) {
            UI::Hint("キーに保存された回転をそのまま使います。");
            return;
        }

        if (key.aimMode == CameraSequenceAimMode::LookAtPoint) {
            Vector3 aimPoint = key.aimPoint;
            if (TrackEdit(UI::DragVec3("注視点", aimPoint, 0.1f))) {
                key.aimPoint = aimPoint;
                playheadChanged = true;
            }

            if (ImGui::Button("今のカメラの正面を注視点に")) {
                CameraSnapshot current{};
                if (CaptureFromActiveCamera(context, current)) {
                    PushUndoState();
                    const float pitch = current.rotation.x;
                    const float yaw = current.rotation.y;
                    const Vector3 forward = {
                        std::cos(pitch) * std::sin(yaw),
                        -std::sin(pitch),
                        std::cos(pitch) * std::cos(yaw)
                    };
                    key.aimPoint = current.position + forward * 10.0f;
                    key.aimOffset = { 0.0f, 0.0f, 0.0f };
                    playheadChanged = true;
                }
            }
        }

        if (key.aimMode == CameraSequenceAimMode::LookAtObject) {
            if (!context.gameObjectManager) {
                UI::Hint("GameObjectManager が未設定のため対象を選べません。");
            } else {
                const char* preview = key.aimObjectName.empty() ? "未選択" : key.aimObjectName.c_str();
                if (ImGui::BeginCombo("注視オブジェクト", preview)) {
                    for (const auto& object : context.gameObjectManager->GetAllObjects()) {
                        if (!object) {
                            continue;
                        }
                        const std::string& name = object->GetName();
                        const bool selected = (key.aimObjectName == name);
                        if (ImGui::Selectable(name.c_str(), selected)) {
                            PushUndoState();
                            key.aimObjectName = name;
                            playheadChanged = true;
                        }
                        if (selected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }

                if (!key.aimObjectName.empty()) {
                    Vector3 resolved{};
                    const CameraSequenceAimContext probe = MakeAimContext(context);
                    if (!CameraSequenceEvaluator::ResolveAimTarget(key, &probe, resolved)) {
                        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f),
                            "対象が見つかりません。保存された回転で再生されます。");
                    }
                }
            }
        }

        Vector3 aimOffset = key.aimOffset;
        if (TrackEdit(UI::DragVec3("オフセット", aimOffset, 0.05f))) {
            key.aimOffset = aimOffset;
            playheadChanged = true;
        }

        float rollDegrees = key.aimRoll * kRadToDeg;
        if (TrackEdit(UI::SliderFloat("ロール (度)", rollDegrees, -45.0f, 45.0f, "%.1f"))) {
            key.aimRoll = rollDegrees * kDegToRad;
            playheadChanged = true;
        }

        UI::Hint("位置だけ打てば、向きは対象を捉えるよう自動計算されます。");
    }

    void CameraKeyframeEditorModule::DrawKeyTransition(CameraSequenceKeyframe& key, bool& playheadChanged)
    {
        int interpolationIndex = static_cast<int>(key.interpolation);
        if (interpolationIndex < 0 || interpolationIndex >= kInterpolationLabelCount) {
            interpolationIndex = static_cast<int>(CameraSequenceInterpolation::Linear);
        }

        if (ImGui::BeginCombo("補間方式", kInterpolationLabels[interpolationIndex])) {
            for (int i = 0; i < kInterpolationLabelCount; ++i) {
                const bool selected = (i == interpolationIndex);
                if (ImGui::Selectable(kInterpolationLabels[i], selected)) {
                    PushUndoState();
                    key.interpolation = static_cast<CameraSequenceInterpolation>(i);
                    playheadChanged = true;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        UI::Hint("スムーズは前後のキーも見て曲線でつなぎます（キーの上は必ず通ります）。");

        const bool usesDefault = (key.easingTypeIndex == kUseSequenceEasing);
        char defaultLabel[128]{};
        std::snprintf(defaultLabel, sizeof(defaultLabel), "シーケンス既定 (%s)",
            CameraSequenceEasing::LabelAt(sequence_.easingTypeIndex));

        const char* currentEasingLabel = usesDefault
            ? defaultLabel
            : CameraSequenceEasing::LabelAt(key.easingTypeIndex);

        if (ImGui::BeginCombo("この区間の緩急", currentEasingLabel)) {
            if (ImGui::Selectable(defaultLabel, usesDefault)) {
                PushUndoState();
                key.easingTypeIndex = kUseSequenceEasing;
                playheadChanged = true;
            }
            for (int i = 0; i < CameraSequenceEasing::Count(); ++i) {
                const bool selected = (!usesDefault && i == key.easingTypeIndex);
                if (ImGui::Selectable(CameraSequenceEasing::LabelAt(i), selected)) {
                    PushUndoState();
                    key.easingTypeIndex = i;
                    playheadChanged = true;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        UI::Separator();
        ImGui::TextDisabled("シーケンス全体の既定");
        if (sequence_.easingTypeIndex < 0 || sequence_.easingTypeIndex >= CameraSequenceEasing::Count()) {
            sequence_.easingTypeIndex = 0;
        }
        if (ImGui::BeginCombo("既定の緩急", CameraSequenceEasing::LabelAt(sequence_.easingTypeIndex))) {
            for (int i = 0; i < CameraSequenceEasing::Count(); ++i) {
                const bool selected = (i == sequence_.easingTypeIndex);
                if (ImGui::Selectable(CameraSequenceEasing::LabelAt(i), selected)) {
                    PushUndoState();
                    sequence_.easingTypeIndex = i;
                    playheadChanged = true;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
    }

    void CameraKeyframeEditorModule::DrawShotTrack()
    {
        UI::Widgets::ToggleSwitch("ショット遷移を有効", &sequence_.shotsEnabled);
        UI::SameLine();
        UI::Hint("ショットはカット割りの区間。無くてもシーケンスは成立します。");

        if (ImGui::Button("現在位置にショットを追加")) {
            PushUndoState();

            CameraSequenceShot shot{};
            shot.name = "ショット" + std::to_string(sequence_.shots.size() + 1);
            shot.startTime = std::clamp(playhead_ - 0.5f, 0.0f, sequence_.timelineLength);
            shot.endTime = std::clamp(playhead_ + 0.5f, 0.0f, sequence_.timelineLength);
            sequence_.shots.push_back(shot);
            sequence_.Sanitize();
            selectedShotIndex_ = static_cast<int>(sequence_.shots.size()) - 1;
            editingShotNameIndex_ = -1;
        }

        UI::SameLine();
        if (ImGui::Button("選択ショットを削除")) {
            if (selectedShotIndex_ >= 0 && selectedShotIndex_ < static_cast<int>(sequence_.shots.size())) {
                PushUndoState();
                sequence_.shots.erase(sequence_.shots.begin() + selectedShotIndex_);
                selectedShotIndex_ = sequence_.shots.empty()
                    ? -1
                    : std::clamp(selectedShotIndex_, 0, static_cast<int>(sequence_.shots.size()) - 1);
                editingShotNameIndex_ = -1;
            }
        }

        if (sequence_.shots.empty()) {
            UI::Hint("ショットがありません。");
            return;
        }

        selectedShotIndex_ = std::clamp(selectedShotIndex_, -1, static_cast<int>(sequence_.shots.size()) - 1);
        if (selectedShotIndex_ < 0) {
            UI::Hint("タイムラインのショット帯をクリックすると選べます。");
            return;
        }

        CameraSequenceShot& shot = sequence_.shots[selectedShotIndex_];

        if (editingShotNameIndex_ != selectedShotIndex_) {
            std::snprintf(shotNameBuffer_, sizeof(shotNameBuffer_), "%s", shot.name.c_str());
            editingShotNameIndex_ = selectedShotIndex_;
        }
        if (UI::InputText("名前", shotNameBuffer_, sizeof(shotNameBuffer_))) {
            shot.name = shotNameBuffer_;
        }

        bool enabled = shot.enabled;
        if (UI::Widgets::ToggleSwitch("有効", &enabled)) {
            PushUndoState();
            shot.enabled = enabled;
        }

        float startTime = shot.startTime;
        if (TrackEdit(UI::DragFloat("開始", startTime, 0.05f, 0.0f, sequence_.timelineLength, "%.2f 秒"))) {
            shot.startTime = startTime;
            sequence_.Sanitize();
        }

        float endTime = shot.endTime;
        if (TrackEdit(UI::DragFloat("終了", endTime, 0.05f, 0.0f, sequence_.timelineLength, "%.2f 秒"))) {
            shot.endTime = endTime;
            sequence_.Sanitize();
        }

        int transitionIndex = static_cast<int>(shot.transitionType);
        if (transitionIndex < 0 || transitionIndex >= kShotTransitionLabelCount) {
            transitionIndex = 0;
        }
        if (ImGui::BeginCombo("遷移", kShotTransitionLabels[transitionIndex])) {
            for (int i = 0; i < kShotTransitionLabelCount; ++i) {
                const bool selected = (i == transitionIndex);
                if (ImGui::Selectable(kShotTransitionLabels[i], selected)) {
                    PushUndoState();
                    shot.transitionType = static_cast<CameraSequenceTransitionType>(i);
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        if (shot.transitionType == CameraSequenceTransitionType::Blend) {
            float blendDuration = shot.blendDuration;
            if (TrackEdit(UI::DragFloat("ブレンド時間", blendDuration, 0.01f, 0.0f, sequence_.timelineLength, "%.2f 秒"))) {
                shot.blendDuration = (std::max)(blendDuration, 0.0f);
            }
        }
    }

    void CameraKeyframeEditorModule::DrawSequenceAssets(const CameraEditorContext& context)
    {
        UI::Hint("実ゲームで使うデータはこのシーケンス(.json)です。");

        ImGui::SetNextItemWidth(240.0f);
        UI::InputText("シーケンス名", clipFileNameBuffer_, sizeof(clipFileNameBuffer_));

        UI::SameLine();
        if (ImGui::Button("保存")) {
            std::string fileName = clipFileNameBuffer_;
            if (!fileName.empty()) {
                if (fileName.find(".json") == std::string::npos) {
                    fileName += ".json";
                }
                JsonManager::GetInstance().CreateJsonDirectory(clipDirectoryPath_);
                const std::filesystem::path fullPath = std::filesystem::path(clipDirectoryPath_) / fileName;
                if (SaveCurrentClipToFile(fullPath.string())) {
                    needRefreshClipFileList_ = true;
                }
            }
        }

        UI::SameLine();
        if (ImGui::Button("一覧更新")) {
            needRefreshClipFileList_ = true;
        }

        if (clipFileList_.empty()) {
            UI::Hint("保存済みシーケンスがありません。");
            return;
        }

        selectedClipFileIndex_ = std::clamp(selectedClipFileIndex_, -1, static_cast<int>(clipFileList_.size()) - 1);

        if (auto lb = UI::Scope::ListBoxScope("##ClipFiles", ImVec2(-1.0f, 96.0f))) {
            for (int i = 0; i < static_cast<int>(clipFileList_.size()); ++i) {
                if (ImGui::Selectable(clipFileList_[i].c_str(), selectedClipFileIndex_ == i)) {
                    selectedClipFileIndex_ = i;
                }
            }
        }

        if (selectedClipFileIndex_ >= 0 && selectedClipFileIndex_ < static_cast<int>(clipFileList_.size())) {
            if (ImGui::Button("選択シーケンスを読み込み")) {
                const std::filesystem::path fullPath =
                    std::filesystem::path(clipDirectoryPath_) / clipFileList_[selectedClipFileIndex_];
                PushUndoState();
                if (LoadClipFromFile(fullPath.string())) {
                    timeline_.ResetView();
                    ApplyEvaluatedAt(context, playhead_);
                } else if (!undoStack_.empty()) {
                    undoStack_.pop_back();
                }
            }
        }
    }

    void CameraKeyframeEditorModule::DrawViewSettings()
    {
        // 一度決めたら触らない設定。毎回使う操作より前には置かない。
        UI::Widgets::ToggleSwitch("可視化を有効", &viewportVisualizationEnabled_);
        UI::Widgets::ToggleSwitch("カメラ軌跡", &viewportShowTrajectory_);
        UI::Widgets::ToggleSwitch("キーフレーム位置", &viewportShowKeyMarkers_);
        UI::Widgets::ToggleSwitch("注視先への線", &viewportShowDebugTarget_);
        UI::Widgets::ToggleSwitch("視錐台", &viewportShowFrustum_);
        UI::Widgets::ToggleSwitch("再生ヘッドの視錐台", &viewportShowPlayheadFrustum_);
        UI::SliderFloat("非選択キーの濃さ", viewportFrustumIdleAlpha_, 0.1f, 1.0f, "%.2f");
        UI::Widgets::ToggleSwitch("ビューポートのギズモ", &viewportGizmoEnabled_);
        UI::Widgets::ToggleSwitch("カメラアイコン", &viewportShowIcons_);
        UI::SliderFloat("アイコンの大きさ", viewportIconScale_, 0.5f, 2.5f, "%.2f");
        UI::DragFloat("視錐台の長さ", viewportFrustumLength_, 0.1f, 0.5f, 100.0f, "%.1f m");

        UI::DragInt("軌跡サンプル/区間", viewportTrajectorySamplesPerSegment_, 1.0f, 2, 64);
        UI::DragFloat("マーカーサイズ", viewportMarkerSize_, 0.01f, 0.02f, 2.0f, "%.2f");
        UI::SliderFloat("軌跡アルファ", viewportTrajectoryAlpha_, 0.1f, 1.0f, "%.2f");

        viewportTrajectorySamplesPerSegment_ = std::clamp(viewportTrajectorySamplesPerSegment_, 2, 64);
        viewportMarkerSize_ = std::clamp(viewportMarkerSize_, 0.02f, 2.0f);

        if (ImGui::TreeNode("色")) {
            UI::ColorEdit3("軌跡", viewportTrajectoryColor_);
            UI::ColorEdit3("キー", viewportKeyMarkerColor_);
            UI::ColorEdit3("選択キー", viewportSelectedKeyColor_);
            UI::ColorEdit3("注視先", viewportDebugTargetColor_);
            ImGui::TreePop();
        }
    }

    void CameraKeyframeEditorModule::DrawStatusBar()
    {
        UI::Separator();
        ImGui::TextDisabled("キー %d   ショット %d   イベント %d   |   %s",
            static_cast<int>(sequence_.keyframes.size()),
            static_cast<int>(sequence_.shots.size()),
            static_cast<int>(sequence_.events.size()),
            clipDirectoryPath_.c_str());
    }

    bool CameraKeyframeEditorModule::CaptureFromActiveCamera(const CameraEditorContext& context, CameraSnapshot& outSnapshot) const
    {
        Camera* active3D = context.cameraManager->GetActiveCamera(CameraType::Camera3D);
        if (!active3D) {
            return false;
        }

        outSnapshot = active3D->CaptureSnapshot("Keyframe");
        return true;
    }

    bool CameraKeyframeEditorModule::ApplyToActiveCamera(const CameraEditorContext& context, const CameraSnapshot& snapshot)
    {
        Camera* active3D = context.cameraManager->GetActiveCamera(CameraType::Camera3D);
        if (!active3D) {
            return false;
        }

        active3D->RestoreSnapshot(snapshot);
        ignoreNextAutoKey_ = true;
        observedSnapshot_ = snapshot;
        hasObservedSnapshot_ = true;
        return true;
    }

    void CameraKeyframeEditorModule::ApplyEvaluatedAt(const CameraEditorContext& context, float time)
    {
        const CameraSequenceAimContext aimContext = MakeAimContext(context);

        CameraSnapshot evaluated{};
        if (CameraSequenceEvaluator::Evaluate(sequence_, time, evaluated, &aimContext)) {
            ApplyToActiveCamera(context, evaluated);
        }
    }

    CameraSequenceAimContext CameraKeyframeEditorModule::MakeAimContext(const CameraEditorContext& context) const
    {
        // 編集中も再生時と同じ解決をしないと、エディタで見た向きと
        // ゲーム中の向きが食い違う。
        CameraSequenceAimContext aimContext{};
        GameObjectManager* objects = context.gameObjectManager;
        if (!objects) {
            return aimContext;
        }

        aimContext.resolveObject = [objects](const std::string& name, Vector3& outPosition) {
            for (const auto& object : objects->GetAllObjects()) {
                if (object && object->GetName() == name) {
                    outPosition = object->GetWorldPosition();
                    return true;
                }
            }
            return false;
        };
        return aimContext;
    }

    bool CameraKeyframeEditorModule::IsSameSnapshot(const CameraSnapshot& lhs, const CameraSnapshot& rhs) const
    {
        constexpr float epsilon = 0.0001f;

        const auto nearEqual = [](float a, float b) {
            return std::fabs(a - b) <= 0.0001f;
        };

        if (lhs.parameters.projectionType != rhs.parameters.projectionType) {
            return false;
        }

        if (!nearEqual(lhs.parameters.fov, rhs.parameters.fov)
            || !nearEqual(lhs.parameters.nearClip, rhs.parameters.nearClip)
            || !nearEqual(lhs.parameters.farClip, rhs.parameters.farClip)
            || !nearEqual(lhs.parameters.aspectRatio, rhs.parameters.aspectRatio)) {
            return false;
        }

        return nearEqual(lhs.position.x, rhs.position.x)
            && nearEqual(lhs.position.y, rhs.position.y)
            && nearEqual(lhs.position.z, rhs.position.z)
            && nearEqual(lhs.rotation.x, rhs.rotation.x)
            && nearEqual(lhs.rotation.y, rhs.rotation.y)
            && nearEqual(lhs.rotation.z, rhs.rotation.z)
            && nearEqual(lhs.scale.x, rhs.scale.x)
            && nearEqual(lhs.scale.y, rhs.scale.y)
            && nearEqual(lhs.scale.z, rhs.scale.z)
            && std::fabs(lhs.parameters.fov - rhs.parameters.fov) <= epsilon;
    }

    void CameraKeyframeEditorModule::UpdateAutoKey(const CameraEditorContext& context)
    {
        // オートキー: カメラを動かした「あと」に自動でキーを打つ。
        // 直前のスナップショットと比べて差が出た時だけ反応させ、
        // 再生中や、こちらがカメラへ書き込んだ直後（ignoreNextAutoKey_）は無視する
        if (!context.cameraManager || !autoKeyEnabled_ || isPlaying_) {
            autoKeyEditing_ = false;
            return;
        }

        CameraSnapshot current{};
        if (!CaptureFromActiveCamera(context, current)) {
            autoKeyEditing_ = false;
            hasObservedSnapshot_ = false;
            return;
        }

        if (ignoreNextAutoKey_) {
            ignoreNextAutoKey_ = false;
            observedSnapshot_ = current;
            hasObservedSnapshot_ = true;
            autoKeyEditing_ = false;
            return;
        }

        if (!hasObservedSnapshot_) {
            observedSnapshot_ = current;
            hasObservedSnapshot_ = true;
            autoKeyEditing_ = false;
            return;
        }

        const bool changed = !IsSameSnapshot(observedSnapshot_, current);
        if (!changed) {
            autoKeyEditing_ = false;
            return;
        }

        if (!autoKeyEditing_) {
            PushUndoState();
            autoKeyEditing_ = true;
        }

        const int nearest = FindNearestKeyframeIndex(playhead_);
        if (nearest >= 0 && std::fabs(sequence_.keyframes[nearest].time - playhead_) <= updateThreshold_) {
            sequence_.keyframes[nearest].snapshot = current;
            selectedIndex_ = nearest;
        } else {
            CameraSequenceKeyframe key{};
            key.time = playhead_;
            key.snapshot = current;
            sequence_.keyframes.push_back(key);
            std::sort(sequence_.keyframes.begin(), sequence_.keyframes.end(),
                [](const CameraSequenceKeyframe& a, const CameraSequenceKeyframe& b) { return a.time < b.time; });

            selectedIndex_ = FindNearestKeyframeIndex(playhead_);
        }

        observedSnapshot_ = current;
    }

    void CameraKeyframeEditorModule::DrawEventTrack()
    {
        UI::Hint("再生ヘッドがこの時刻を跨いだ瞬間に発火します。ゲーム中の再生でも同じです。");

        if (ImGui::Button("現在位置にイベントを追加")) {
            PushUndoState();

            CameraSequenceEvent event{};
            event.time = playhead_;
            event.type = CameraSequenceEventType::Shake;
            // 既定のプリセット名を入れておく。空のまま置かれて
            // 「何も起きない」と誤解されるのを防ぐ。
            const auto& presets = CameraShake::GetPresetLibrary().GetAll();
            event.name = presets.empty() ? std::string() : presets.front().name;

            sequence_.events.push_back(event);
            sequence_.SortEvents();
            selectedEventIndex_ = -1;
            for (int i = 0; i < static_cast<int>(sequence_.events.size()); ++i) {
                if (sequence_.events[i].time == event.time && sequence_.events[i].name == event.name) {
                    selectedEventIndex_ = i;
                    break;
                }
            }
            editingEventNameIndex_ = -1;
        }

        UI::SameLine();
        if (ImGui::Button("選択イベントを削除")) {
            if (selectedEventIndex_ >= 0 && selectedEventIndex_ < static_cast<int>(sequence_.events.size())) {
                PushUndoState();
                sequence_.events.erase(sequence_.events.begin() + selectedEventIndex_);
                selectedEventIndex_ = sequence_.events.empty()
                    ? -1
                    : std::clamp(selectedEventIndex_, 0, static_cast<int>(sequence_.events.size()) - 1);
                editingEventNameIndex_ = -1;
            }
        }

        if (sequence_.events.empty()) {
            UI::Hint("イベントがありません。");
            return;
        }

        selectedEventIndex_ = std::clamp(selectedEventIndex_, -1, static_cast<int>(sequence_.events.size()) - 1);

        if (auto lb = UI::Scope::ListBoxScope("イベント一覧", ImVec2(-1.0f, 110.0f))) {
            for (int i = 0; i < static_cast<int>(sequence_.events.size()); ++i) {
                const CameraSequenceEvent& event = sequence_.events[i];
                const int typeIndex = std::clamp(static_cast<int>(event.type), 0, kEventTypeLabelCount - 1);

                char label[256]{};
                std::snprintf(label, sizeof(label), "%.2f秒  %s  %s%s##event%d",
                    event.time,
                    kEventTypeLabels[typeIndex],
                    event.name.empty() ? "(名前なし)" : event.name.c_str(),
                    event.enabled ? "" : "  (無効)",
                    i);

                if (ImGui::Selectable(label, selectedEventIndex_ == i)) {
                    selectedEventIndex_ = i;
                    editingEventNameIndex_ = -1;
                }
            }
        }

        if (selectedEventIndex_ < 0 || selectedEventIndex_ >= static_cast<int>(sequence_.events.size())) {
            return;
        }

        CameraSequenceEvent& event = sequence_.events[selectedEventIndex_];

        bool enabled = event.enabled;
        if (UI::Widgets::ToggleSwitch("有効", &enabled)) {
            PushUndoState();
            event.enabled = enabled;
        }

        float time = event.time;
        if (TrackEdit(UI::DragFloat("時刻 (秒)", time, 0.02f, 0.0f, sequence_.timelineLength, "%.2f"))) {
            event.time = std::clamp(time, 0.0f, sequence_.timelineLength);
            // 並べ替えると添字がずれるので、同じイベントを選び直す。
            const CameraSequenceEvent moved = event;
            sequence_.SortEvents();
            for (int i = 0; i < static_cast<int>(sequence_.events.size()); ++i) {
                if (sequence_.events[i].time == moved.time && sequence_.events[i].name == moved.name) {
                    selectedEventIndex_ = i;
                    break;
                }
            }
            editingEventNameIndex_ = -1;
            return;
        }

        int typeIndex = std::clamp(static_cast<int>(event.type), 0, kEventTypeLabelCount - 1);
        if (ImGui::BeginCombo("種類", kEventTypeLabels[typeIndex])) {
            for (int i = 0; i < kEventTypeLabelCount; ++i) {
                const bool selected = (i == typeIndex);
                if (ImGui::Selectable(kEventTypeLabels[i], selected)) {
                    PushUndoState();
                    event.type = static_cast<CameraSequenceEventType>(i);
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        switch (event.type) {
        case CameraSequenceEventType::Shake: {
            const auto& presets = CameraShake::GetPresetLibrary().GetAll();
            const char* preview = event.name.empty() ? "未選択" : event.name.c_str();

            if (ImGui::BeginCombo("シェイクプリセット", preview)) {
                for (const auto& preset : presets) {
                    const bool selected = (preset.name == event.name);
                    if (ImGui::Selectable(preset.name.c_str(), selected)) {
                        PushUndoState();
                        event.name = preset.name;
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            if (!event.name.empty() && !CameraShake::GetPresetLibrary().Find(event.name)) {
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f),
                    "プリセットが見つかりません。再生しても何も起きません。");
            }

            float scale = event.value;
            if (TrackEdit(UI::SliderFloat("強さ倍率", scale, 0.0f, 3.0f, "%.2f"))) {
                event.value = scale;
            }

            if (ImGui::Button("この揺れを試す")) {
                CameraShake::PlayPreset(event.name, event.value);
            }
            break;
        }

        case CameraSequenceEventType::Trauma: {
            float amount = event.value;
            if (TrackEdit(UI::SliderFloat("加算量", amount, 0.0f, 1.0f, "%.2f"))) {
                event.value = amount;
            }
            if (ImGui::Button("試す")) {
                CameraShake::AddTrauma(event.value);
            }
            break;
        }

        case CameraSequenceEventType::TimeScale: {
            float scale = event.value;
            if (TrackEdit(UI::SliderFloat("時間スケール", scale, 0.0f, 2.0f, "%.2f"))) {
                event.value = scale;
            }
            float duration = event.duration;
            if (TrackEdit(UI::DragFloat("継続 (秒)", duration, 0.01f, 0.0f, 10.0f, "%.2f"))) {
                event.duration = (std::max)(duration, 0.0f);
            }
            UI::Hint("継続が過ぎると等倍へ戻ります。");
            break;
        }

        case CameraSequenceEventType::Callback: {
            if (editingEventNameIndex_ != selectedEventIndex_) {
                std::snprintf(eventNameBuffer_, sizeof(eventNameBuffer_), "%s", event.name.c_str());
                editingEventNameIndex_ = selectedEventIndex_;
            }
            if (UI::InputText("イベント名", eventNameBuffer_, sizeof(eventNameBuffer_))) {
                event.name = eventNameBuffer_;
            }
            float value = event.value;
            if (TrackEdit(UI::DragFloat("値", value, 0.05f, -1000.0f, 1000.0f, "%.2f"))) {
                event.value = value;
            }
            UI::Hint("CameraSequenceCallbackEvent として EventBus へ流れます。");
            break;
        }
        }
    }

    void CameraKeyframeEditorModule::DrawViewportVisualization(const CameraEditorContext& context)
    {
        if (!viewportVisualizationEnabled_ || sequence_.keyframes.empty()) {
            return;
        }

        auto& lineManager = LineManager::GetInstance();
        const CameraSequenceAimContext aimContext = MakeAimContext(context);

        // スナップショットの position はそのまま視点のワールド座標（軌道パラメータは持たない）。
        if (viewportShowTrajectory_ && sequence_.keyframes.size() >= 2) {
            // 区間ごとの補間結果を細かくサンプルし、Sceneビュー上で軌跡として可視化する。
            for (size_t i = 0; i + 1 < sequence_.keyframes.size(); ++i) {
                const CameraSequenceKeyframe& from = sequence_.keyframes[i];
                const CameraSequenceKeyframe& to = sequence_.keyframes[i + 1];

                if (to.time <= from.time) {
                    continue;
                }

                // 実際の評価をそのままサンプルする。区間ごとの補間方式・緩急が
                // 描かれる軌跡へ反映されるので、見た目と再生結果が食い違わない。
                Vector3 prev = from.snapshot.position;
                for (int sampleIndex = 1; sampleIndex <= viewportTrajectorySamplesPerSegment_; ++sampleIndex) {
                    const float localT = static_cast<float>(sampleIndex) / static_cast<float>(viewportTrajectorySamplesPerSegment_);
                    const float sampleTime = from.time + (to.time - from.time) * localT;

                    CameraSnapshot sampled{};
                    if (!CameraSequenceEvaluator::EvaluateRaw(sequence_, sampleTime, sampled, &aimContext)) {
                        break;
                    }

                    lineManager.DrawLine(prev, sampled.position, viewportTrajectoryColor_, viewportTrajectoryAlpha_, false);
                    prev = sampled.position;
                }
            }
        }

        if (viewportShowKeyMarkers_) {
            // キーフレーム位置をマーカー表示し、選択中キーを色で区別する。
            for (int i = 0; i < static_cast<int>(sequence_.keyframes.size()); ++i) {
                const Vector3 position = sequence_.keyframes[i].snapshot.position;
                const Vector3 color = (i == selectedIndex_) ? viewportSelectedKeyColor_ : viewportKeyMarkerColor_;

                // ワイヤ球は深度テストを切れないので、3 軸の十字で描く。
                // 地形の向こうにあるキーも見えないと、位置合わせができない。
                const float size = viewportMarkerSize_;
                lineManager.DrawLine(position - Vector3{ size, 0.0f, 0.0f }, position + Vector3{ size, 0.0f, 0.0f }, color, 0.95f, false);
                lineManager.DrawLine(position - Vector3{ 0.0f, size, 0.0f }, position + Vector3{ 0.0f, size, 0.0f }, color, 0.95f, false);
                lineManager.DrawLine(position - Vector3{ 0.0f, 0.0f, size }, position + Vector3{ 0.0f, 0.0f, size }, color, 0.95f, false);
            }
        }

        if (viewportShowFrustum_) {
            // 選択キーは濃く、他は薄く。どのキーを触っているのかを色で分ける。
            for (int i = 0; i < static_cast<int>(sequence_.keyframes.size()); ++i) {
                const bool selected = (i == selectedIndex_);
                DrawKeyFrustum(sequence_.keyframes[i], aimContext,
                    selected ? viewportSelectedKeyColor_ : viewportKeyMarkerColor_,
                    selected ? 0.95f : viewportFrustumIdleAlpha_);
            }
        }

        if (viewportShowPlayheadFrustum_) {
            // 再生ヘッド位置の構図。キーの間でも「いま何が写っているか」が読めるので、
            // スクラブしながら画を詰めるときに効く。
            CameraSnapshot current{};
            if (CameraSequenceEvaluator::Evaluate(sequence_, playhead_, current, &aimContext)) {
                DrawFrustumFromPose(current.position, current.rotation,
                    current.parameters.fov, current.parameters.aspectRatio,
                    Vector3{ 0.95f, 0.35f, 0.32f }, 0.95f);
            }
        }

        if (viewportShowDebugTarget_) {
            // 注視を使っているキーは、視点から注視先へ線を引く。
            // どこを見ているつもりのキーなのかが、画面を切り替えずに分かる。
            for (const auto& key : sequence_.keyframes) {
                Vector3 target{};
                if (!CameraSequenceEvaluator::ResolveAimTarget(key, &aimContext, target)) {
                    continue;
                }

                lineManager.DrawLine(key.snapshot.position, target, viewportDebugTargetColor_, 0.7f, false);
                                const float targetSize = viewportMarkerSize_ * 0.75f;
                lineManager.DrawLine(target - Vector3{ targetSize, 0.0f, 0.0f }, target + Vector3{ targetSize, 0.0f, 0.0f }, viewportDebugTargetColor_, 0.95f, false);
                lineManager.DrawLine(target - Vector3{ 0.0f, targetSize, 0.0f }, target + Vector3{ 0.0f, targetSize, 0.0f }, viewportDebugTargetColor_, 0.95f, false);
            }
        }
    }

    void CameraKeyframeEditorModule::DrawKeyFrustum(const CameraSequenceKeyframe& key,
        const CameraSequenceAimContext& aim, const Vector3& color, float alpha) const
    {
        // 注視キーは保存された回転ではなく、注視先を向いた回転で描く。
        // そうしないと画に写る範囲が実際の再生とずれる。
        Vector3 rotation = key.snapshot.rotation;
        Vector3 target{};
        if (CameraSequenceEvaluator::ResolveAimTarget(key, &aim, target)) {
            rotation = CameraSequenceEvaluator::LookRotation(key.snapshot.position, target, key.aimRoll);
        }

        DrawFrustumFromPose(key.snapshot.position, rotation,
            key.snapshot.parameters.fov, key.snapshot.parameters.aspectRatio, color, alpha);
    }

    void CameraKeyframeEditorModule::DrawFrustumFromPose(const Vector3& position,
        const Vector3& rotation, float fov, float aspectRatio,
        const Vector3& color, float alpha) const
    {
        const Matrix4x4 basis = MathCore::Matrix::MakeAffine(
            Vector3{ 1.0f, 1.0f, 1.0f }, rotation, Vector3{ 0.0f, 0.0f, 0.0f });
        const Vector3 forward = basis.GetAxisZ();
        const Vector3 right = basis.GetAxisX();
        const Vector3 up = basis.GetAxisY();

        // 遠クリップまで描くと線が画面を埋めるので、見て分かる長さで打ち切る。
        const float length = viewportFrustumLength_;
        const float halfHeight = std::tan(fov * 0.5f) * length;
        // アスペクトは 0（自動）のことが多いので、読めれば十分な 16:9 で描く。
        const float aspect = (aspectRatio > 0.0f) ? aspectRatio : (16.0f / 9.0f);
        const float halfWidth = halfHeight * aspect;

        const Vector3 origin = position;
        const Vector3 center = origin + forward * length;
        const Vector3 corners[4] = {
            center + up * halfHeight - right * halfWidth,
            center + up * halfHeight + right * halfWidth,
            center - up * halfHeight + right * halfWidth,
            center - up * halfHeight - right * halfWidth
        };

        auto& lineManager = LineManager::GetInstance();
        for (int i = 0; i < 4; ++i) {
            // 地形に隠れると構図の確認にならないので、常に手前へ描く。
            lineManager.DrawLine(origin, corners[i], color, alpha, false);
            lineManager.DrawLine(corners[i], corners[(i + 1) % 4], color, alpha, false);
        }
    }

    void CameraKeyframeEditorModule::DrawViewportOverlay(const CameraEditorContext& context,
        const Camera& viewCamera, const CameraEditorViewport& viewport)
    {
        if (viewportShowIcons_ && viewport.IsValid()) {
            DrawKeyIcons(context, viewCamera, viewport);
        }

        DrawKeyGizmo(context, viewCamera);
    }

    void CameraKeyframeEditorModule::DrawKeyIcons(const CameraEditorContext& context,
        const Camera& viewCamera, const CameraEditorViewport& viewport)
    {
        if (sequence_.keyframes.empty()) {
            return;
        }

        ImDrawList* draw = ImGui::GetWindowDrawList();
        const CameraSequenceAimContext aimContext = MakeAimContext(context);
        const float scale = viewportIconScale_;
        const float hitRadius = CameraIconOverlay::GetHitRadius(scale);

        const ImVec2 mouse = ImGui::GetMousePos();
        int clickedKey = -1;
        float clickedDistance = hitRadius;

        for (int i = 0; i < static_cast<int>(sequence_.keyframes.size()); ++i) {
            const CameraSequenceKeyframe& key = sequence_.keyframes[i];

            float screenX = 0.0f;
            float screenY = 0.0f;
            if (!CameraIconOverlay::WorldToScreen(key.snapshot.position, viewCamera, viewport, screenX, screenY)) {
                continue;
            }

            // ビューポートの外へはみ出したものは描かない。ImGui のクリップに任せると
            // 隣のパネルの上にアイコンが乗る。
            if (screenX < viewport.x - hitRadius || screenX > viewport.x + viewport.width + hitRadius
                || screenY < viewport.y - hitRadius || screenY > viewport.y + viewport.height + hitRadius) {
                continue;
            }

            // レンズが向く方向は、そのキーが実際に撮る向き。注視キーは注視先を向いた
            // 回転で求めないと、アイコンの向きと再生結果がずれる。
            Vector3 rotation = key.snapshot.rotation;
            Vector3 target{};
            if (CameraSequenceEvaluator::ResolveAimTarget(key, &aimContext, target)) {
                rotation = CameraSequenceEvaluator::LookRotation(key.snapshot.position, target, key.aimRoll);
            }
            const Matrix4x4 basis = MathCore::Matrix::MakeAffine(
                Vector3{ 1.0f, 1.0f, 1.0f }, rotation, Vector3{ 0.0f, 0.0f, 0.0f });
            const float angle = CameraIconOverlay::ComputeScreenAngle(
                key.snapshot.position, basis.GetAxisZ(), viewCamera, viewport);

            const bool selected = (i == selectedIndex_);
            const ImU32 fill = selected ? IM_COL32(240, 180, 64, 255) : IM_COL32(120, 170, 235, 235);
            const ImU32 outline = selected ? IM_COL32(70, 44, 6, 255) : IM_COL32(16, 26, 40, 255);

            CameraIconOverlay::DrawCameraGlyph(draw, screenX, screenY, angle,
                selected ? scale * 1.2f : scale, fill, outline);

            // 番号と名前。アイコンだけではどのキーか分からない。
            char label[160]{};
            if (key.label.empty()) {
                std::snprintf(label, sizeof(label), "%d", i + 1);
            } else {
                std::snprintf(label, sizeof(label), "%d %s", i + 1, key.label.c_str());
            }
            const float labelY = screenY + CameraIconOverlay::GetHitRadius(scale) + 2.0f;
            draw->AddText(ImVec2(screenX - 3.0f + 1.0f, labelY + 1.0f), IM_COL32(0, 0, 0, 160), label);
            draw->AddText(ImVec2(screenX - 3.0f, labelY),
                selected ? IM_COL32(255, 226, 160, 255) : IM_COL32(210, 224, 240, 235), label);

            const float dx = mouse.x - screenX;
            const float dy = mouse.y - screenY;
            const float distance = std::sqrt(dx * dx + dy * dy);
            if (distance <= clickedDistance) {
                clickedDistance = distance;
                clickedKey = i;
            }
        }

        // ギズモを掴んでいる最中は選択を変えない。掴んだまま別のキーへ移ると
        // ドラッグの対象が入れ替わってしまう。
        if (clickedKey >= 0 && !Gizmo::IsUsing() && !Gizmo::IsOver()
            && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            selectedIndex_ = clickedKey;
            playhead_ = sequence_.keyframes[clickedKey].time;
            ApplyEvaluatedAt(context, playhead_);
        }
    }

    void CameraKeyframeEditorModule::DrawKeyGizmo(const CameraEditorContext& context, const Camera& viewCamera)
    {
        if (!viewportGizmoEnabled_
            || selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(sequence_.keyframes.size())) {
            return;
        }

        CameraSequenceKeyframe& key = sequence_.keyframes[selectedIndex_];

        // 注視点キーは注視先を掴めたほうが早い。位置は数値でも追えるが、
        // 「どこを見るか」は空間で決めたい。
        const bool editAimPoint = (key.aimMode == CameraSequenceAimMode::LookAtPoint);
        Vector3 handle = editAimPoint ? (key.aimPoint + key.aimOffset) : key.snapshot.position;
        const Vector3 before = handle;

        if (!Gizmo::ManipulatePoint(handle, &viewCamera)) {
            gizmoDragging_ = false;
            return;
        }

        // 掴んだ最初のフレームだけ履歴を積む。毎フレーム積むと履歴が埋まる。
        if (!gizmoDragging_) {
            gizmoDragging_ = true;
            PushUndoState();
        }

        if (editAimPoint) {
            key.aimPoint += (handle - before);
        } else {
            key.snapshot.position = handle;
        }

        ApplyEvaluatedAt(context, playhead_);
    }
    int CameraKeyframeEditorModule::FindNearestKeyframeIndex(float time) const
    {
        if (sequence_.keyframes.empty()) {
            return -1;
        }

        int bestIndex = 0;
        float bestDistance = std::fabs(sequence_.keyframes[0].time - time);

        for (int i = 1; i < static_cast<int>(sequence_.keyframes.size()); ++i) {
            const float distance = std::fabs(sequence_.keyframes[i].time - time);
            if (distance < bestDistance) {
                bestDistance = distance;
                bestIndex = i;
            }
        }

        return bestIndex;
    }

    int CameraKeyframeEditorModule::FindPreviousKeyframeIndex(float time) const
    {
        int index = -1;
        for (int i = 0; i < static_cast<int>(sequence_.keyframes.size()); ++i) {
            if (sequence_.keyframes[i].time <= time) {
                index = i;
            } else {
                break;
            }
        }
        return index;
    }

    int CameraKeyframeEditorModule::FindNextKeyframeIndex(float time) const
    {
        for (int i = 0; i < static_cast<int>(sequence_.keyframes.size()); ++i) {
            if (sequence_.keyframes[i].time >= time) {
                return i;
            }
        }
        return -1;
    }

    void CameraKeyframeEditorModule::RefreshClipFileList()
    {
        clipFileList_ = CameraSequenceIO::GetSequenceFileList(clipDirectoryPath_);
        needRefreshClipFileList_ = false;
    }

    bool CameraKeyframeEditorModule::SaveCurrentClipToFile(const std::string& filePath) const
    {
        // 編集中のタイムラインがシーケンスそのものなので、そのまま渡すだけでよい。
        return CameraSequenceIO::Save(filePath, sequence_);
    }

    bool CameraKeyframeEditorModule::LoadClipFromFile(const std::string& filePath)
    {
        // 読み込み成功時だけ内部状態を差し替える。失敗しても編集中のタイムラインは壊さない
        CameraSequenceAsset asset{};
        if (!CameraSequenceIO::Load(filePath, asset)) {
            return false;
        }

        // 時刻の並び・範囲は CameraSequenceIO::Load が整えて返す。
        sequence_ = std::move(asset);

        selectedIndex_ = sequence_.keyframes.empty() ? -1 : 0;
        selectedShotIndex_ = sequence_.shots.empty() ? -1 : 0;
        editingShotNameIndex_ = -1;
        playhead_ = 0.0f;
        isPlaying_ = false;
        return true;
    }

    CameraKeyframeEditorModule::EditorState CameraKeyframeEditorModule::CaptureEditorState() const
    {
        EditorState state{};
        state.sequence = sequence_;
        state.playhead = playhead_;
        state.selectedIndex = selectedIndex_;
        state.selectedShotIndex = selectedShotIndex_;
        return state;
    }

    void CameraKeyframeEditorModule::ApplyEditorState(const EditorState& state)
    {
        sequence_ = state.sequence;
        playhead_ = state.playhead;
        selectedIndex_ = state.selectedIndex;
        selectedShotIndex_ = state.selectedShotIndex;
        editingShotNameIndex_ = -1;
    }

    void CameraKeyframeEditorModule::PushUndoState()
    {
        PushUndoState(CaptureEditorState());
    }

    void CameraKeyframeEditorModule::PushUndoState(const EditorState& state)
    {
        undoStack_.push_back(state);
        if (undoStack_.size() > maxHistoryCount_) {
            undoStack_.erase(undoStack_.begin());
        }
        redoStack_.clear();
    }

    bool CameraKeyframeEditorModule::TrackEdit(bool changed)
    {
        // グループになっている DragFloat3 でも、ImGui 側が掴んでいる要素の ID を
        // 最後の項目として転送してくれるので、この 2 つで足りる。
        const unsigned int itemId = static_cast<unsigned int>(ImGui::GetItemID());
        const bool active = ImGui::IsItemActive();

        if (active && itemId != 0 && itemId != activeEditItemId_) {
            // 掴み始め。ここではまだ積まない。掴んだだけで動かさなかった操作を
            // 履歴に残すと、Ctrl+Z が「何も変わらない 1 手」になってしまう。
            activeEditItemId_ = itemId;
            pendingEditState_ = CaptureEditorState();
            hasPendingEditState_ = true;
        }

        if (changed) {
            if (hasPendingEditState_ && itemId == activeEditItemId_) {
                // ドラッグ中の最初の変更。掴む前の状態を 1 件だけ積む。
                PushUndoState(pendingEditState_);
                hasPendingEditState_ = false;
            } else if (!hasPendingEditState_ && itemId != activeEditItemId_) {
                // 掴まずに 1 回で終わる変更（想定外の経路）。履歴を落とすより
                // 積んでおく方がまし。
                PushUndoState();
            }
        }

        if (!active && itemId == activeEditItemId_) {
            // 手を離した。控えは捨てる。
            activeEditItemId_ = 0;
            hasPendingEditState_ = false;
        }

        return changed;
    }

    void CameraKeyframeEditorModule::Undo()
    {
        if (undoStack_.empty()) {
            return;
        }

        redoStack_.push_back(CaptureEditorState());
        ApplyEditorState(undoStack_.back());
        undoStack_.pop_back();
    }

    void CameraKeyframeEditorModule::Redo()
    {
        if (redoStack_.empty()) {
            return;
        }

        undoStack_.push_back(CaptureEditorState());
        ApplyEditorState(redoStack_.back());
        redoStack_.pop_back();
    }
}

#endif // _DEBUG
