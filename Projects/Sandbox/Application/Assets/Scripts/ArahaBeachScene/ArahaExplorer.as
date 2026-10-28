using namespace CoreEngine;

// アラハビーチを歩き回り、見渡すための操作をまとめる
// プレイヤー視点と広域カメラを切り替え、左上に操作の一覧を出し、速さなどを変える設定の画面を開く
// UI の部品はシーンに置いたものを名前で探す（Tools/OkinawaBeach/export_araha_scene.py が置く）
[DisplayName("アラハビーチ：探索の操作と表示")]
class ArahaExplorer : ScriptComponent
{
    [ObjectRef] [Tooltip("プレイヤー（BeachPlayer を付けたオブジェクト）")]
    GameObject@ player;

    [ObjectRef] [Tooltip("広域カメラ（ArahaFlyCamera を付けたオブジェクト）")]
    GameObject@ flyCamera;

    [Tooltip("設定を残すファイル（Application/Saved からの相対パス。空なら残さない）")]
    string saveFile = "ArahaExplore.json";

    [Range(1.0f, 10.0f)] [Tooltip("知らせの文を出しておく時間 s")]
    float messageSeconds = 3.0f;

    [Range(0.0f, 200.0f)] [Tooltip("真下に降ろせないとき、降ろせる地面を探す半径 m")]
    float placeSearchRadius = 40.0f;

    private BeachPlayer@ player_;
    private ArahaFlyCamera@ camera_;

    private GameObject@ helpPanel_;
    private GameObject@ helpText_;
    private GameObject@ statusText_;
    private GameObject@ settingsButton_;
    private GameObject@ settingsButtonLabel_;
    private GameObject@ settingsPanel_;
    private GameObject@ settingsTitle_;
    private GameObject@ closeButton_;
    private GameObject@ closeLabel_;
    private GameObject@ resetButton_;
    private GameObject@ resetLabel_;
    private GameObject@ settingsHint_;
    private array<GameObject@> rowLabels_;
    private array<GameObject@> rowSliders_;
    private array<GameObject@> rowFills_;
    private array<GameObject@> rowHandles_;
    private array<GameObject@> rowValues_;

    private bool connected_ = false;
    private bool cameraView_ = false;
    private bool settingsOpen_ = false;
    private bool gamepad_ = false;
    private string message_ = "";
    private float messageTime_ = 0.0f;
    private array<int> shown_;
    private array<float> sliderValues_;
    private array<float> defaults_;

    // ===== 設定の項目（添字が項目の番号） =====
    private array<string> labels_ = { "歩く速さ", "走る速さ", "進む速さ", "速くするときの倍率", "上昇・下降の速さ", "向きを変える速さ", "視野角" };
    private array<string> keys_ = { "walkSpeed", "runSpeed", "flySpeed", "flyBoost", "flyClimb", "flyLook", "flyFov" };
    private array<string> units_ = { " m/s", " m/s", " m/s", " 倍", " m/s", " 倍", " 度" };
    private array<float> mins_ = { 0.5f, 1.0f, 1.0f, 1.0f, 1.0f, 0.2f, 20.0f };
    private array<float> maxs_ = { 30.0f, 80.0f, 500.0f, 10.0f, 300.0f, 4.0f, 100.0f };
    private array<bool> logScale_ = { true, true, true, false, true, true, false };
    private array<int> decimals_ = { 1, 1, 0, 1, 0, 1, 0 };
    private array<int> playerSettings_ = { 0, 1 };
    private array<int> cameraSettings_ = { 2, 3, 4, 5, 6 };

    // ===== 設定の画面の並び（画面の右上からの距離 px） =====
    private float panelWidth_ = 600.0f;
    private float margin_ = 16.0f;
    private float padding_ = 24.0f;
    private float rowTop_ = 84.0f;
    private float rowStep_ = 66.0f;
    private int rowCount_ = 5;

    void Start()
    {
        Connect();
        CaptureDefaults();
        LoadSettings();
        ApplyView();
        CloseSettings(false);
    }

    void OnScriptReloaded()
    {
        Connect();
        CaptureDefaults();
        ApplyView();
        CloseSettings(false);
    }

