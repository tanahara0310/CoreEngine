// 開幕の演出と、目標距離の提示。
// 白幕（雲の中）が晴れ、真上のカメラからゲームの構図へ降り、目標看板 → 「つなげ！！」→ HUD の登場の順に進む。
// 演出のあいだはカーソルの入力だけを止める（トロッコは最初のレールを敷くまで走らないので、進行ごと待つ）。
// 目標の距離に届いたら足元の目盛りを達成の色にする
[DisplayName("開幕の演出と目標")]
class GameEntrance : ScriptComponent
{
    [DisplayName("開幕の演出を出す")]
    bool cinematicEnabled = true;

    [DisplayName("白幕が晴れるまで（秒）"), Range(0.2, 8)]
    float cloudSeconds = 1.40f;

    [DisplayName("開幕の真上のカメラリグ")]
    string skyRig = "Entrance_Sky";

    [DisplayName("ゲームのカメラリグ")]
    string playRig = "GamePlay";

    [DisplayName("ゲームの構図へ降り始めるまで（秒）"), Range(0, 4)]
    float cameraDelay = 0.15f;

    [DisplayName("ゲームの構図へ降り切るまで（秒）"), Range(0.1, 8)]
    float cameraBlendSeconds = 1.50f;

    [DisplayName("目標看板が降りてくる時刻（秒）"), Range(0, 10)]
    float signDelay = 1.50f;

    [DisplayName("「つなげ！！」の時刻（秒）"), Range(0, 12)]
    float callDelay = 3.20f;

    [DisplayName("「つなげ！！」から HUD が出るまで（秒）"), Range(0, 6)]
    float hudRevealOffset = 0.12f;

    [DisplayName("HUD が滑り込む時間（秒）"), Range(0.05, 3)]
    float hudRevealSeconds = 0.45f;

    [DisplayName("HUD が 1 枚ずつ遅れる間隔（秒）"), Range(0, 1)]
    float hudStagger = 0.10f;

    [DisplayName("目標距離（m）"), Range(10, 2000)]
    int goalMeters = 500;

    [DisplayName("目盛りの色")]
    Vector4 markerColor = Vector4(1.0f, 1.0f, 1.0f, 1.0f);

    [DisplayName("届いた目標の目盛りの色")]
    Vector4 reachedColor = Vector4(0.02f, 0.22f, 0.03f, 1.0f);

    [DisplayName("目標の目盛りの色（届くまで脈打つ）")]
    Vector4 targetColor = Vector4(0.25f, 0.010f, 0.008f, 1.0f);

    [DisplayName("白幕の描画順"), Range(0, 8000)]
    int whiteoutSortOrder = 3000;

    private RailBuilder@ builder_;
    private TrainMovement@ train_;
    private MapView@ mapView_;
    private StaminaGauge@ stamina_;
    private SpeedGauge@ speedGauge_;
    private PauseMenu@ pauseMenu_;
    private RailDirectionGuide@ guide_;
    private ObjectiveSign sign_;
    private UIImage@ whiteout_;

    private float elapsed_ = 0.0f;
    private float pulseTimer_ = 0.0f;
    private float reachedFlash_ = 0.6f;
    private bool whiteoutActive_ = false;
    private bool controlLocked_ = false;
    private bool skyRigStarted_ = false;
    private bool playRigStarted_ = false;
    private bool signShown_ = false;
    private bool callPlayed_ = false;
    private bool hudRevealDone_ = false;
    private bool entranceDone_ = false;
    private bool goalReached_ = false;

    void Setup(RailBuilder@ builder, TrainMovement@ train, MapView@ mapView, StaminaGauge@ stamina, SpeedGauge@ speedGauge,
        PauseMenu@ pauseMenu, RailDirectionGuide@ guide)
    {
        @builder_ = builder;
        @train_ = train;
        @mapView_ = mapView;
        @stamina_ = stamina;
        @speedGauge_ = speedGauge;
        @pauseMenu_ = pauseMenu;
        @guide_ = guide;

        sign_.Build(owner);

        // 1 フレーム目から白いよう、ここで作って濃さも入れておく
        @whiteout_ = owner.SpawnUIImage("Application/Assets/Textures/UI/dim.png", "EntranceWhiteout");
        whiteout_.anchor = UIAnchor::Center;
        whiteout_.pivot = Vector2(0.5f, 0.5f);
        whiteout_.anchoredPosition = Vector2(0.0f, 0.0f);
        whiteout_.size = Vector2(1920.0f, 1080.0f);
        whiteout_.sortOrder = whiteoutSortOrder;
        whiteout_.gameObject.active = false;
        if (cinematicEnabled) {
            ApplyWhiteout(0.0f);
            SetControlLocked(true);
        }
        UpdateHudReveal();
    }

    // 毎フレームの最初に呼ぶ
    void Tick(float deltaTime)
    {
        const float step = Clamp(deltaTime, 0.0f, 0.1f);
        UpdateEntrance(step);
        UpdateGoal(step);
        sign_.Tick(step);
    }

