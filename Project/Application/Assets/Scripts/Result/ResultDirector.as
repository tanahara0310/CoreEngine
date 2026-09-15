// リザルトの進行。BGM を鳴らし、最初の更新で Tips を出す。左右の入力で選択肢を替え、決定の入力で演出の後に次のシーンへ移る。
// 戻るの入力ではすぐタイトルへ移る
[DisplayName("リザルトの進行")]
class ResultDirector : ScriptComponent
{
    [DisplayName("BGM")]
    string bgmPath = "Sounds/BGM/Result_bgm.mp3";

    [DisplayName("BGM の音量"), Range(0, 1)]
    float bgmVolume = 1.0f / 3.0f;

    [DisplayName("選択肢を替える音")]
    string selectSePath = "Application/Assets/Sounds/SE/rail_build.mp3";

    [DisplayName("決定の音")]
    string decisionSePath = "Sounds/SE/decision.mp3";

    [DisplayName("「もういちど」で移るシーン")]
    string retryScene = "GameScene";

    [DisplayName("「タイトルへ」と戻るで移るシーン")]
    string titleScene = "TitleScene";

    [DisplayName("Tips のオブジェクトの名前")]
    string tipsName = "ResultTipsSettings";

    [DisplayName("ゲージのオブジェクトの名前")]
    string gaugeName = "ResultGauge";

    [DisplayName("次に出す Tips の番号を置く名前（シーンをまたぐ値）")]
    string tipIndexKey = "Result.NextTipIndex";

    private Sound@ bgm_;
    private ResultGauge@ gauge_;
    private bool titleSelected_ = false;
    private bool returnRequested_ = false;
    private bool tipInitialized_ = false;
    private string nextScene_;

    void Awake() override
    {
        PlayParams params;
        params.bus = AudioBus::BGM;
        params.loop = true;
        params.volume = bgmVolume;
        @bgm_ = Audio::PlayScoped(bgmPath, params);
    }

    void Start() override
    {
        GameObject@ gaugeObject = owner.FindObject(gaugeName);
        if (gaugeObject is null || !gaugeObject.GetComponent(@gauge_)) {
            Warn(owner.name + ": " + gaugeName + " の ResultGauge が見つかりません");
        }
    }

    void Update() override
    {
        InitializeTipText();

        if (returnRequested_) {
            return;
        }

        if (Input::IsActionTriggered(InputAction::UICancel)) {
            returnRequested_ = true;
            Scene::ChangeScene(titleScene);
            return;
        }

        const bool left = Input::IsActionTriggered(InputAction::MoveLeft);
        const bool right = Input::IsActionTriggered(InputAction::MoveRight);
        if (left != right) {
            titleSelected_ = !titleSelected_;
            if (gauge_ !is null) {
                gauge_.SetSelection(titleSelected_, true);
            }

            PlayParams params;
            params.bus = AudioBus::SE;
            Audio::PlayOneShot(selectSePath, params);
        }

        if (Input::IsActionTriggered(InputAction::UIConfirm)) {
            Confirm();
        }
    }

    void OnDestroy() override
    {
        @bgm_ = null;
        @gauge_ = null;
    }

    // 前回の続きの番号から、空でない Tips を探して出し、次の番号を控える
    private void InitializeTipText()
    {
        if (tipInitialized_) {
            return;
        }
        tipInitialized_ = true;

        ResultTips@ tips;
        GameObject@ tipsObject = owner.FindObject(tipsName);
        if (tipsObject is null || !tipsObject.GetComponent(@tips)) {
            return;
        }

        const uint count = tips.tips.length();
        if (count == 0) {
            return;
        }

        const uint next = uint(Max(0, Session::GetInt(tipIndexKey, 0)));
        for (uint offset = 0; offset < count; ++offset) {
            const uint index = (next + offset) % count;
            if (tips.tips[index].isEmpty()) {
                continue;
            }
            if (gauge_ !is null) {
                gauge_.SetTipText(tips.tips[index]);
            }
            Session::SetInt(tipIndexKey, int((index + 1) % count));
            return;
        }
    }

    // 決定の音を鳴らし、選んだ木札の演出が終わったら選んだ側のシーンへ移る
    private void Confirm()
    {
        if (returnRequested_) {
            return;
        }
        returnRequested_ = true;

        PlayParams params;
        params.bus = AudioBus::SE;
        Audio::PlayOneShot(decisionSePath, params);

        nextScene_ = titleSelected_ ? titleScene : retryScene;
        if (gauge_ !is null) {
            gauge_.Confirm(titleSelected_, TweenCallback(this.GoToNextScene));
            return;
        }
        GoToNextScene();
    }

    private void GoToNextScene()
    {
        Scene::ChangeScene(nextScene_);
    }
}