    void Update()
    {
        if (!connected_) {
            return;
        }
        gamepad_ = Input::GetConnectedGamepadCount() > 0;

        if (Input::IsActionTriggered(InputAction::Pause) || Clicked(settingsButton_)) {
            if (settingsOpen_) {
                CloseSettings(true);
            } else {
                OpenSettings();
            }
        } else if (settingsOpen_ && (Input::IsActionTriggered(InputAction::UICancel) || Clicked(closeButton_))) {
            CloseSettings(true);
        }
        if (settingsOpen_ && Clicked(resetButton_)) {
            ResetSettings();
        }

        if (Input::IsActionTriggered(InputAction::SwitchView)) {
            cameraView_ = !cameraView_;
            ApplyView();
        }
        if (cameraView_ && Input::IsActionTriggered(InputAction::PlacePlayer)) {
            PlacePlayerBelowCamera();
        }

        if (settingsOpen_) {
            SyncSliders();
        }
        if (messageTime_ > 0.0f) {
            messageTime_ -= Time::UnscaledDeltaTime();
        }
        RefreshHelp();
        LayoutHelp();
    }

    // ===== つなぐ =====

    private void Connect()
    {
        @player_ = null;
        @camera_ = null;
        if (player !is null) {
            player.GetComponent(@player_);
        }
        if (flyCamera !is null) {
            flyCamera.GetComponent(@camera_);
        }
        if (player_ is null) {
            Warn("ArahaExplorer: player に BeachPlayer がありません");
        }
        if (camera_ is null) {
            Warn("ArahaExplorer: flyCamera に ArahaFlyCamera がありません");
        }

        @helpPanel_ = Find("ExploreHelpPanel");
        @helpText_ = Find("ExploreHelpText");
        @statusText_ = Find("ExploreStatusText");
        @settingsButton_ = Find("ExploreSettingsButton");
        @settingsButtonLabel_ = Find("ExploreSettingsButtonLabel");
        @settingsPanel_ = Find("ExploreSettingsPanel");
        @settingsTitle_ = Find("ExploreSettingsTitle");
        @closeButton_ = Find("ExploreCloseButton");
        @closeLabel_ = Find("ExploreCloseLabel");
        @resetButton_ = Find("ExploreResetButton");
        @resetLabel_ = Find("ExploreResetLabel");
        @settingsHint_ = Find("ExploreSettingsHint");
        rowLabels_.resize(0);
        rowSliders_.resize(0);
        rowFills_.resize(0);
        rowHandles_.resize(0);
        rowValues_.resize(0);
        for (int i = 0; i < rowCount_; i++) {
            rowLabels_.insertLast(Find("ExploreRowLabel" + i));
            rowSliders_.insertLast(Find("ExploreRowSlider" + i));
            rowFills_.insertLast(Find("ExploreRowFill" + i));
            rowHandles_.insertLast(Find("ExploreRowHandle" + i));
            rowValues_.insertLast(Find("ExploreRowValue" + i));
        }
        sliderValues_.resize(rowCount_);
        connected_ = player_ !is null && camera_ !is null;
    }

    private GameObject@ Find(const string &in name)
    {
        GameObject@ found = owner.FindObject(name);
        if (found is null) {
            Warn("ArahaExplorer: UI の " + name + " がシーンにありません");
        }
        return found;
    }

    // ===== 視点 =====

    private void ApplyView()
    {
        if (camera_ !is null) {
            camera_.SetControlled(cameraView_);
        }
        if (player_ !is null) {
            player_.SetControlled(!cameraView_);
        }
        if (settingsOpen_) {
            ShowSettingRows();
        }
    }

    // 広域カメラの真下へプレイヤーを立たせ、プレイヤー視点へ戻す
    // 真下に立てなければ、まわりを 2m ずつ広げて placeSearchRadius まで探す
    private void PlacePlayerBelowCamera()
    {
        const Vector3 at = flyCamera.transform.position;
        const float facing = camera_.GetYaw();
        const int rings = int(placeSearchRadius / 2.0f);
        for (int ring = 0; ring <= rings; ring++) {
            const float radius = 2.0f * float(ring);
            const int count = Max(1, ring * 6);
            for (int k = 0; k < count; k++) {
                const float angle = 6.2831853f * float(k) / float(count);
                if (player_.PlaceAt(at.x + cos(angle) * radius, at.z + sin(angle) * radius, facing)) {
                    cameraView_ = false;
                    ApplyView();
                    Notify(ring == 0 ? "カメラの真下へプレイヤーを降ろしました"
                                     : "真下は建物か海なので、近くの地面へプレイヤーを降ろしました");
                    return;
                }
            }
        }
        Notify("近くに降ろせる地面がありません（海の上・建物の中・地面の表の外）");
    }

