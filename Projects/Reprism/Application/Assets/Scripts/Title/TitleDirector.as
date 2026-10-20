using namespace CoreEngine;

// タイトル画面（元の TitleScene）。ロゴの登場演出・背景・Space でステージ選択へ
enum TitleLogoPhase
{
    Scan,   // 上から写し取るように現れる
    Glitch, // グリッチ
    Echo,   // 残像が広がる
    Idle    // 呼吸する光と、4 秒ごとの残像
}

class TitleLogoEcho
{
    Td3Sprite@ sprite;
    float age = 0.0f;
    float lifetime = 0.8f;
}

[DisplayName("Reprism：タイトル")]
class TitleDirector : ScriptComponent
{
    private Td3SpriteFactory@ factory_;
    private CyberGridBackground grid_;
    private GameBackgroundEffect backgroundEffect_;
    private Td3Sprite@ logoSprite_;
    private ShaderMaterial@ logoMaterial_;
    private array<TitleLogoEcho@> logoEchoes_;
    private TitleLogoPhase logoPhase_ = TitleLogoPhase::Scan;
    private float logoPhaseTimer_ = 0.0f;
    private float logoIdleTime_ = 0.0f;
    private float idleEchoTimer_ = 0.0f;
    private array<Td3UIImage@> uiImages_;
    private Sound@ bgm_;

    // E キーの空の遷移（元は登録の無いシーン名へ切り替えようとして、暗転して戻るだけになる）
    private int emptyTransitionPhase_ = 0;
    private float emptyTransitionTimer_ = 0.0f;
    private int emptyTransitionWaitFrames_ = 0;
    private bool pendingStageSelect_ = false;
    private bool pendingEmptyTransition_ = false;

    private GameObject@ cameraObject_;

    void Start()
    {
        @factory_ = Td3SpriteFactory(owner);

        // 黒い背景
        Td3Sprite@ black = factory_.Create("blackBackGround.png", "blackBackGround");
        black.SetSortingLayer(-10);

        // ロゴ（描画順は既定のまま）
        @logoSprite_ = factory_.Create("UI/UI_TitleLogo.png", "title");
        logoSprite_.SetMaterial("Application/Assets/Materials/TitleLogo.mat");
        @logoMaterial_ = ShaderMaterial("Application/Assets/Materials/TitleLogo.mat");
        logoMaterial_.SetFloat("scanProgress", 0.0f);
        logoMaterial_.SetVector("color", Vector4(1.0f, 1.0f, 1.0f, 1.0f));

        // 残像（3 枚・使い終わった状態で待つ）
        for (int i = 0; i < 3; i++) {
            TitleLogoEcho echo;
            @echo.sprite = factory_.Create("UI/UI_TitleLogo.png", "titleEcho" + i);
            echo.sprite.SetColor(Vector4(1.0f, 0.05f, 0.85f, 0.0f));
            echo.sprite.SetSortingLayer(-1);
            echo.age = echo.lifetime;
            logoEchoes_.insertLast(echo);
        }

        // カメラ（ズーム 1.5）
        @cameraObject_ = owner.FindObject("MainCamera");
        SetCamera(1.5f, Vector2(0.0f, 0.0f));

        PlayParams params;
        params.bus = AudioBus::BGM;
        params.loop = true;
        @bgm_ = Audio::PlayScoped("Application/Assets/Audio/Bgm/Title.mp3", params);

        grid_.Initialize(factory_, "bgGrid_title");
        backgroundEffect_.Initialize(factory_, 0, 0, 36);

        // 案内の画像（元のシーン保存で表示していた 2 枚：ボタンの 8 コマ目と文字の 3 コマ目）
        Td3UIImage@ button = CreateTd3UIImage(owner, "UI/UI_Button.png", "TitleUIButton8");
        button.SetVerticalFrame(14, 8);
        button.SetAnchor(UIAnchor::Center);
        button.SetAnchoredPosition(Vector2(-160.0f, 324.0f));
        button.SetPivot(Vector2(0.5f, 0.5f));
        button.SetSize(Vector2(208.0f, 104.0f));
        button.SetSortOrder(50);
        uiImages_.insertLast(button);

        Td3UIImage@ word = CreateTd3UIImage(owner, "UI/UI_word.png", "TitleUIWord2");
        word.SetVerticalFrame(6, 2);
        word.SetAnchor(UIAnchor::Center);
        word.SetAnchoredPosition(Vector2(70.0f, 324.0f));
        word.SetPivot(Vector2(0.5f, 0.5f));
        word.SetSize(Vector2(312.0f, 78.0f));
        word.SetSortOrder(50);
        uiImages_.insertLast(word);
    }

