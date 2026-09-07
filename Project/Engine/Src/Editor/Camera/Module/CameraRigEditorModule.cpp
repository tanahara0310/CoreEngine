#include "pch.h"
#include "CameraRigEditorModule.h"

#ifdef USE_IMGUI

#include "Camera/Camera.h"
#include "Camera/CameraManager.h"
#include "Camera/Rig/CameraRig.h"
#include "Camera/Rig/CameraRigIO.h"
#include "Editor/ImGui/ImGuiAll.h"
#include "GameObject/GameObject.h"
#include "GameObject/GameObjectManager.h"

#include <filesystem>
#include <memory>

namespace CoreEngine
{
    namespace
    {
        constexpr float kRadToDeg = MathCore::Constants::kRadToDeg;
        constexpr float kDegToRad = MathCore::Constants::kDegToRad;

        // 表示名は「何が起きるか」を先に書く。英名は既存資料と突き合わせるための添え物。
        constexpr const char* kBodyModeLabels[] = {
            "固定 (Fixed)",
            "対象に付いていく (FollowTarget)",
            "対象の周りを回る (OrbitTarget)",
            "対象をまとめて収める (FrameTargets)",
            "レールの上を滑る (Rail)"
        };

        constexpr const char* kAimModeLabels[] = {
            "位置側の向きをそのまま使う (FollowBody)",
            "対象を見る (LookAtTarget)",
            "対象をまとめて見る (FrameTargets)"
        };

        constexpr const char* kLensModeLabels[] = {
            "固定 (Fixed)",
            "距離で変える (DistanceToFov)",
            "速さで変える (SpeedToFov)"
        };

        constexpr const char* kOffsetSpaceLabels[] = {
            "ワールド軸（対象が振り向いても位置は変わらない）",
            "対象の向きに追従（背後に付き続ける）"
        };

        constexpr const char* kDistanceSourceLabels[] = {
            "対象どうしの広がり",
            "カメラから注視先まで"
        };

        constexpr const char* kDampingModeLabels[] = {
            "指数減衰（すぐ動き出す）",
            "バネ（飛び飛びの目標でもカクつかない）"
        };

