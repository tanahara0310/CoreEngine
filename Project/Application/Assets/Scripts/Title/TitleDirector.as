// タイトルの進行。登場の演出が全部終わったら BGM を鳴らし、決定の入力で案内の文字の演出の後に次のシーンへ移る。
// パッドのつなぎ外しが変わったら、案内の文言を替える
[DisplayName("タイトルの進行")]
class TitleDirector : ScriptComponent
{
    [DisplayName("BGM")]
    string bgmPath = "Sounds/BGM/Title_bgm.mp3";

    [DisplayName("BGM の音量"), Range(0, 1)]
    float bgmVolume = 1.0f / 3.0f;

    [DisplayName("決定の音")]
    string decisionSePath = "Sounds/SE/decision.mp3";

    [DisplayName("決定で移るシーン")]
    string nextScene = "GameScene";

    [DisplayName("パッドのときの案内")]
    string gamepadPrompt = "- Aボタンをおしてスタート -";

    [DisplayName("キーボードのときの案内")]
    string keyboardPrompt = "- SPACEキーをおしてスタート -";

    [DisplayName("ロゴのオブジェクトの名前")]
    string logoName = "title";

    [DisplayName("トロッコのオブジェクトの名前")]
    string trolleyName = "trolley";

    [DisplayName("サルのオブジェクトの名前")]
    string monkeyName = "monkey";

    [DisplayName("案内の文字のオブジェクトの名前")]
    string hintName = "StartHint";

    private TitleHint@ hint_;
    private Sound@ bgm_;
    private int pendingIntros_ = 0;
    private bool introsWatched_ = false;
    private bool bgmStarted_ = false;
    private bool gamepadConnected_ = false;
    private bool startRequested_ = false;

    void Start() override
    {
        gamepadConnected_ = Input::IsGamepadConnected();

        WatchIntro(trolleyName);
        WatchIntro(monkeyName);
        WatchIntro(logoName);
        WatchIntro(hintName);
        @hint_ = FindTitleHint(owner, hintName);

        introsWatched_ = true;
        StartBgmIfReady();
    }

    void Update() override
    {
        if (startRequested_) {
            return;
        }

        const bool gamepadConnected = Input::IsGamepadConnected();
        if (gamepadConnected != gamepadConnected_) {
            gamepadConnected_ = gamepadConnected;
            UpdateStartPrompt();
        }

        if (Input::IsActionTriggered(InputAction::UIConfirm)) {
            StartGame();
        }
    }

    void OnDestroy() override
    {
        @bgm_ = null;
        @hint_ = null;
    }

    // 名前で探したオブジェクトの登場の演出が終わるのを待つ数に入れる
    private void WatchIntro(const string &in name)
    {
        TitleIntro@ intro;
        GameObject@ object = owner.FindObject(name);
        if (object is null || !object.GetComponent(@intro)) {
            Warn(owner.name + ": " + name + " の登場の演出が見つかりません");
            return;
        }
        ++pendingIntros_;
        intro.SetOnIntroComplete(TitleIntroCallback(this.OnIntroComplete));
    }

    private void OnIntroComplete()
    {
        if (pendingIntros_ > 0) {
            --pendingIntros_;
        }
        StartBgmIfReady();
    }

    private void StartBgmIfReady()
    {
        if (!introsWatched_ || pendingIntros_ != 0 || bgmStarted_) {
            return;
        }

        PlayParams params;
        params.bus = AudioBus::BGM;
        params.loop = true;
        params.volume = bgmVolume;
        @bgm_ = Audio::PlayScoped(bgmPath, params);
        bgmStarted_ = true;
    }

    private void UpdateStartPrompt()
    {
        GameObject@ hintObject = owner.FindObject(hintName);
        if (hintObject is null) {
            return;
        }
        hintObject.uiText.text = gamepadConnected_ ? gamepadPrompt : keyboardPrompt;
    }

    // 決定の音を鳴らし、案内の文字の演出が終わったら次のシーンへ移る
    private void StartGame()
    {
        if (startRequested_) {
            return;
        }
        startRequested_ = true;

        PlayParams params;
        params.bus = AudioBus::SE;
        Audio::PlayOneShot(decisionSePath, params);

        if (hint_ !is null) {
            hint_.PlayStartReaction(TweenCallback(this.GoToNextScene));
            return;
        }
        GoToNextScene();
    }

    private void GoToNextScene()
    {
        Scene::ChangeScene(nextScene);
    }
}