    // マップの表示が目盛りを並べ直した後に呼ぶ
    void TickLate(float deltaTime)
    {
        const float step = Clamp(deltaTime, 0.0f, 0.1f);
        const float pulse = 1.0f + sin(pulseTimer_ * 4.0f) * 0.35f;
        const float flash = Clamp(reachedFlash_ / 0.6f, 0.0f, 1.0f);
        const Vector4 goalColor = goalReached_
            ? Lerp(Vector4(1.0f, 1.0f, 1.0f, 1.0f), reachedColor, flash)
            : Vector4(targetColor.x * pulse, targetColor.y * pulse, targetColor.z * pulse, targetColor.w);
        pulseTimer_ += step;
        if (mapView_ !is null) {
            mapView_.PaintDistanceMarkers(goalMeters, goalColor, markerColor);
        }
    }

    // 白幕の濃さ（0 = 開幕の雲の中 ／ 1 = 晴れ切った）
    private void ApplyWhiteout(float progress)
    {
        const float alpha = 1.0f - Ease(EaseType::EaseInOutCubic, Clamp(progress, 0.0f, 1.0f));
        whiteoutActive_ = alpha > 0.0f;
        whiteout_.gameObject.active = whiteoutActive_;
        whiteout_.color = Vector4(1.0f, 1.0f, 1.0f, alpha);
    }

    private void SetControlLocked(bool locked)
    {
        if (builder_ is null || controlLocked_ == locked) {
            return;
        }
        controlLocked_ = locked;
        builder_.SetInputLocked(locked);
    }

    private void UpdateEntrance(float step)
    {
        if (entranceDone_) {
            return;
        }
        elapsed_ += step;

        if (cinematicEnabled) {
            const float progress = elapsed_ / Max(0.01f, cloudSeconds);
            if (progress < 1.0f) {
                ApplyWhiteout(progress);
            } else if (whiteoutActive_) {
                ApplyWhiteout(1.0f);
            }

            // 真上のリグ → ゲームの構図
            if (!skyRigStarted_) {
                skyRigStarted_ = true;
                if (!CameraRig::Activate(skyRig)) {
                    Warn("GameEntrance: リグ " + skyRig + " が無いのでカメラの演出は飛ばします");
                    playRigStarted_ = true;
                }
            }
            if (!playRigStarted_ && elapsed_ >= cameraDelay) {
                playRigStarted_ = true;
                CameraRig::Activate(playRig, Max(0.01f, cameraBlendSeconds));
            }
        }

        const float signAt = cinematicEnabled ? signDelay : 0.3f;
        if (!signShown_ && elapsed_ >= signAt) {
            signShown_ = true;
            sign_.Show(Max(1, goalMeters));
        }

        const float callAt = CallTime();
        if (!callPlayed_ && elapsed_ >= callAt) {
            callPlayed_ = true;
            sign_.PlayStartCall();
            CameraShake::Play(CameraShakePresets::Landing());
            PlaySe("Application/Assets/Sounds/SE/title_bound.mp3");
        }

        UpdateHudReveal();

        if (callPlayed_ && hudRevealDone_ && !whiteoutActive_ && elapsed_ >= callAt + 0.05f) {
            entranceDone_ = true;
            SetControlLocked(false);
        }
    }

    private float CallTime() const
    {
        return cinematicEnabled ? callDelay : 1.6f;
    }

    // HUD は演出中は引っ込めておき、「つなげ！！」の後に 1 枚ずつ行き過ぎて戻る動きで出す
    private void UpdateHudReveal()
    {
        if (hudRevealDone_) {
            return;
        }
        const float start = CallTime() + hudRevealOffset;
        const float span = Max(0.05f, hudRevealSeconds);
        // 床の矢印はワールド側なので、HUD の 1 枚目と同時でも読み分けられる
        if (guide_ !is null) {
            guide_.SetIntroReveal(RevealAt(start, span, 0.0f));
        }
        if (stamina_ !is null) {
            stamina_.SetIntroReveal(RevealAt(start, span, 0.0f));
        }
        if (speedGauge_ !is null) {
            speedGauge_.SetIntroReveal(RevealAt(start, span, hudStagger));
        }
        if (pauseMenu_ !is null) {
            pauseMenu_.SetIntroReveal(RevealAt(start, span, hudStagger * 2.0f));
        }
        if (elapsed_ >= start + hudStagger * 2.0f + span) {
            hudRevealDone_ = true;
        }
    }

    private float RevealAt(float start, float span, float stagger) const
    {
        return Ease(EaseType::EaseOutBack, Clamp((elapsed_ - start - stagger) / span, 0.0f, 1.0f));
    }

    // 目標の距離（ワールドの X がそのままメートル）を越えたら、達成の音を鳴らして目盛りを光らせる
    private void UpdateGoal(float step)
    {
        if (train_ is null) {
            return;
        }
        reachedFlash_ += step;
        if (goalReached_) {
            return;
        }
        if (train_.GetWorldPosition().x < float(goalMeters)) {
            return;
        }
        goalReached_ = true;
        reachedFlash_ = 0.0f;
        PlaySe("Application/Assets/Sounds/SE/decision.mp3");
        Log("GameEntrance: " + goalMeters + "m 到達");
    }

    private void PlaySe(const string &in path)
    {
        PlayParams params;
        params.bus = AudioBus::SE;
        Audio::PlayOneShot(path, params);
    }
}