    void Update()
    {
        const float dt = Time::DeltaTime();

        // 空の遷移の暗転中は、シーンの更新を止める（元の遷移のブロック）
        if (UpdateEmptyTransition(dt)) {
            return;
        }

        grid_.Update(dt, 1.5f, Vector2(0.0f, 0.0f));
        backgroundEffect_.Update(dt);
        UpdateLogo(dt);

        if (ActionTriggered(InputAction::Decide)) {
            PlaySe("SE_Decide.mp3");
            RequestStageSelect();
        }
        if (ActionTriggered(InputAction::OpenEditor)) {
            RequestEmptyTransition();
        }
    }

    private void UpdateLogo(float dt)
    {
        logoPhaseTimer_ += dt;
        logoIdleTime_ += dt;

        const float kScanDuration = 1.4f;
        const float kGlitchDuration = 0.65f;
        const float kEchoDuration = 0.9f;
        const float kEchoSpread = 0.22f;
        const float kEchoSpacing = 0.18f;

        switch (logoPhase_) {
        case TitleLogoPhase::Scan:
        {
            const float t = Clamp(logoPhaseTimer_ / kScanDuration, 0.0f, 1.0f);
            const float scanT = Td3EaseLerp(0.0f, 1.0f, t, Td3Ease::EaseInOutQuad);
            logoMaterial_.SetFloat("scanProgress", scanT);
            logoMaterial_.SetFloat("glitchIntensity", 0.0f);
            logoMaterial_.SetFloat("time", logoIdleTime_);
            if (logoPhaseTimer_ >= kScanDuration) {
                logoPhase_ = TitleLogoPhase::Glitch;
                logoPhaseTimer_ = 0.0f;
            }
            break;
        }
        case TitleLogoPhase::Glitch:
        {
            const float t = Clamp(logoPhaseTimer_ / kGlitchDuration, 0.0f, 1.0f);
            logoMaterial_.SetFloat("glitchIntensity", sin(t * kPi));
            logoMaterial_.SetFloat("scanProgress", 1.0f);
            logoMaterial_.SetFloat("time", logoIdleTime_);
            if (logoPhaseTimer_ >= kGlitchDuration) {
                for (uint i = 0; i < logoEchoes_.length(); i++) {
                    logoEchoes_[i].age = -float(i) * kEchoSpacing;
                }
                logoMaterial_.SetFloat("glitchIntensity", 0.0f);
                logoPhase_ = TitleLogoPhase::Echo;
                logoPhaseTimer_ = 0.0f;
            }
            break;
        }
        case TitleLogoPhase::Echo:
        {
            logoMaterial_.SetFloat("scanProgress", 1.0f);
            logoMaterial_.SetFloat("glitchIntensity", 0.0f);
            logoMaterial_.SetFloat("time", logoIdleTime_);
            bool allDone = true;
            for (uint i = 0; i < logoEchoes_.length(); i++) {
                TitleLogoEcho@ echo = logoEchoes_[i];
                echo.age += dt;
                if (echo.age < 0.0f) {
                    echo.sprite.SetColor(Vector4(1.0f, 0.05f, 0.85f, 0.0f));
                    allDone = false;
                    continue;
                }
                const float progress = Clamp(echo.age / echo.lifetime, 0.0f, 1.0f);
                if (progress < 1.0f) {
                    allDone = false;
                }
                UpdateEcho(echo, progress, kEchoSpread);
            }
            if (allDone && logoPhaseTimer_ >= kEchoDuration) {
                logoPhase_ = TitleLogoPhase::Idle;
                logoPhaseTimer_ = 0.0f;
                for (uint i = 0; i < logoEchoes_.length(); i++) {
                    logoEchoes_[i].sprite.SetColor(Vector4(1.0f, 0.05f, 0.85f, 0.0f));
                }
            }
            break;
        }
        case TitleLogoPhase::Idle:
        {
            const float kPulseSpeed = 1.5f;
            const float pulse = sin(logoIdleTime_ * kPulseSpeed) * 0.5f + 0.5f;
            logoMaterial_.SetFloat("pulseIntensity", pulse);
            logoMaterial_.SetFloat("scanProgress", 1.0f);
            logoMaterial_.SetFloat("glitchIntensity", 0.0f);
            logoMaterial_.SetFloat("time", logoIdleTime_);

            // 4 秒ごとに残像を出し直す
            const float kIdleEchoInterval = 4.0f;
            const float kIdleEchoSpacing = 0.18f;
            idleEchoTimer_ += dt;
            if (idleEchoTimer_ >= kIdleEchoInterval) {
                for (uint i = 0; i < logoEchoes_.length(); i++) {
                    logoEchoes_[i].age = -float(i) * kIdleEchoSpacing;
                }
                idleEchoTimer_ = 0.0f;
            }

            const float kIdleEchoSpread = 0.22f;
            for (uint i = 0; i < logoEchoes_.length(); i++) {
                TitleLogoEcho@ echo = logoEchoes_[i];
                echo.age += dt;
                if (echo.age < 0.0f) {
                    echo.sprite.SetColor(Vector4(1.0f, 0.05f, 0.85f, 0.0f));
                    continue;
                }
                const float progress = Clamp(echo.age / echo.lifetime, 0.0f, 1.0f);
                UpdateEcho(echo, progress, kIdleEchoSpread);
            }
            break;
        }
        }
    }