        /// @brief 番号付きの列挙をコンボで選ばせる
        /// @return 選び直したら true
        template <typename Enum, size_t Count>
        bool EnumCombo(const char* label, Enum& value, const char* const (&labels)[Count])
        {
            const int current = static_cast<int>(value);
            const char* preview = (current >= 0 && current < static_cast<int>(Count))
                ? labels[current] : labels[0];

            bool changed = false;
            if (ImGui::BeginCombo(label, preview)) {
                for (int i = 0; i < static_cast<int>(Count); ++i) {
                    const bool selected = (i == current);
                    if (ImGui::Selectable(labels[i], selected)) {
                        value = static_cast<Enum>(i);
                        changed = true;
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            return changed;
        }

        /// @brief ラジアンの値を度で編集させる
        bool DragAngleDegrees(const char* label, float& radians, float speed,
            float minDegrees, float maxDegrees)
        {
            float degrees = radians * kRadToDeg;
            if (UI::DragFloat(label, degrees, speed, minDegrees, maxDegrees, "%.1f 度")) {
                radians = degrees * kDegToRad;
                return true;
            }
            return false;
        }
    }

    void CameraRigEditorModule::RefreshRigFileList()
    {
        rigFileList_ = CameraRig::GetLibrary().ListNames();
        needRefreshRigFileList_ = false;

        // 選択が範囲外に落ちたら外す。消したファイルを指したままにしない。
        if (selectedRigIndex_ >= static_cast<int>(rigFileList_.size())) {
            selectedRigIndex_ = -1;
        }
    }

    std::string CameraRigEditorModule::MakeRigPath(const std::string& name) const
    {
        const std::filesystem::path path =
            std::filesystem::path(CameraRig::GetLibrary().GetDirectory()) / (name + ".json");
        return path.string();
    }

    bool CameraRigEditorModule::SaveCurrentRig()
    {
        const std::string name = nameBuffer_;
        if (name.empty()) {
            statusMessage_ = "名前が空です";
            return false;
        }

        rig_.name = name;
        rig_.Sanitize();

        const std::string path = MakeRigPath(name);
        std::error_code error;
        std::filesystem::create_directories(
            std::filesystem::path(path).parent_path(), error);

        if (!CameraRigIO::Save(path, rig_)) {
            statusMessage_ = "保存に失敗: " + path;
            return false;
        }

        // 保存したものが次の Activate で読まれるよう、キャッシュを捨てる。
        CameraRig::GetLibrary().Reload(name);
        needRefreshRigFileList_ = true;
        statusMessage_ = "保存しました: " + name;
        return true;
    }

    bool CameraRigEditorModule::LoadRig(const std::string& name)
    {
        CameraRigAsset loaded{};
        if (!CameraRigIO::Load(MakeRigPath(name), loaded)) {
            statusMessage_ = "読み込めませんでした: " + name;
            return false;
        }

        rig_ = std::move(loaded);
        if (rig_.name.empty()) {
            rig_.name = name;
        }

        std::snprintf(nameBuffer_, sizeof(nameBuffer_), "%s", rig_.name.c_str());
        dirty_ = true;
        statusMessage_ = "読み込みました: " + name;
        return true;
    }

    void CameraRigEditorModule::PushToRuntime()
    {
        CameraRigRuntime* runtime = CameraRig::GetActiveRuntime();
        if (!runtime || !runtime->IsActive()) {
            return;
        }

        // 動かしている間だけ、編集中の値をそのまま実機へ流す。切り替え直しにはならないので、
        // 繋ぎの途中でも減衰の途中でも見た目が飛ばない。
        CameraRigAsset copy = rig_;
        copy.Sanitize();
        runtime->ReplaceAsset(std::make_shared<const CameraRigAsset>(std::move(copy)));
    }

    void CameraRigEditorModule::Update(const CameraEditorContext&)
    {
        if (dirty_) {
            PushToRuntime();
            dirty_ = false;
        }
    }

    void CameraRigEditorModule::Draw(const CameraEditorContext& context)
    {
        if (!context.cameraManager) {
            return;
        }

        if (needRefreshRigFileList_) {
            RefreshRigFileList();
        }

        DrawRigSelector(context);
        DrawTransport();

        UI::Spacing();

        if (ImGui::BeginTabBar("##RigParts")) {
            if (ImGui::BeginTabItem("位置")) {
                DrawBody(context);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("向き")) {
                DrawAim(context);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("画角")) {
                DrawLens();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("滑らかさ")) {
                DrawDamping();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        UI::Spacing();
        ImGui::TextDisabled("%s", CameraRig::GetLibrary().GetDirectory().c_str());
        if (!statusMessage_.empty()) {
            UI::SameLine();
            ImGui::TextDisabled("|");
            UI::SameLine();
            ImGui::TextUnformatted(statusMessage_.c_str());
        }
    }

    void CameraRigEditorModule::DrawRigSelector(const CameraEditorContext&)
    {
        ImGui::SetNextItemWidth(200.0f);
        if (UI::InputText("名前", nameBuffer_, sizeof(nameBuffer_))) {
            dirty_ = true;
        }

        UI::SameLine();
        ImGui::SetNextItemWidth(180.0f);
        const char* preview = (selectedRigIndex_ >= 0
            && selectedRigIndex_ < static_cast<int>(rigFileList_.size()))
            ? rigFileList_[selectedRigIndex_].c_str()
            : "保存済みリグ...";

        if (ImGui::BeginCombo("##RigFiles", preview)) {
            for (int i = 0; i < static_cast<int>(rigFileList_.size()); ++i) {
                const bool selected = (i == selectedRigIndex_);
                if (ImGui::Selectable(rigFileList_[i].c_str(), selected)) {
                    selectedRigIndex_ = i;
                    LoadRig(rigFileList_[i]);
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        UI::SameLine();
        if (ImGui::SmallButton("保存")) {
            SaveCurrentRig();
        }
        UI::SameLine();
        if (ImGui::SmallButton("新規")) {
            rig_ = CameraRigAsset{};
            std::snprintf(nameBuffer_, sizeof(nameBuffer_), "NewCameraRig");
            selectedRigIndex_ = -1;
            dirty_ = true;
            statusMessage_ = "新しいリグを作りました";
        }
        UI::SameLine();
        if (ImGui::SmallButton("再読込")) {
            needRefreshRigFileList_ = true;
            if (selectedRigIndex_ >= 0
                && selectedRigIndex_ < static_cast<int>(rigFileList_.size())) {
                LoadRig(rigFileList_[selectedRigIndex_]);
            }
        }
    }

    void CameraRigEditorModule::DrawTransport()
    {
        CameraRigRuntime* runtime = CameraRig::GetActiveRuntime();

        if (ImGui::Button("▶ このリグを動かす")) {
            if (runtime) {
                CameraRigAsset copy = rig_;
                copy.Sanitize();

                CameraRigActivateOptions options;
                options.blendSeconds = activateBlendSeconds_;

                // ライブラリではなく編集中の中身をそのまま渡す。保存しなくても試せる。
                runtime->Activate(std::make_shared<const CameraRigAsset>(std::move(copy)),
                    options, nameBuffer_);
                statusMessage_ = "動かしています";
            } else {
                statusMessage_ = "シーンにリグ Feature がありません";
            }
        }

        UI::SameLine();
        if (ImGui::Button("■ 止める")) {
            CameraRig::Deactivate();
            statusMessage_ = "止めました（ゲーム側の追従へ戻ります）";
        }

        UI::SameLine();
        ImGui::SetNextItemWidth(110.0f);
        UI::DragFloat("繋ぎ", activateBlendSeconds_, 0.05f, 0.0f, 5.0f, "%.2f 秒");
        UI::SameLine();
        UI::Hint("動かし始めるとき、今の構図から何秒かけて移るか");

        // いま誰がカメラを握っているか。動かしたのに変わらないときの原因がここで分かる。
        if (runtime && runtime->IsActive()) {
            const float weight = runtime->GetBlendWeight();
            ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.45f, 1.0f),
                "動作中: \"%s\"  繋ぎ %.0f%%",
                runtime->GetName().empty() ? "(無名)" : runtime->GetName().c_str(),
                weight * 100.0f);
            UI::SameLine();
            UI::Hint("Ctrl+S でシーンを保存すると、動作中のこのリグが"
                "シーンの開始リグとして記録されます。次回そのシーンを開くと自動で動きます。");
        } else {
            ImGui::TextDisabled("停止中（カメラはゲーム側の追従が握っています）");
            UI::SameLine();
            UI::Hint("止めた状態で Ctrl+S すると、シーンの開始リグの指定も外れます。");
        }
    }

    bool CameraRigEditorModule::DrawObjectPicker(const char* label, std::string& objectName,
        const CameraEditorContext& context)
    {
        bool changed = false;
        const char* preview = objectName.empty() ? "(なし)" : objectName.c_str();

        if (ImGui::BeginCombo(label, preview)) {
            if (ImGui::Selectable("(なし)", objectName.empty())) {
                objectName.clear();
                changed = true;
            }

            if (context.gameObjectManager) {
                for (const auto& object : context.gameObjectManager->GetAllObjects()) {
                    if (!object) {
                        continue;
                    }
                    const std::string& name = object->GetName();
                    const bool selected = (name == objectName);
                    if (ImGui::Selectable(name.c_str(), selected)) {
                        objectName = name;
                        changed = true;
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
            }
            ImGui::EndCombo();
        }
        return changed;
    }

    bool CameraRigEditorModule::DrawTargetRef(const char* label, CameraRigTargetRef& target,
        const CameraEditorContext& context, bool showWeight)
    {
        bool changed = false;
        ImGui::PushID(label);

        changed |= DrawObjectPicker(label, target.objectName, context);
        changed |= UI::DragVec3("ずらし", target.offset, 0.05f);
        UI::SameLine();
        UI::Hint("足元ではなく胸を見る、といった調整");

        if (showWeight) {
            changed |= UI::DragFloat("重み", target.weight, 0.05f, 0.0f, 10.0f, "%.2f");
            UI::SameLine();
            UI::Hint("大きいほどこの対象へ寄る。0 で数に入れない");
        }

        ImGui::PopID();
        return changed;
    }

    bool CameraRigEditorModule::DrawTargetList(CameraRigTargetRef*,
        std::vector<CameraRigTargetRef>& targets, const CameraEditorContext& context)
    {
        bool changed = false;

        if (ImGui::SmallButton("＋ 対象を足す")) {
            targets.emplace_back();
            changed = true;
        }
        UI::SameLine();
        ImGui::TextDisabled("%d 件", static_cast<int>(targets.size()));

        int removeIndex = -1;
        for (int i = 0; i < static_cast<int>(targets.size()); ++i) {
            ImGui::PushID(i);
            ImGui::SeparatorText(targets[i].objectName.empty()
                ? "(未設定)" : targets[i].objectName.c_str());

            char label[64]{};
            std::snprintf(label, sizeof(label), "対象 %d", i + 1);
            changed |= DrawTargetRef(label, targets[i], context, true);

            if (ImGui::SmallButton("この対象を外す")) {
                removeIndex = i;
            }
            ImGui::PopID();
        }

        if (removeIndex >= 0) {
            targets.erase(targets.begin() + removeIndex);
            changed = true;
        }
        return changed;
    }

    void CameraRigEditorModule::DrawBody(const CameraEditorContext& context)
    {
        bool changed = EnumCombo("決め方", rig_.body.mode, kBodyModeLabels);

        switch (rig_.body.mode) {
        case CameraRigBodyMode::Fixed:
            changed |= UI::DragVec3("位置", rig_.body.position, 0.1f);
            changed |= UI::DragVec3("向き (度)", rig_.body.rotation, 0.5f, -360.0f, 360.0f);
            UI::Hint("向きは「向き」タブが FollowBody のときだけ使われます。");

            if (ImGui::SmallButton("今のカメラの位置と向きを取り込む")) {
                if (Camera* camera = context.cameraManager->GetViewCamera()) {
                    rig_.body.position = camera->GetTranslate();
                    rig_.body.rotation = camera->GetRotate();
                    changed = true;
                }
            }
            break;

        case CameraRigBodyMode::FollowTarget:
            changed |= DrawTargetRef("付いていく対象", rig_.body.target, context, false);
            changed |= UI::DragVec3("オフセット", rig_.body.offset, 0.1f);
            changed |= EnumCombo("オフセットの向き", rig_.body.offsetSpace, kOffsetSpaceLabels);
            UI::Hint("背後に付く TPS なら「対象の向きに追従」、見下ろし固定なら「ワールド軸」。");
            break;

        case CameraRigBodyMode::OrbitTarget:
            changed |= DrawTargetRef("回る中心の対象", rig_.body.target, context, false);
            changed |= UI::DragFloat("距離", rig_.body.orbitDistance, 0.1f, 0.0f, 500.0f, "%.2f m");
            changed |= DragAngleDegrees("方位角", rig_.body.orbitYaw, 0.5f, -360.0f, 360.0f);
            UI::SameLine();
            UI::Hint("0 で対象の背後。対象の向きに乗ります");
            changed |= DragAngleDegrees("仰角", rig_.body.orbitPitch, 0.5f, -89.0f, 89.0f);
            UI::SameLine();
            UI::Hint("正で見下ろし");
            break;

        case CameraRigBodyMode::FrameTargets:
            changed |= UI::DragVec3("オフセット", rig_.body.offset, 0.1f);
            changed |= UI::SliderFloat("寄り X", rig_.body.frameBias.x, 0.0f, 1.0f, "%.2f");
            changed |= UI::SliderFloat("寄り Y", rig_.body.frameBias.y, 0.0f, 1.0f, "%.2f");
            changed |= UI::SliderFloat("寄り Z", rig_.body.frameBias.z, 0.0f, 1.0f, "%.2f");
            UI::Hint("0 = 先頭の対象 / 0.5 = まん中 / 1 = 末尾の対象。"
                "軸ごとに指定できるので「横だけ片方へ寄せる」が作れます。");
            if (ImGui::SmallButton("3 軸そろえる (0.5)")) {
                rig_.body.frameBias = { 0.5f, 0.5f, 0.5f };
                changed = true;
            }
            changed |= UI::DragFloat("広がり 1m あたり引く量",
                rig_.body.framePullBackPerMeter, 0.01f, 0.0f, 5.0f, "%.2f m");
            UI::SameLine();
            UI::Hint("離れたら後ろへ下がって両方を収める。0 なら下がらない");
            changed |= UI::DragFloat("引く量の上限",
                rig_.body.framePullBackMax, 0.1f, 0.0f, 200.0f, "%.2f m");
            UI::SameLine();
            UI::Hint("ここまでしか下がらない。0 なら無制限。"
                "見せたくない外側が画に入るのを止めたいときに使う");
            changed |= DrawTargetList(nullptr, rig_.body.targets, context);
            break;

        case CameraRigBodyMode::Rail: {
            changed |= UI::Widgets::ToggleSwitch("対象に合わせて滑る", &rig_.body.railFollowTarget);
            if (rig_.body.railFollowTarget) {
                changed |= DrawTargetRef("追いかける対象", rig_.body.target, context, false);
            } else {
                changed |= UI::SliderFloat("レール上の位置", rig_.body.railPosition,
                    0.0f, 1.0f, "%.3f");
            }
            changed |= UI::Widgets::ToggleSwitch("レールを環状に閉じる", &rig_.body.railLoop);
            changed |= UI::DragVec3("レールへのずらし", rig_.body.railOffset, 0.1f);

            ImGui::SeparatorText("制御点");
            if (ImGui::SmallButton("＋ 今のカメラ位置を足す")) {
                if (Camera* camera = context.cameraManager->GetViewCamera()) {
                    rig_.body.railPoints.push_back(camera->GetTranslate());
                    changed = true;
                }
            }
            UI::SameLine();
            if (ImGui::SmallButton("＋ 原点を足す")) {
                rig_.body.railPoints.emplace_back();
                changed = true;
            }
            UI::SameLine();
            ImGui::TextDisabled("%d 点（2 点以上で有効）",
                static_cast<int>(rig_.body.railPoints.size()));

            int removeIndex = -1;
            for (int i = 0; i < static_cast<int>(rig_.body.railPoints.size()); ++i) {
                ImGui::PushID(i);
                char label[32]{};
                std::snprintf(label, sizeof(label), "%d", i + 1);
                changed |= UI::DragVec3(label, rig_.body.railPoints[i], 0.1f);
                UI::SameLine();
                if (ImGui::SmallButton("削除")) {
                    removeIndex = i;
                }
                ImGui::PopID();
            }
            if (removeIndex >= 0) {
                rig_.body.railPoints.erase(rig_.body.railPoints.begin() + removeIndex);
                changed = true;
            }
            break;
        }
        }

        if (changed) {
            dirty_ = true;
        }
    }

    void CameraRigEditorModule::DrawAim(const CameraEditorContext& context)
    {
        bool changed = EnumCombo("決め方", rig_.aim.mode, kAimModeLabels);

        switch (rig_.aim.mode) {
        case CameraRigAimMode::FollowBody:
            UI::Hint("「位置」タブの向きをそのまま使います。対象は要りません。");
            break;

        case CameraRigAimMode::LookAtTarget:
            changed |= DrawTargetRef("見る対象", rig_.aim.target, context, false);
            break;

        case CameraRigAimMode::FrameTargets:
            changed |= DrawTargetList(nullptr, rig_.aim.targets, context);
            break;
        }

        if (rig_.aim.mode != CameraRigAimMode::FollowBody) {
            ImGui::SeparatorText("画面のどこに置くか");
            changed |= UI::SliderFloat("横", rig_.aim.screenX, 0.0f, 1.0f, "%.2f");
            UI::SameLine();
            UI::Hint("0 = 左端 / 0.5 = 中央 / 1 = 右端");
            changed |= UI::SliderFloat("縦", rig_.aim.screenY, 0.0f, 1.0f, "%.2f");
            UI::SameLine();
            UI::Hint("0 = 上端 / 0.5 = 中央 / 1 = 下端");

            if (ImGui::SmallButton("中央へ戻す")) {
                rig_.aim.screenX = 0.5f;
                rig_.aim.screenY = 0.5f;
                changed = true;
            }
            UI::SameLine();
            UI::Hint("対象を少し下へ置くと、前方が広く見えます");

            changed |= DragAngleDegrees("画面の傾き", rig_.aim.roll, 0.2f, -45.0f, 45.0f);
        }

        if (changed) {
            dirty_ = true;
        }
    }

    void CameraRigEditorModule::DrawLens()
    {
        bool changed = EnumCombo("決め方", rig_.lens.mode, kLensModeLabels);

        if (rig_.lens.mode == CameraRigLensMode::Fixed) {
            changed |= UI::SliderFloat("視野角", rig_.lens.fovDegrees,
                CameraRigAsset::kMinFovDegrees, 120.0f, "%.1f 度");
        } else {
            const bool byDistance = (rig_.lens.mode == CameraRigLensMode::DistanceToFov);
            if (byDistance) {
                changed |= EnumCombo("測る距離", rig_.lens.distanceSource, kDistanceSourceLabels);
            }

            const char* format = byDistance ? "%.2f m" : "%.2f m/s";
            changed |= UI::DragFloat("この値のとき", rig_.lens.inputMin, 0.1f, 0.0f, 500.0f, format);
            UI::SameLine();
            changed |= UI::SliderFloat("この視野角", rig_.lens.fovMinDegrees,
                CameraRigAsset::kMinFovDegrees, 120.0f, "%.1f 度");

            changed |= UI::DragFloat("この値まで", rig_.lens.inputMax, 0.1f, 0.0f, 500.0f, format);
            UI::SameLine();
            changed |= UI::SliderFloat("この視野角まで", rig_.lens.fovMaxDegrees,
                CameraRigAsset::kMinFovDegrees, 120.0f, "%.1f 度");

            UI::Hint(byDistance
                ? "離れたら広く、寄ったら狭く写します。"
                : "速く動くほど広がって、スピード感が出ます。");
        }

        if (changed) {
            dirty_ = true;
        }
    }

    void CameraRigEditorModule::DrawDamping()
    {
        UI::Hint("1 秒あたりどれだけ目標へ近づくか。0 で減衰なし（ぴったり追う）。");
        UI::Spacing();

        bool changed = false;
        changed |= EnumCombo("寄せ方", rig_.damping.mode, kDampingModeLabels);
        UI::Hint("バネは速度を持って寄るので、グリッド単位で飛ぶ対象を追ってもカクつかない。"
            "位置・注視先・視野角に効き、向きはその 2 つから引き直される。");
        UI::Spacing();

        changed |= UI::DragFloat("位置", rig_.damping.position, 0.1f, 0.0f, 60.0f, "%.2f");
        changed |= UI::DragFloat("向き", rig_.damping.rotation, 0.1f, 0.0f, 60.0f, "%.2f");
        changed |= UI::DragFloat("視野角", rig_.damping.fov, 0.1f, 0.0f, 60.0f, "%.2f");
        changed |= UI::DragFloat("注視先", rig_.damping.aim, 0.1f, 0.0f, 60.0f, "%.2f");
        UI::SameLine();
        UI::Hint("対象が跳ねても画がぶれないようにする");

        UI::Spacing();
        // 速さを一括で置き直すだけのボタン。寄せ方は別の選択なので巻き込まない。
        const CameraRigDampingMode dampingMode = rig_.damping.mode;
        if (ImGui::SmallButton("全部そろえる (3.0)")) {
            rig_.damping = { 3.0f, 3.0f, 3.0f, 3.0f };
            rig_.damping.mode = dampingMode;
            changed = true;
        }
        UI::SameLine();
        if (ImGui::SmallButton("減衰なし")) {
            rig_.damping = { 0.0f, 0.0f, 0.0f, 0.0f };
            rig_.damping.mode = dampingMode;
            changed = true;
        }

        if (changed) {
            dirty_ = true;
        }
    }
}

#endif // USE_IMGUI
