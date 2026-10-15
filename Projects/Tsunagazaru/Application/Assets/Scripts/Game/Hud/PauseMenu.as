// ポーズメニューのテクスチャ
const string kPauseCursor = "Application/Assets/Textures/UI/cursor.png";
const string kPauseLeafM = "Application/Assets/Textures/UI/Plant/leaf_m.png";
const string kPauseLeafS = "Application/Assets/Textures/UI/Plant/leaf_s.png";
const string kPauseDim = "Application/Assets/Textures/UI/dim.png";

enum PausePhase
{
    Hidden,
    Opening,
    Idle,
    Closing
}

enum PauseChoice
{
    Continue,
    Retry,
    Title
}

// 吊るした木札 1 枚
class PauseItem
{
    SignPlank@ plank;
    UIImage@ ropeLeft;
    UIImage@ ropeRight;
    array<UIImage@> creepers;
    UIText@ text;
    float select = 0.0f;
}

// 決定やカーソル移動で舞う葉 1 枚
class PauseLeaf
{
    UIImage@ image;
    Vector2 position;
    Vector2 velocity;
    float rotation = 0.0f;
    float spin = 0.0f;
    float lifeSpan = 0.0f;
    float life = 0.0f;
    float size = 1.0f;
}

// ポーズメニュー。ESC（パッドは START）で開閉し、天井から看板と木札 3 枚（つづける・さいしょから・タイトルへ）を吊り下ろす。
// 開いている間はゲームの時間を止め、BGM を絞る。画面右上には「ESC ぽーず」の札を常に出しておく。
// 時間が止まっていても動かすため、メニューの動きは実時間で数える
[DisplayName("ポーズメニュー")]
class PauseMenu : ScriptComponent
{
    [DisplayName("ポーズメニューを使う")]
    bool menuEnabled = true;

    [DisplayName("ポーズ中の BGM の倍率"), Range(0, 1)]
    float bgmDuck = 0.35f;

    [DisplayName("効果音の音量"), Range(0, 1)]
    float seVolume = 0.6f;

    [DisplayName("描画順"), Range(0, 8000)]
    int sortOrder = 2000;

    [DisplayName("看板が落ちて収まるまで（秒）"), Range(0.1, 2)]
    float openSeconds = 0.55f;

    [DisplayName("看板が跳ね上がって消えるまで（秒）"), Range(0.05, 1.5)]
    float closeSeconds = 0.28f;

    [DisplayName("暗幕の色（画面の色）")]
    Vector4 dimColor = Vector4(35.0f / 255.0f, 86.0f / 255.0f, 45.0f / 255.0f, 1.0f);

    [DisplayName("暗幕の濃さ"), Range(0, 1)]
    float dimAlpha = 0.35f;

    [DisplayName("決定で舞う葉の枚数"), Range(0, 28)]
    int confirmLeaves = 14;

    [DisplayName("フォント")]
    string font = "x8y12pxDenkiChip.ttf";

    private GameManager@ gameManager_;
    private UIImage@ dim_;
    private array<UIImage@> vines_;
    private array<UIImage@> creepers_;
    private array<UIImage@> foliage_;
    private SignPlank@ board_;
    private SignPlank@ boardLower_;
    private array<PauseItem@> items_;
    private UIImage@ cursor_;
    private array<PauseLeaf@> leaves_;
    private SignPlank@ hudPlank_;
    private UIImage@ hudLeaf_;
    private UIText@ title_;
    private UIText@ hudText_;

    private PausePhase phase_ = PausePhase::Hidden;
    private float phaseTimer_ = 0.0f;
    private float swayTimer_ = 0.0f;
    private float confirmTimer_ = 0.0f;
    private float cursorY_ = 0.0f;
    private float introReveal_ = 1.0f;
    private int selection_ = 0;
    private int pendingChoice_ = -1;
    private float pendingTimer_ = 0.0f;
    private bool hudActive_ = false;
    private bool gamepadHint_ = false;
    private bool paused_ = false;
    private float savedTimeScale_ = 1.0f;
    private float savedBgmVolume_ = 1.0f;