    // ロゴの大きさから外へ広げ、sin の山で現れて消える
    private void UpdateEcho(TitleLogoEcho@ echo, float progress, float spread)
    {
        const Vector2 baseScale = logoSprite_.GetScale();
        const float scale = 1.0f + progress * spread;
        echo.sprite.SetScale(baseScale.x * scale, baseScale.y * scale);
        echo.sprite.SetTranslate(logoSprite_.GetTranslate());
        const float alpha = sin(progress * kPi) * 0.55f;
        echo.sprite.SetColor(Vector4(1.0f, 0.05f, 0.85f, alpha));
    }

    private void RequestStageSelect()
    {
        if (emptyTransitionPhase_ != 0) {
            pendingStageSelect_ = true;
            return;
        }
        ChangeSceneTd3(kSceneStageSelect);
    }

    private void RequestEmptyTransition()
    {
        if (emptyTransitionPhase_ != 0) {
            pendingEmptyTransition_ = true;
            return;
        }
        StartEmptyTransition();
    }

    private void StartEmptyTransition()
    {
        emptyTransitionPhase_ = 1;
        emptyTransitionTimer_ = 0.0f;
        emptyTransitionWaitFrames_ = 0;
        ShaderMaterial@ fade = PrepareFadeFall();
        if (fade !is null) {
            fade.SetFloat("progress", 0.0f);
        }
        PostEffect::SetEnabled("Fade", true);
        SetUIVisible(false);
    }

    // 暗転中（シーンを止める間）なら true
    private bool UpdateEmptyTransition(float dt)
    {
        if (emptyTransitionPhase_ == 0) {
            return false;
        }
        ShaderMaterial@ fade = PostEffect::GetMaterial("Fade");
        const float kOutSeconds = 1.4f;
        const float kInSeconds = 1.0f;
        emptyTransitionTimer_ += dt;
        if (emptyTransitionPhase_ == 1) {
            if (emptyTransitionTimer_ >= kOutSeconds) {
                emptyTransitionTimer_ = kOutSeconds;
                ++emptyTransitionWaitFrames_;
                if (emptyTransitionWaitFrames_ >= 3) {
                    emptyTransitionPhase_ = 2;
                    emptyTransitionTimer_ = 0.0f;
                }
            }
            if (fade !is null) {
                fade.SetFloat("progress", emptyTransitionPhase_ == 1 ? emptyTransitionTimer_ / kOutSeconds : 1.0f);
            }
            return true;
        }

        const float t = Clamp(emptyTransitionTimer_ / kInSeconds, 0.0f, 1.0f);
        if (fade !is null) {
            fade.SetFloat("progress", 1.0f - t);
        }
        if (emptyTransitionTimer_ >= kInSeconds) {
            emptyTransitionPhase_ = 0;
            if (fade !is null) {
                fade.SetFloat("progress", 0.0f);
            }
            PostEffect::SetEnabled("Fade", false);
            SetUIVisible(true);
            if (pendingStageSelect_) {
                pendingStageSelect_ = false;
                pendingEmptyTransition_ = false;
                ChangeSceneTd3(kSceneStageSelect);
            } else if (pendingEmptyTransition_) {
                pendingEmptyTransition_ = false;
                StartEmptyTransition();
            }
        }
        return false;
    }

    private void SetUIVisible(bool visible)
    {
        for (uint i = 0; i < uiImages_.length(); i++) {
            uiImages_[i].SetActive(visible);
        }
    }

    private void SetCamera(float zoom, const Vector2 &in position)
    {
        if (cameraObject_ is null) {
            return;
        }
        cameraObject_.transform.position = Vector3(position.x, position.y, -10.0f);
        cameraObject_.camera.orthographicSize = kScreenHeight * 0.5f / zoom;
    }
}