    private void Notify(const string &in text)
    {
        message_ = text;
        messageTime_ = messageSeconds;
    }

    // ===== 左上の表示 =====

    private void RefreshHelp()
    {
        SetText(helpText_, HelpText());
        string status;
        if (messageTime_ > 0.0f) {
            status = message_;
        } else if (cameraView_) {
            status = "高さ " + formatFloat(camera_.HeightAboveGround(), "", 0, 0) + " m　進む速さ "
                   + FormatValue(2, camera_.moveSpeed);
        } else {
            status = "速さ " + formatFloat(player_.GetSpeed(), "", 0, 1) + " m/s（歩き "
                   + formatFloat(player_.walkSpeed, "", 0, 1) + "・走り " + formatFloat(player_.runSpeed, "", 0, 1) + "）";
        }
        SetText(statusText_, status);
        SetText(settingsButtonLabel_, settingsOpen_ ? "設定を閉じる" : "設定を開く");
    }

    private string HelpText() const
    {
        if (cameraView_) {
            if (gamepad_) {
                return "広域カメラ（コントローラー）\n"
                     + "左スティック：移動\n"
                     + "RT・LT：上昇・下降\n"
                     + "左スティック押し込み：速く動く\n"
                     + "右スティック：向きを変える\n"
                     + "X：真下へプレイヤーを降ろす\n"
                     + "Y：プレイヤー視点へ切り替え\n"
                     + "メニューボタン：設定（速さなど）";
            }
            return "広域カメラ（キーボード・マウス）\n"
                 + "WASD：移動　E・Q：上昇・下降\n"
                 + "左 Shift：速く動く\n"
                 + "矢印キー・右ドラッグ：向きを変える\n"
                 + "ホイール：進む速さを変える\n"
                 + "F：真下へプレイヤーを降ろす\n"
                 + "Tab：プレイヤー視点へ切り替え\n"
                 + "Esc：設定（速さなど）";
        }
        if (gamepad_) {
            return "プレイヤー視点（コントローラー）\n"
                 + "左スティック：移動\n"
                 + "左スティック押し込み：走る\n"
                 + "右スティック：カメラを回す\n"
                 + "Y：広域カメラへ切り替え\n"
                 + "メニューボタン：設定（速さなど）";
        }
        return "プレイヤー視点（キーボード・マウス）\n"
             + "WASD：移動　左 Shift：走る\n"
             + "矢印キー・右ドラッグ：カメラを回す\n"
             + "ホイール：カメラを寄せる・離す\n"
             + "Tab：広域カメラへ切り替え\n"
             + "Esc：設定（速さなど）";
    }

    // 文の大きさに合わせて、下地と文と設定のボタンを左上から並べる
    private void LayoutHelp()
    {
        if (helpText_ is null || statusText_ is null) {
            return;
        }
        const Vector2 helpSize = helpText_.uiText.measuredSize;
        const Vector2 statusSize = statusText_.uiText.measuredSize;
        const float left = margin_ + 16.0f;
        const float top = margin_ + 12.0f;
        Place(helpText_, Vector2(left, top));
        const float statusTop = top + helpSize.y + 12.0f;
        Place(statusText_, Vector2(left, statusTop));
        const float width = Max(Max(helpSize.x, statusSize.x) + 32.0f, 420.0f);
        const float bottom = statusTop + statusSize.y + 12.0f;
        if (helpPanel_ !is null) {
            UIImage@ image = helpPanel_.uiImage;
            image.anchoredPosition = Vector2(margin_, margin_);
            image.size = Vector2(width, bottom - margin_);
        }
        if (settingsButton_ !is null) {
            UIImage@ image = settingsButton_.uiImage;
            image.anchoredPosition = Vector2(margin_, bottom + 10.0f);
            image.size = Vector2(200.0f, 44.0f);
            Place(settingsButtonLabel_, Vector2(margin_ + 100.0f, bottom + 32.0f));
        }
    }

    // ===== 設定の画面 =====

    private void OpenSettings()
    {
        settingsOpen_ = true;
        SetActive(settingsPanel_, true);
        SetActive(settingsTitle_, true);
        SetActive(closeButton_, true);
        SetActive(closeLabel_, true);
        SetActive(resetButton_, true);
        SetActive(resetLabel_, true);
        SetActive(settingsHint_, true);
        ShowSettingRows();
        // コントローラーでは選ぶ相手が要るので、閉じるボタンから始める
        if (gamepad_ && closeButton_ !is null) {
            closeButton_.uiButton.Focus();
        }
    }