    void Setup(GameManager@ gameManager)
    {
        @gameManager_ = gameManager;
        Build();
    }

    // 開幕の演出から HUD として出す（1 = 定位置・行き過ぎは 2 まで）
    void SetIntroReveal(float reveal)
    {
        introReveal_ = Clamp(reveal, 0.0f, 2.0f);
    }

    bool IsOpen() const
    {
        return phase_ == PausePhase::Opening || phase_ == PausePhase::Idle;
    }

    void Update() override
    {
        if (title_ is null) {
            return;
        }
        const float deltaTime = Time::UnscaledDeltaTime();
        if (pendingChoice_ >= 0) {
            pendingTimer_ -= deltaTime;
            if (pendingTimer_ <= 0.0f) {
                Commit(pendingChoice_);
            }
        } else {
            HandleInput();
        }
        Tick(deltaTime);
    }

    // シーンを抜けるときは必ず時間を戻す（止めたまま次のシーンへ行くとそちらも動かない）
    void OnDestroy() override
    {
        Resume();
    }

    // ---------------------------------------------------------------- 入力と開閉

    private void HandleInput()
    {
        const bool gamepad = Input::IsGamepadConnected();
        if (gamepad != gamepadHint_) {
            gamepadHint_ = gamepad;
            hudText_.text = gamepad ? "START ぽーず" : "ESC ぽーず";
        }

        if (Input::IsActionTriggered(InputAction::Pause)) {
            if (IsOpen()) {
                RequestClose();
            } else {
                RequestOpen();
            }
            return;
        }
        if (!IsOpen()) {
            return;
        }
        if (Input::IsActionTriggered(InputAction::Back)) {
            RequestClose();
            return;
        }
        if (Input::IsActionTriggered(InputAction::MoveForward)) {
            MoveSelection(-1);
            PlaySe("Application/Assets/Sounds/SE/build.mp3", 1.3f);
        } else if (Input::IsActionTriggered(InputAction::MoveBack)) {
            MoveSelection(1);
            PlaySe("Application/Assets/Sounds/SE/build.mp3", 1.3f);
        }
        if (Input::IsActionTriggered(InputAction::Decide)) {
            PlayConfirm();
            PlaySe("Application/Assets/Sounds/SE/decision.mp3", 1.0f);
            pendingChoice_ = selection_;
            pendingTimer_ = 0.34f;
        }
    }

    private void RequestOpen()
    {
        // 終わりの演出やシーンの切り替えと重ねない
        if (!menuEnabled || phase_ != PausePhase::Hidden) {
            return;
        }
        if (gameManager_ !is null && !gameManager_.IsPlaying()) {
            return;
        }
        Open();
        Pause();
        PlaySe("Application/Assets/Sounds/SE/title_bound.mp3", 1.0f);
    }

    private void RequestClose()
    {
        Close();
        Resume();
        PlaySe("Application/Assets/Sounds/SE/build_return.mp3", 1.0f);
    }

    // 決めた項目を実行する（札がへこんで跳ね返るのを見せてから呼ぶ）
    private void Commit(int choice)
    {
        pendingChoice_ = -1;
        Close();
        Resume();
        if (choice == PauseChoice::Continue) {
            return;
        }
        Scene::ChangeScene(choice == PauseChoice::Retry ? "GameScene" : "TitleScene");
    }

    private void Pause()
    {
        if (paused_) {
            return;
        }
        paused_ = true;
        GamePause::paused = true;
        savedTimeScale_ = Time::TimeScale();
        Time::SetTimeScale(0.0f);
        savedBgmVolume_ = Audio::GetBusVolume(AudioBus::BGM);
        Audio::SetBusVolume(AudioBus::BGM, savedBgmVolume_ * Clamp(bgmDuck, 0.0f, 1.0f));
    }

