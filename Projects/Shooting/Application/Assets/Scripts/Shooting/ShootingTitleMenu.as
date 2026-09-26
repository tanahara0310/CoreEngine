// タイトル画面：ロゴと自機を見せ、「ゲームスタート」でゲームのシーンへ移る
[DisplayName("シューティング：タイトル")]
class ShootingTitleMenu : ScriptComponent
{
    [ObjectRef] [Tooltip("ロゴの文字")]
    GameObject@ logo;

    [ObjectRef] [Tooltip("ロゴの下の一文")]
    GameObject@ subtitle;

    [ObjectRef] [Tooltip("ゲームスタートのボタン")]
    GameObject@ startButton;

    [ObjectRef] [Tooltip("ゲームスタートのボタンの文字")]
    GameObject@ startLabel;

    [ObjectRef] [Tooltip("ハイスコアを出す文字")]
    GameObject@ highScore;

    [ObjectRef] [Tooltip("決定のしかたを出す文字（点滅させる）")]
    GameObject@ pressHint;

    [ObjectRef] [Tooltip("飛び立つ自機（親のオブジェクト）")]
    GameObject@ ship;

    [Tooltip("移る先のシーン")]
    string gameScene = "ShootingGame";

    [Tooltip("ハイスコアを持つファイル（Application/Saved からの相対パス）")]
    string recordFile = "Shooting/Record.json";

    [Tooltip("押してからシーンを移るまでの秒数")]
    float launchTime = 1.0f;

    private bool starting_ = false;
    private float time_ = 0.0f;
    private Vector2 logoRest_;
    private Vector4 subtitleColor_;
    private Vector4 hintColor_;

    void Start()
    {
        // 時間の速さを元に戻す
        Time::SetTimeScale(1.0f);

        SaveFile@ record = SaveFile(recordFile);
        if (highScore !is null) {
            highScore.uiText.text = "ハイスコア  " + formatInt(record.GetInt("highScore", 0), "0", 7);
        }

        if (logo !is null) {
            logoRest_ = logo.uiText.anchoredPosition;
            Tween::To(logoRest_ + Vector2(0.0f, -360.0f), logoRest_, 0.9f, TweenVector2Setter(this.SetLogoPosition))
                .SetEase(EaseType::EaseOutBack)
                .SetLink(owner);
        }
        if (subtitle !is null) {
            subtitleColor_ = subtitle.uiText.color;
            SetSubtitleAlpha(0.0f);
            Tween::To(0.0f, 1.0f, 0.6f, TweenFloatSetter(this.SetSubtitleAlpha))
                .SetDelay(0.5f)
                .SetLink(owner);
        }
        if (pressHint !is null) {
            hintColor_ = pressHint.uiText.color;
        }
        if (startButton !is null) {
            startButton.uiButton.Focus();
        }
    }

    void Update()
    {
        time_ += Time::UnscaledDeltaTime();
        if (pressHint !is null && !starting_) {
            Vector4 color = hintColor_;
            color.w = hintColor_.w * (0.55f + 0.45f * sin(time_ * 4.0f));
            pressHint.uiText.color = color;
        }

        if (starting_ || startButton is null) {
            return;
        }
        // 選択が外れていたら、スタートのボタンを選び直す
        if (!UI::HasFocus()) {
            startButton.uiButton.Focus();
        }
        if (startButton.uiButton.wasClicked) {
            Launch();
        }
    }

    // ロゴの位置（Tween から呼ばれる）
    void SetLogoPosition(const Vector2 &in position)
    {
        logo.uiText.anchoredPosition = position;
    }

    // ロゴの下の一文の不透明度（Tween から呼ばれる）
    void SetSubtitleAlpha(float alpha)
    {
        Vector4 color = subtitleColor_;
        color.w = subtitleColor_.w * alpha;
        subtitle.uiText.color = color;
    }

    // ゲームのシーンへ移る（Tween から呼ばれる）
    void GoToGame()
    {
        Scene::ChangeScene(gameScene);
    }

    // 自機を奥へ飛ばし、少し待ってからゲームへ移る
    private void Launch()
    {
        starting_ = true;
        if (startLabel !is null) {
            startLabel.uiText.text = "GO!";
        }
        if (pressHint !is null) {
            pressHint.active = false;
        }
        CameraShake::PlayPreset("Recoil", 0.6f);

        if (ship !is null) {
            ShootingSpinner@ spinner;
            if (ship.GetComponent(@spinner)) {
                spinner.Halt();
            }
            // 向きを正面へ戻してから、奥へ飛ばす
            Vector3 rotation = ship.transform.rotation;
            rotation.y = fmod(rotation.y, 6.2831855f);
            if (rotation.y < 0.0f) {
                rotation.y += 6.2831855f;
            }
            ship.transform.rotation = rotation;
            const float facing = rotation.y > 3.1415927f ? 6.2831855f : 0.0f;
            Tween::RotateTo(ship, Vector3(0.0f, facing, 0.0f), 0.3f)
                .SetEase(EaseType::EaseOutQuad)
                .SetLink(ship);
            Tween::MoveTo(ship, ship.transform.position + Vector3(0.0f, 0.5f, 40.0f), 0.8f)
                .SetEase(EaseType::EaseInBack)
                .SetDelay(0.2f)
                .SetLink(ship);
        }
        Tween::Delay(launchTime, TweenCallback(this.GoToGame)).SetLink(owner);
    }
}
