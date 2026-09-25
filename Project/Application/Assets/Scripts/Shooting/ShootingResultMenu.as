// リザルト画面：スコアを数え上げ、ハイスコアを更新して、「もう一度」か「タイトルへ」で移る
[DisplayName("シューティング：リザルト")]
class ShootingResultMenu : ScriptComponent
{
    [ObjectRef] [Tooltip("結果の見出し（STAGE CLEAR / GAME OVER）")]
    GameObject@ headline;

    [ObjectRef] [Tooltip("スコアの数字")]
    GameObject@ scoreValue;

    [ObjectRef] [Tooltip("撃破数とボーナスの文字")]
    GameObject@ detail;

    [ObjectRef] [Tooltip("ハイスコアの文字")]
    GameObject@ best;

    [ObjectRef] [Tooltip("ハイスコアを更新したときに出す文字")]
    GameObject@ newRecord;

    [ObjectRef] [Tooltip("もう一度遊ぶボタン")]
    GameObject@ retryButton;

    [ObjectRef] [Tooltip("タイトルへ戻るボタン")]
    GameObject@ titleButton;

    [Tooltip("「もう一度」で移るシーン")]
    string gameScene = "ShootingGame";

    [Tooltip("「タイトルへ」で移るシーン")]
    string titleScene = "ShootingTitle";

    [Tooltip("ハイスコアを持つファイル（Application/Saved からの相対パス）")]
    string recordFile = "Shooting/Record.json";

    [Tooltip("スコアを数え上げる秒数")]
    float countUpTime = 1.2f;

    [Color] [Tooltip("クリアしたときの見出しの色")]
    Vector4 clearColor = Vector4(0.45f, 1.0f, 0.55f, 1.0f);

    [Color] [Tooltip("ゲームオーバーのときの見出しの色")]
    Vector4 gameOverColor = Vector4(1.0f, 0.35f, 0.3f, 1.0f);

    private int score_ = 0;
    private bool isNewRecord_ = false;
    private bool counted_ = false;
    private bool leaving_ = false;
    private float time_ = 0.0f;

    void Start()
    {
        Time::SetTimeScale(1.0f);

        const bool played = Session::Has("Shooting.Score");
        score_ = Session::GetInt("Shooting.Score", 0);
        const int kills = Session::GetInt("Shooting.Kills", 0);
        const int bonus = Session::GetInt("Shooting.Bonus", 0);
        const bool cleared = Session::GetBool("Shooting.Cleared", false);

        // ハイスコアを読み、超えていれば書き換える
        SaveFile@ record = SaveFile(recordFile);
        const int previousBest = record.GetInt("highScore", 0);
        isNewRecord_ = played && score_ > previousBest;
        if (isNewRecord_) {
            record.SetInt("highScore", score_);
            if (!record.Save()) {
                Warn("シューティング: ハイスコアを保存できませんでした: " + recordFile);
            }
        }

        if (headline !is null) {
            headline.uiText.text = cleared ? "STAGE CLEAR" : "GAME OVER";
            headline.uiText.color = cleared ? clearColor : gameOverColor;
        }
        if (detail !is null) {
            detail.uiText.text = "撃破  " + kills + "　　ボーナス  +" + bonus;
        }
        if (best !is null) {
            best.uiText.text = "ハイスコア  " + formatInt(Max(previousBest, score_), "0", 7);
        }
        if (newRecord !is null) {
            newRecord.active = false;
        }
        SetShownScore(0.0f);
        Tween::To(0.0f, float(score_), countUpTime, TweenFloatSetter(this.SetShownScore))
            .SetEase(EaseType::EaseOutCubic)
            .SetDelay(0.3f)
            .SetLink(owner)
            .OnComplete(TweenCallback(this.OnCountFinished));

        if (retryButton !is null) {
            retryButton.uiButton.Focus();
        }
        Log("シューティング: リザルト（スコア " + score_ + " / ハイスコア更新 " + isNewRecord_ + "）");
    }

    void Update()
    {
        time_ += Time::UnscaledDeltaTime();
        if (counted_ && isNewRecord_ && newRecord !is null) {
            Vector4 color = newRecord.uiText.color;
            color.w = 0.6f + 0.4f * sin(time_ * 6.0f);
            newRecord.uiText.color = color;
        }

        if (leaving_) {
            return;
        }
        // 選択が外れていたら、「もう一度」を選び直す
        if (!UI::HasFocus() && retryButton !is null) {
            retryButton.uiButton.Focus();
        }
        if (retryButton !is null && retryButton.uiButton.wasClicked) {
            Leave(gameScene);
        } else if (titleButton !is null && titleButton.uiButton.wasClicked) {
            Leave(titleScene);
        }
    }

    // 数え上げの途中の表示（Tween から呼ばれる）
    void SetShownScore(float value)
    {
        if (scoreValue !is null) {
            scoreValue.uiText.text = formatInt(int(value + 0.5f), "0", 7);
        }
    }

    // 数え終わった（Tween から呼ばれる）
    void OnCountFinished()
    {
        counted_ = true;
        SetShownScore(float(score_));
        if (isNewRecord_ && newRecord !is null) {
            newRecord.active = true;
            CameraShake::PlayPreset("Landing", 0.5f);
        }
    }

    private void Leave(const string &in sceneName)
    {
        leaving_ = true;
        Scene::ChangeScene(sceneName);
    }
}