    private void Resume()
    {
        if (!paused_) {
            return;
        }
        paused_ = false;
        GamePause::paused = false;
        Time::SetTimeScale(savedTimeScale_);
        Audio::SetBusVolume(AudioBus::BGM, savedBgmVolume_);
    }

    private void PlaySe(const string &in path, float pitch)
    {
        PlayParams params;
        params.bus = AudioBus::SE;
        params.volume = seVolume;
        params.pitch = pitch;
        Audio::PlayOneShot(path, params);
    }

    // ---------------------------------------------------------------- 組み立て

    private void Build()
    {
        @dim_ = SpawnPart(kPauseDim, "PauseDim", sortOrder);

        // 天井から下ろす蔦（7 本 × 4 節）
        for (int i = 0; i < 7 * 4; ++i) {
            UIImage@ vine = SpawnPart(kSignVineV, "PauseVine" + i, sortOrder + 1);
            vine.size = Vector2(32.0f, 64.0f);
            vines_.insertLast(vine);
        }
        // 看板の上下の縁を這う蔦
        for (int i = 0; i < 16; ++i) {
            UIImage@ creeper = SpawnPart(kSignVineH, "PauseCreeper" + i, sortOrder + 6);
            creeper.size = Vector2(128.0f, 48.0f);
            creepers_.insertLast(creeper);
        }
        // 茂み（0-3 は看板の四隅・4-7 は画面の四隅）
        for (int i = 0; i < 8; ++i) {
            UIImage@ leaf = SpawnPart(kSignFoliage, "PauseFoliage" + i, sortOrder + (i < 4 ? 7 : 1));
            leaf.size = Vector2(208.0f, 128.0f);
            foliage_.insertLast(leaf);
        }

        @board_ = SpawnPlank("PauseBoardUpper", sortOrder + 2);
        @boardLower_ = SpawnPlank("PauseBoardLower", sortOrder + 2);

        const array<string> itemTexts = { "つづける", "さいしょから", "タイトルへ" };
        for (uint i = 0; i < itemTexts.length(); ++i) {
            PauseItem item;
            const string name = "PauseItem" + i;
            @item.ropeLeft = SpawnPart(kSignVineV, name + "RopeL", sortOrder + 1);
            @item.ropeRight = SpawnPart(kSignVineV, name + "RopeR", sortOrder + 1);
            @item.plank = SpawnPlank(name, sortOrder + 3);
            for (int k = 0; k < 2; ++k) {
                UIImage@ creeper = SpawnPart(kSignVineH, name + "Creeper" + k, sortOrder + 6);
                creeper.size = Vector2(128.0f, 48.0f);
                item.creepers.insertLast(creeper);
            }
            @item.text = SpawnLabel(itemTexts[i], "PauseItemText" + i, 48.0f, sortOrder + 5);
            items_.insertLast(item);
        }

        @cursor_ = SpawnPart(kPauseCursor, "PauseCursor", sortOrder + 7);
        cursor_.size = Vector2(128.0f, 64.0f);

        for (int i = 0; i < 28; ++i) {
            PauseLeaf leaf;
            @leaf.image = SpawnPart(i % 2 == 0 ? kPauseLeafM : kPauseLeafS, "PauseLeaf" + i, sortOrder + 8);
            leaves_.insertLast(leaf);
        }

        // 画面右上に常に出す操作の札
        @hudPlank_ = SpawnPlank("PauseHudPlank", sortOrder - 2);
        @hudLeaf_ = SpawnPart(kPauseLeafM, "PauseHudLeaf", sortOrder - 1);
        hudLeaf_.size = Vector2(48.0f, 28.0f);

        @title_ = SpawnLabel("ぽーずちゅう", "PauseTitle", 96.0f, sortOrder + 5);
        @hudText_ = SpawnLabel("ESC ぽーず", "PauseHudText", 48.0f, sortOrder - 1);

        SetHudActive(true);
    }