    private void CloseSettings(bool save)
    {
        settingsOpen_ = false;
        SetActive(settingsPanel_, false);
        SetActive(settingsTitle_, false);
        SetActive(closeButton_, false);
        SetActive(closeLabel_, false);
        SetActive(resetButton_, false);
        SetActive(resetLabel_, false);
        SetActive(settingsHint_, false);
        for (int i = 0; i < rowCount_; i++) {
            ShowRow(i, false);
        }
        UI::ClearFocus();
        if (save) {
            SaveSettings();
        }
    }

    // 今の視点の項目だけを出し、画面の大きさを合わせる
    private void ShowSettingRows()
    {
        if (cameraView_) {
            shown_ = cameraSettings_;
        } else {
            shown_ = playerSettings_;
        }
        SetText(settingsTitle_, cameraView_ ? "設定：広域カメラ" : "設定：プレイヤー");
        SetText(closeLabel_, "閉じる");
        SetText(resetLabel_, "既定に戻す");
        SetText(settingsHint_, gamepad_ ? "上下：項目を選ぶ　左右：値を変える\nB・メニューボタン：閉じる"
                                        : "つまみをドラッグして変える\nEsc：閉じる");
        for (int i = 0; i < rowCount_; i++) {
            const bool visible = i < int(shown_.length());
            ShowRow(i, visible);
            if (visible) {
                const int id = shown_[i];
                SetText(rowLabels_[i], labels_[id]);
                if (rowSliders_[i] !is null) {
                    UISlider@ slider = rowSliders_[i].uiSlider;
                    slider.value = ToSlider(id, GetValue(id));
                    sliderValues_[i] = slider.value;
                }
                SetText(rowValues_[i], FormatValue(id, GetValue(id)));
            }
        }
        LayoutSettings(int(shown_.length()));
    }

    private void ShowRow(int i, bool visible)
    {
        if (i >= int(rowLabels_.length())) {
            return;
        }
        SetActive(rowLabels_[i], visible);
        SetActive(rowSliders_[i], visible);
        SetActive(rowFills_[i], visible);
        SetActive(rowHandles_[i], visible);
        SetActive(rowValues_[i], visible);
    }

    private void LayoutSettings(int count)
    {
        const float left = -margin_ - panelWidth_ + padding_;
        const float right = -margin_ - padding_;
        const float top = margin_;
        for (int i = 0; i < count; i++) {
            const float y = top + rowTop_ + rowStep_ * float(i);
            Place(rowLabels_[i], Vector2(left, y));
            Place(rowValues_[i], Vector2(right, y));
            if (rowSliders_[i] !is null) {
                UIImage@ track = rowSliders_[i].uiImage;
                track.anchoredPosition = Vector2(left, y + 36.0f);
                track.size = Vector2(right - left, 18.0f);
            }
        }
        const float bottom = top + rowTop_ + rowStep_ * float(count) + 4.0f;
        Place(settingsTitle_, Vector2(left, top + 22.0f));
        if (closeButton_ !is null) {
            closeButton_.uiImage.anchoredPosition = Vector2(right, top + 16.0f);
            Place(closeLabel_, Vector2(right - 60.0f, top + 38.0f));
        }
        if (resetButton_ !is null) {
            resetButton_.uiImage.anchoredPosition = Vector2(left, bottom);
            Place(resetLabel_, Vector2(left + 100.0f, bottom + 22.0f));
        }
        Place(settingsHint_, Vector2(left + 220.0f, bottom));
        if (settingsPanel_ !is null) {
            UIImage@ panel = settingsPanel_.uiImage;
            panel.anchoredPosition = Vector2(-margin_, margin_);
            panel.size = Vector2(panelWidth_, bottom + 44.0f + padding_ - margin_);
        }
    }