    private UIImage@ SpawnPart(const string &in texture, const string &in name, int order)
    {
        UIImage@ image = owner.SpawnUIImage(texture, name);
        image.anchor = UIAnchor::TopCenter;
        image.pivot = Vector2(0.5f, 0.5f);
        image.sortOrder = order;
        image.gameObject.active = false;
        return image;
    }

    private SignPlank@ SpawnPlank(const string &in name, int order)
    {
        SignPlank plank;
        @plank.mid = SpawnPart(kSignPlankMid, name + "Mid", order);
        @plank.capLeft = SpawnPart(kSignPlankCapL, name + "CapL", order + 1);
        @plank.capRight = SpawnPart(kSignPlankCapR, name + "CapR", order + 1);
        return plank;
    }

    private UIText@ SpawnLabel(const string &in text, const string &in name, float fontSize, int order)
    {
        UIText@ label = owner.SpawnUIText(font, text, name);
        label.anchor = UIAnchor::TopCenter;
        label.pivot = Vector2(0.5f, 0.5f);
        label.fontSize = fontSize;
        label.SetAlign(TextAlignH::Center, TextAlignV::Middle);
        label.SetOutline(Vector4(0.0f, 0.0f, 0.0f, 1.0f), 0.05f);
        label.sortOrder = order;
        label.gameObject.active = false;
        return label;
    }

    // ---------------------------------------------------------------- 開閉と選択

    private void Open()
    {
        phase_ = PausePhase::Opening;
        phaseTimer_ = 0.0f;
        selection_ = 0;
        confirmTimer_ = 0.0f;
        for (uint i = 0; i < items_.length(); ++i) {
            items_[i].select = 0.0f;
        }
        items_[selection_].select = 1.0f;
        cursorY_ = 430.0f + 96.0f * 0.5f;
        SetMenuActive(true);
        SetHudActive(false);
        ApplyLayout(0.0f);
    }

    private void Close()
    {
        if (!IsOpen()) {
            return;
        }
        phase_ = PausePhase::Closing;
        phaseTimer_ = 0.0f;
    }

    private void MoveSelection(int delta)
    {
        const int count = int(items_.length());
        selection_ = ((selection_ + delta) % count + count) % count;
        // カーソルが移った先で葉を少しだけ散らす
        const float top = 430.0f + 130.0f * float(selection_);
        BurstLeaves(Vector2(kPauseCursorOffsetX, top + 48.0f), 3, 260.0f);
    }

    private void PlayConfirm()
    {
        confirmTimer_ = 0.34f;
        const float top = 430.0f + 130.0f * float(selection_);
        BurstLeaves(Vector2(0.0f, top + 48.0f), Max(0, confirmLeaves), 700.0f);
    }

    // ---------------------------------------------------------------- 毎フレーム

    private void Tick(float deltaTime)
    {
        swayTimer_ += deltaTime;
        // 葉はメニューが消えたあとも飛び続ける
        UpdateLeaves(deltaTime);

        if (phase_ == PausePhase::Hidden) {
            if (!hudActive_) {
                SetHudActive(true);
            }
            PlaceHud();
            return;
        }

        phaseTimer_ += deltaTime;
        confirmTimer_ = Max(0.0f, confirmTimer_ - deltaTime);
        if (phase_ == PausePhase::Opening) {
            // いちばん遅い木札が着地しきってから操作を受ける
            if (phaseTimer_ >= openSeconds + 0.06f * float(items_.length())) {
                phase_ = PausePhase::Idle;
            }
        } else if (phase_ == PausePhase::Closing && phaseTimer_ >= closeSeconds) {
            phase_ = PausePhase::Hidden;
            SetMenuActive(false);
            return;
        }

        // 選択の度合いを目標へ寄せる（切り替えた瞬間に飛ばず、傾きが滑る）
        for (uint i = 0; i < items_.length(); ++i) {
            const float target = (int(i) == selection_ && phase_ != PausePhase::Closing) ? 1.0f : 0.0f;
            items_[i].select = Follow(items_[i].select, target, 16.0f, deltaTime);
        }
        ApplyLayout(deltaTime);
    }

    // 画面右上の操作の札。開幕の登場では板の左端が画面外へ抜けるまで右へ寄せてから戻す
    private void PlaceHud()
    {
        const float sway = sin(swayTimer_ * 1.5f) * 0.05f * 0.5f;
        const float width = Max(200.0f, hudText_.measuredSize.x + 56.0f * 2.0f);
        const float centerX = 960.0f - width * 0.5f - 48.0f;
        const float x = centerX + (1.0f - introReveal_) * (960.0f - centerX + width * 0.5f + 48.0f);
        const float plain = ToneMappedChannel(1.0f);
        const Vector4 plainColor = Vector4(plain, plain, plain, 1.0f);
        PlacePlank(hudPlank_, Vector2(x, 40.0f + 48.0f), width, 1.0f, 1.0f, sway, plainColor);
        hudText_.anchoredPosition = Vector2(x, 40.0f + 48.0f);
        hudText_.rotation = sway;
        hudText_.color = ToneMappedColor(kPauseItemColor);
        hudLeaf_.anchoredPosition = Vector2(x - width * 0.5f + 6.0f, 40.0f + 4.0f);
        hudLeaf_.rotation = sway - 0.5f;
        hudLeaf_.color = plainColor;
    }

    private float DropProgress(float delaySeconds) const
    {
        if (phase_ == PausePhase::Opening) {
            const float t = Clamp((phaseTimer_ - delaySeconds) / Max(0.01f, openSeconds), 0.0f, 1.0f);
            return Ease(EaseType::EaseOutBack, t);
        }
        if (phase_ == PausePhase::Idle) {
            return 1.0f;
        }
        if (phase_ == PausePhase::Closing) {
            const float t = Clamp(phaseTimer_ / Max(0.01f, closeSeconds), 0.0f, 1.0f);
            return 1.0f - Ease(EaseType::EaseInBack, t);
        }
        return 0.0f;
    }

    private void PlaceRope(UIImage@ rope, float centerX, float topY, float length, const Vector4 &in color)
    {
        const float clamped = Max(0.0f, length);
        rope.rotation = 0.0f;
        rope.size = Vector2(32.0f, clamped);
        rope.anchoredPosition = Vector2(centerX, topY + clamped * 0.5f);
        rope.color = color;
    }

    private void PlacePlank(SignPlank@ plank, const Vector2 &in center, float width, float scale, float scaleY, float angle,
        const Vector4 &in color)
    {
        const float w = width * scale;
        const float h = 96.0f * scale * scaleY;
        const float capW = 40.0f * scale;
        const float capOffset = (w - capW) * 0.5f;
        PlacePlankPart(plank.mid, center, 0.0f, Max(0.0f, w - capW * 2.0f), h, angle, color);
        PlacePlankPart(plank.capLeft, center, -capOffset, capW, h, angle, color);
        PlacePlankPart(plank.capRight, center, capOffset, capW, h, angle, color);
    }

    private void PlacePlankPart(UIImage@ image, const Vector2 &in center, float offsetX, float sizeX, float sizeY, float angle,
        const Vector4 &in color)
    {
        const Vector2 offset = RotateOffset(Vector2(offsetX, 0.0f), angle);
        image.anchoredPosition = Vector2(center.x + offset.x, center.y + offset.y);
        image.size = Vector2(sizeX, sizeY);
        image.rotation = angle;
        image.color = color;
    }