    // スライダーを動かしたら値へ写し、値がほかで変わったら（ホイールなど）スライダーへ写す
    private void SyncSliders()
    {
        for (int i = 0; i < int(shown_.length()); i++) {
            if (rowSliders_[i] is null) {
                continue;
            }
            const int id = shown_[i];
            UISlider@ slider = rowSliders_[i].uiSlider;
            const float now = slider.value;
            if (abs(now - sliderValues_[i]) > 0.00001f) {
                SetValue(id, FromSlider(id, now));
                sliderValues_[i] = now;
            } else {
                const float wanted = ToSlider(id, GetValue(id));
                if (abs(wanted - now) > 0.0001f) {
                    slider.value = wanted;
                    sliderValues_[i] = slider.value;
                }
            }
            SetText(rowValues_[i], FormatValue(id, GetValue(id)));
        }
    }

    private void ResetSettings()
    {
        for (int id = 0; id < int(defaults_.length()); id++) {
            SetValue(id, defaults_[id]);
        }
        ShowSettingRows();
        SaveSettings();
    }

    // ===== 値 =====

    private float GetValue(int id)
    {
        switch (id) {
        case 0: return player_.walkSpeed;
        case 1: return player_.runSpeed;
        case 2: return camera_.moveSpeed;
        case 3: return camera_.boostMultiplier;
        case 4: return camera_.climbSpeed;
        case 5: return camera_.lookSensitivity;
        case 6: return camera_.fieldOfView;
        }
        return 0.0f;
    }

    private void SetValue(int id, float value)
    {
        const float v = Clamp(value, mins_[id], maxs_[id]);
        switch (id) {
        case 0: player_.walkSpeed = v; break;
        case 1: player_.runSpeed = v; break;
        case 2: camera_.moveSpeed = v; break;
        case 3: camera_.boostMultiplier = v; break;
        case 4: camera_.climbSpeed = v; break;
        case 5: camera_.lookSensitivity = v; break;
        case 6: camera_.fieldOfView = v; break;
        }
    }

    // 値をスライダーの 0〜1 へ（倍々で変わる項目は対数で並べる）
    private float ToSlider(int id, float value)
    {
        const float v = Clamp(value, mins_[id], maxs_[id]);
        if (logScale_[id]) {
            return (log(v) - log(mins_[id])) / (log(maxs_[id]) - log(mins_[id]));
        }
        return (v - mins_[id]) / (maxs_[id] - mins_[id]);
    }

    private float FromSlider(int id, float t)
    {
        const float s = Saturate(t);
        float v;
        if (logScale_[id]) {
            v = exp(log(mins_[id]) + (log(maxs_[id]) - log(mins_[id])) * s);
        } else {
            v = mins_[id] + (maxs_[id] - mins_[id]) * s;
        }
        // 出している桁で丸める
        const float step = pow(10.0f, -float(decimals_[id]));
        return Clamp(floor(v / step + 0.5f) * step, mins_[id], maxs_[id]);
    }

    private string FormatValue(int id, float value)
    {
        return formatFloat(value, "", 0, uint(decimals_[id])) + units_[id];
    }

    private void CaptureDefaults()
    {
        defaults_.resize(0);
        if (!connected_) {
            return;
        }
        for (int id = 0; id < int(labels_.length()); id++) {
            defaults_.insertLast(GetValue(id));
        }
    }

    private void LoadSettings()
    {
        if (!connected_ || saveFile == "") {
            return;
        }
        SaveFile@ file = SaveFile(saveFile);
        if (!file.exists) {
            return;
        }
        for (int id = 0; id < int(keys_.length()); id++) {
            if (file.Has(keys_[id])) {
                SetValue(id, file.GetFloat(keys_[id]));
            }
        }
    }

    private void SaveSettings()
    {
        if (!connected_ || saveFile == "") {
            return;
        }
        SaveFile@ file = SaveFile(saveFile);
        for (int id = 0; id < int(keys_.length()); id++) {
            file.SetFloat(keys_[id], GetValue(id));
        }
        file.Save();
    }

    // ===== UI の小道具 =====

    private bool Clicked(GameObject@ button)
    {
        return button !is null && button.active && button.uiButton.exists && button.uiButton.wasClicked;
    }

    private void SetActive(GameObject@ target, bool on)
    {
        if (target !is null && target.active != on) {
            target.active = on;
        }
    }

    private void SetText(GameObject@ target, const string &in text)
    {
        if (target is null) {
            return;
        }
        UIText@ label = target.uiText;
        if (label.exists && label.text != text) {
            label.text = text;
        }
    }

    private void Place(GameObject@ target, const Vector2 &in position)
    {
        if (target is null) {
            return;
        }
        UIText@ label = target.uiText;
        if (label.exists) {
            label.anchoredPosition = position;
        }
    }
}