    private void ApplyLayout(float deltaTime)
    {
        const float plainValue = ToneMappedChannel(1.0f);
        const Vector4 plain = Vector4(plainValue, plainValue, plainValue, 1.0f);

        // 暗幕
        float dimT = 1.0f;
        if (phase_ == PausePhase::Opening) {
            dimT = Clamp(phaseTimer_ / 0.18f, 0.0f, 1.0f);
        } else if (phase_ == PausePhase::Closing) {
            dimT = 1.0f - Clamp(phaseTimer_ / Max(0.01f, closeSeconds), 0.0f, 1.0f);
        }
        dim_.anchoredPosition = Vector2(0.0f, 540.0f);
        dim_.size = Vector2(1920.0f, 1080.0f);
        dim_.color = Vector4(dimColor.x, dimColor.y, dimColor.z, dimAlpha * dimT);

        // 看板（板 2 枚）
        const float boardTop = 176.0f - (1.0f - DropProgress(0.0f)) * 1300.0f;
        const float boardBottom = boardTop + 96.0f * 2.0f;
        PlacePlank(board_, Vector2(0.0f, boardTop + 48.0f), 900.0f, 1.0f, 1.0f, 0.0f, plain);
        PlacePlank(boardLower_, Vector2(0.0f, boardTop + 144.0f), 900.0f, 1.0f, 1.0f, 0.0f, plain);
        title_.anchoredPosition = Vector2(0.0f, boardTop + 96.0f);
        title_.color = ToneMappedColor(kPauseTitleColor);

        // 天井から下ろす蔦。本ごとに揺れの位相をずらし、下の節ほど大きく振る
        const array<float> vineX = { -420.0f, -300.0f, -160.0f, 0.0f, 160.0f, 300.0f, 420.0f };
        for (uint v = 0; v < 7; ++v) {
            const float sway = sin(swayTimer_ * 1.5f + float(v) * 1.7f) * 0.05f;
            for (uint s = 0; s < 4; ++s) {
                UIImage@ image = vines_[v * 4 + s];
                const float amount = sway * (1.0f - float(s) / 4.0f);
                image.anchoredPosition = Vector2(vineX[v] + amount * 40.0f, boardTop - 64.0f * (float(s) + 0.5f));
                image.rotation = amount;
                image.size = Vector2(32.0f, 64.0f);
                image.color = plain;
            }
        }

        // 看板の上下の縁を這う蔦（下の縁は上下を返す）
        const uint perRow = creepers_.length() / 2;
        const float creeperLeft = -128.0f * float(perRow) * 0.5f + 64.0f;
        for (uint i = 0; i < creepers_.length(); ++i) {
            const bool lowerRow = i >= perRow;
            creepers_[i].anchoredPosition = Vector2(creeperLeft + 128.0f * float(i % perRow), lowerRow ? boardBottom + 2.0f : boardTop - 6.0f);
            creepers_[i].rotation = lowerRow ? kPi : 0.0f;
            creepers_[i].color = plain;
        }

        // 茂み。看板の四隅は看板と一緒に落ち、画面の四隅は暗幕と一緒に現れる
        const float shoulderX = 900.0f * 0.5f - 40.0f;
        const array<float> boardX = { -shoulderX, shoulderX, -shoulderX, shoulderX };
        const array<float> boardY = { boardTop + 8.0f, boardTop + 8.0f, boardBottom - 12.0f, boardBottom - 12.0f };
        const array<float> boardRotation = { -0.25f, 0.25f, 2.9f, -2.9f };
        for (uint i = 0; i < 4; ++i) {
            const float sway = sin(swayTimer_ * 1.5f + float(i)) * 0.05f;
            foliage_[i].anchoredPosition = Vector2(boardX[i], boardY[i]);
            foliage_[i].rotation = boardRotation[i] + sway;
            foliage_[i].size = Vector2(208.0f, 128.0f);
            foliage_[i].color = plain;
        }
        const array<float> cornerX = { -960.0f + 70.0f, 960.0f - 70.0f, -960.0f + 70.0f, 960.0f - 70.0f };
        const array<float> cornerY = { 70.0f, 70.0f, 1010.0f, 1010.0f };
        const array<float> cornerRotation = { -0.6f, 0.6f, 2.5f, -2.5f };
        for (uint i = 0; i < 4; ++i) {
            const float sway = sin(swayTimer_ * 1.5f * 0.7f + float(i) * 2.1f) * 0.05f;
            UIImage@ image = foliage_[4 + i];
            image.anchoredPosition = Vector2(cornerX[i], cornerY[i]);
            image.rotation = cornerRotation[i] + sway;
            image.size = Vector2(208.0f * dimT, 128.0f * dimT);
            image.color = plain;
        }

        // 木札。決定した瞬間だけ縦に潰して跳ね返す
        float confirmScaleY = 1.0f;
        float confirmScaleX = 1.0f;
        if (confirmTimer_ > 0.0f) {
            const float bump = sin((1.0f - confirmTimer_ / 0.34f) * kPi);
            confirmScaleY = 1.0f - 0.20f * bump;
            confirmScaleX = 1.0f + 0.20f * 0.5f * bump;
        }

        float previousBottom = boardBottom;
        float selectedCenterY = cursorY_;
        for (uint i = 0; i < items_.length(); ++i) {
            PauseItem@ item = items_[i];
            const float drop = DropProgress(0.06f * float(i + 1));
            const float top = 430.0f + 130.0f * float(i) - (1.0f - drop) * 1300.0f;
            const float centerY = top + 48.0f;
            const bool isSelected = int(i) == selection_;
            const float scale = 1.0f + 0.05f * item.select;
            const float angle = -0.030f * item.select;
            const float lit = ToneMappedChannel(1.0f + 0.20f * item.select);
            PlacePlank(item.plank, Vector2(0.0f, centerY), 660.0f * (isSelected ? confirmScaleX : 1.0f), scale,
                isSelected ? confirmScaleY : 1.0f, angle, Vector4(lit, lit, lit, 1.0f));

            // 蔦は 1 つ上の板の下端と、この札の上端を繋ぐ
            PlaceRope(item.ropeLeft, -200.0f, previousBottom, top - previousBottom, plain);
            PlaceRope(item.ropeRight, 200.0f, previousBottom, top - previousBottom, plain);
            previousBottom = top + 96.0f;

            // 札の上を這う蔦。文字に被らないよう左右へ寄せる
            for (uint k = 0; k < item.creepers.length(); ++k) {
                const float side = k == 0 ? -1.0f : 1.0f;
                const Vector2 offset = RotateOffset(Vector2(side * (660.0f * 0.5f - 64.0f) * scale, -48.0f * scale), angle);
                item.creepers[k].anchoredPosition = Vector2(offset.x, centerY + offset.y);
                item.creepers[k].rotation = angle;
                item.creepers[k].color = plain;
            }

            item.text.anchoredPosition = Vector2(0.0f, centerY);
            item.text.rotation = angle;
            item.text.fontSize = 48.0f * scale;
            item.text.color = ToneMappedColor(Lerp(kPauseItemColor, kPauseSelectedColor, item.select));

            if (isSelected) {
                selectedCenterY = centerY;
            }
        }

        // 葉のカーソル。札から札へ滑って移る
        cursorY_ = Follow(cursorY_, selectedCenterY, 24.0f, deltaTime);
        cursor_.anchoredPosition = Vector2(kPauseCursorOffsetX, cursorY_);
        cursor_.rotation = sin(swayTimer_ * 3.0f) * 0.08f;
        cursor_.size = Vector2(128.0f, 64.0f);
        cursor_.color = plain;
    }

    // ---------------------------------------------------------------- 舞う葉

    private void BurstLeaves(const Vector2 &in origin, int count, float power)
    {
        int spawned = 0;
        for (uint i = 0; i < leaves_.length() && spawned < count; ++i) {
            PauseLeaf@ leaf = leaves_[i];
            if (leaf.life > 0.0f) {
                continue;
            }
            // y は下向きなので、上へ飛ばすには sin が負になる角度を選ぶ
            const float angle = Random::Range(-2.6f, -0.5f);
            const float speed = power * Random::Range(0.45f, 1.0f);
            leaf.position = Vector2(origin.x + Random::Range(-90.0f, 90.0f), origin.y + Random::Range(-24.0f, 24.0f));
            leaf.velocity = Vector2(cos(angle) * speed, sin(angle) * speed);
            leaf.rotation = Random::Range(-3.14f, 3.14f);
            leaf.spin = Random::Range(-7.0f, 7.0f);
            leaf.lifeSpan = Random::Range(0.7f, 1.15f);
            leaf.life = leaf.lifeSpan;
            leaf.size = Random::Range(0.8f, 1.35f);
            leaf.image.gameObject.active = true;
            ++spawned;
        }
    }

    private void UpdateLeaves(float deltaTime)
    {
        const float plain = ToneMappedChannel(1.0f);
        for (uint i = 0; i < leaves_.length(); ++i) {
            PauseLeaf@ leaf = leaves_[i];
            if (leaf.life <= 0.0f) {
                continue;
            }
            leaf.life -= deltaTime;
            if (leaf.life <= 0.0f) {
                leaf.life = 0.0f;
                leaf.image.gameObject.active = false;
                continue;
            }
            leaf.velocity.y += 900.0f * deltaTime;
            leaf.velocity.x *= 0.99f;
            leaf.position.x += leaf.velocity.x * deltaTime;
            leaf.position.y += leaf.velocity.y * deltaTime;
            leaf.rotation += leaf.spin * deltaTime;
            leaf.image.anchoredPosition = leaf.position;
            leaf.image.rotation = leaf.rotation;
            leaf.image.size = Vector2(48.0f * leaf.size, 28.0f * leaf.size);
            leaf.image.color = Vector4(plain, plain, plain, Clamp(leaf.life / 0.35f, 0.0f, 1.0f));
        }
    }

    // ---------------------------------------------------------------- 表示の切り替え

    private void SetMenuActive(bool active)
    {
        dim_.gameObject.active = active;
        SetPlankActive(board_, active);
        SetPlankActive(boardLower_, active);
        cursor_.gameObject.active = active;
        title_.gameObject.active = active;
        for (uint i = 0; i < vines_.length(); ++i) {
            vines_[i].gameObject.active = active;
        }
        for (uint i = 0; i < creepers_.length(); ++i) {
            creepers_[i].gameObject.active = active;
        }
        for (uint i = 0; i < foliage_.length(); ++i) {
            foliage_[i].gameObject.active = active;
        }
        for (uint i = 0; i < items_.length(); ++i) {
            SetPlankActive(items_[i].plank, active);
            items_[i].ropeLeft.gameObject.active = active;
            items_[i].ropeRight.gameObject.active = active;
            for (uint k = 0; k < items_[i].creepers.length(); ++k) {
                items_[i].creepers[k].gameObject.active = active;
            }
            items_[i].text.gameObject.active = active;
        }
    }

    private void SetHudActive(bool active)
    {
        hudActive_ = active;
        SetPlankActive(hudPlank_, active);
        hudLeaf_.gameObject.active = active;
        hudText_.gameObject.active = active;
    }

    private void SetPlankActive(SignPlank@ plank, bool active)
    {
        plank.mid.gameObject.active = active;
        plank.capLeft.gameObject.active = active;
        plank.capRight.gameObject.active = active;
    }
}

// 元のゲームで露出を打ち消して描いていた文字の色（ToneMappedColor で画面の色にする）
const Vector4 kPauseTitleColor = Vector4(0.944f, 0.413f, 0.053f, 1.0f);
const Vector4 kPauseItemColor = Vector4(0.735f, 0.546f, 0.296f, 1.0f);
const Vector4 kPauseSelectedColor = Vector4(1.000f, 0.546f, 0.087f, 1.0f);
// 葉のカーソルの x（木札の左端からさらに左）
const float kPauseCursorOffsetX = -(660.0f * 0.5f) - 82.0f;

// 指数で目標へ寄せる（フレームレートに依らない追従）
float Follow(float current, float target, float rate, float deltaTime)
{
    return current + (target - current) * (1.0f - exp(-rate * deltaTime));
}
