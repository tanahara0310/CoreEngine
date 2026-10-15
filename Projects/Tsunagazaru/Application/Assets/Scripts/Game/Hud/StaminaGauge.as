// ゲージのテクスチャ（2 倍で描いてある。寸法はテクスチャの実サイズと揃える）
const string kStaminaPip = "Application/Assets/Textures/UI/Gauge/pip.png";
const string kStaminaPipEmpty = "Application/Assets/Textures/UI/Gauge/pip_empty.png";
const string kStaminaCapL = "Application/Assets/Textures/UI/Board/board_cap_l.png";
const string kStaminaCapR = "Application/Assets/Textures/UI/Board/board_cap_r.png";
const string kStaminaLeaf = "Application/Assets/Textures/UI/Plant/leaf.png";
const string kStaminaVine = "Application/Assets/Textures/UI/Plant/vine.png";

// バナナの粒 1 つ
class StaminaPip
{
    UIImage@ image;
    float grow = 0.0f;      // 0 = 皮 ／ 1 = 実りきった
    float delay = 0.0f;     // 実り始めるまでの待ち
    float flash = 0.0f;
    bool flashGain = true;  // 光るのは増えたとき（緑）か減ったとき（赤）か
    bool filled = false;
    bool showsFruit = true;
    bool preview = false;
}

// スタミナをバナナの粒で見せるゲージ。板（端木 2 枚と横へ伸ばした中板）の上に、スタミナ 2 ごとに 1 粒並べる。
// 実る・食べられるときは粒が伸び縮みして光り、次の 1 マスが高いときは食べられる粒を点滅で予告する
[DisplayName("スタミナのゲージ")]
class StaminaGauge : ScriptComponent
{
    [DisplayName("位置（左上から・px）")]
    Vector2 position = Vector2(32.0f, 44.0f);

    [DisplayName("表示の倍率"), Range(0.25, 2)]
    float scale = 1.0f;

    [DisplayName("1 粒のスタミナ"), Range(0.5, 20)]
    float staminaPerPip = 2.0f;

    [DisplayName("描画順"), Range(0, 5000)]
    int sortOrder = 900;

    [DisplayName("揺れの速さ"), Range(0, 10)]
    float swaySpeed = 1.8f;

    [DisplayName("揺れの角度"), Range(0, 0.5)]
    float swayAmplitude = 0.05f;

    [DisplayName("上下の揺れ（px）"), Range(0, 8)]
    float swayBob = 2.4f;

    [DisplayName("粒の明るさ"), Range(0.05, 2)]
    float pipBrightness = 1.0f;

    [DisplayName("蔦と葉の明るさ"), Range(0.05, 2)]
    float foliageBrightness = 1.0f;

    [DisplayName("高いマスを点滅で予告する")]
    bool previewEnabled = true;

    [DisplayName("バナナが入ったときに弾む時間"), Range(0.05, 2)]
    float gainPopDuration = 0.38f;

    [DisplayName("バナナが入ったときに持ち上がる量（px）"), Range(0, 40)]
    float gainPopLift = 6.0f;

    [DisplayName("バナナが入ったときに粒が縦へ伸びる量"), Range(0, 1)]
    float gainPopStretch = 0.18f;

    [DisplayName("切り替わった瞬間の光の強さ"), Range(0, 4)]
    float flashStrength = 1.4f;

    private Hunger@ hunger_;
    private RailBuilder@ builder_;
    private UIImage@ board_;
    private UIImage@ capLeft_;
    private UIImage@ capRight_;
    private array<StaminaPip@> pips_;
    private array<UIImage@> leaves_;
    private array<UIImage@> vines_;
    private int visiblePipCount_ = 0;
    private int fillFrontIndex_ = 0;
    private int previewPipCount_ = 0;
    private float lowPulse_ = 0.0f;
    private float introReveal_ = 1.0f;
    private float gainPopElapsed_ = 0.0f;
    private bool gainPopActive_ = false;

    // GameDirector が最初に呼ぶ
    void Setup(Hunger@ hunger, RailBuilder@ builder)
    {
        @hunger_ = hunger;
        @builder_ = builder;
        BuildParts();
    }

    // 突入の演出あけの登場（0 = 画面の左外 ／ 1 = 定位置。行き過ぎの 2 まで許す）
    void SetIntroReveal(float reveal)
    {
        introReveal_ = Clamp(reveal, 0.0f, 2.0f);
    }

    // 実っている一番右の粒の位置と大きさ（飛んでくるバナナの着地点）
    bool TryGetFillFrontTarget(Vector2 &out targetPosition, Vector2 &out targetSize)
    {
        targetPosition = Vector2(0, 0);
        targetSize = Vector2(0, 0);
        if (board_ is null || visiblePipCount_ == 0) {
            return false;
        }
        UIImage@ image = pips_[Min(fillFrontIndex_, int(pips_.length()) - 1)].image;
        targetSize = image.size;
        const Vector2 anchored = image.anchoredPosition;
        targetPosition = Vector2(anchored.x, anchored.y + targetSize.y * 0.5f);
        return true;
    }

    // 入ってきた量ぶんの粒を、生え際から左へさかのぼって伸び直させる
    void PlayGainPop(float staminaAmount)
    {
        if (board_ is null) {
            return;
        }
        gainPopElapsed_ = 0.0f;
        gainPopActive_ = true;
        const int count = Clamp(int(floor(staminaAmount / Max(0.5f, staminaPerPip) + 0.5f)), 1, 10);
        for (int offset = 0; offset < count; ++offset) {
            if (fillFrontIndex_ < offset) {
                break;
            }
            StaminaPip@ pip = pips_[fillFrontIndex_ - offset];
            if (!pip.filled) {
                continue;
            }
            pip.grow = Min(pip.grow, 0.45f);
            pip.delay = float(offset) * 0.02f * 2.0f;
            pip.flash = 1.0f;
            pip.flashGain = true;
        }
    }

    void Update() override
    {
        if (board_ is null || hunger_ is null || GamePause::paused) {
            return;
        }
        const float deltaTime = Time::UnscaledDeltaTime();
        const float time = Time::UnscaledTimeSinceStartup();
        if (gainPopActive_) {
            gainPopElapsed_ += deltaTime;
            if (gainPopElapsed_ >= Max(0.05f, gainPopDuration)) {
                gainPopActive_ = false;
            }
        }
        UpdateTargets(deltaTime);
        UpdateAnimation(deltaTime, time);
        ApplyLayout(time);
    }

    // ---------------------------------------------------------------- 作る

    private void BuildParts()
    {
        // 持ち主の UI 画像が中板（横に一様なテクスチャなので伸ばしても崩れない）
        @board_ = owner.uiImage;
        if (!board_.exists) {
            Error("StaminaGauge: UI 画像に付けてください");
            @board_ = null;
            return;
        }
        board_.anchor = UIAnchor::TopLeft;
        board_.pivot = Vector2(0.0f, 0.0f);
        board_.sortOrder = sortOrder;

        @capLeft_ = SpawnHudImage(owner, kStaminaCapL, "StaminaGaugeCapL", sortOrder + 1, Vector2(0, 0), Vector2(28, 76));
        @capRight_ = SpawnHudImage(owner, kStaminaCapR, "StaminaGaugeCapR", sortOrder + 1, Vector2(0, 0), Vector2(28, 76));

        // 粒は上限ぶん作っておき、使う数だけ見せる。軸を上端にすると、縮めたときに下から食べられたように見える
        pips_.resize(80);
        for (uint i = 0; i < pips_.length(); ++i) {
            @pips_[i] = StaminaPip();
            @pips_[i].image = SpawnHudImage(owner, kStaminaPip, "StaminaGaugePip_" + i, sortOrder + 2, Vector2(0.5f, 0.0f), Vector2(10, 30));
            pips_[i].image.gameObject.active = false;
        }
        for (int i = 0; i < 2; ++i) {
            leaves_.insertLast(SpawnHudImage(owner, kStaminaLeaf, "StaminaGaugeLeaf_" + i, sortOrder + 3, Vector2(0.0f, 0.5f), Vector2(44, 26)));
        }
        for (int i = 0; i < 6; ++i) {
            vines_.insertLast(SpawnHudImage(owner, kStaminaVine, "StaminaGaugeVine_" + i, sortOrder + 4, Vector2(0.5f, 0.5f), Vector2(104, 38)));
        }
    }

    // ---------------------------------------------------------------- 毎フレーム

    private void UpdateTargets(float deltaTime)
    {
        const float perPip = Max(0.5f, staminaPerPip);
        visiblePipCount_ = Clamp(int(ceil(hunger_.GetMaximum() / perPip)), 1, int(pips_.length()));
        // 切り捨て。「あと何回敷けるか」と粒の数を揃える
        const int filledCount = int(Max(0.0f, floor(hunger_.GetCurrent() / perPip)));
        fillFrontIndex_ = filledCount > 0 ? Min(filledCount, visiblePipCount_) - 1 : 0;

        float stagger = 0.0f;
        for (uint i = 0; i < pips_.length(); ++i) {
            StaminaPip@ pip = pips_[i];
            const bool filled = int(i) < filledCount && int(i) < visiblePipCount_;
            if (filled != pip.filled) {
                pip.filled = filled;
                // 左から順に少しずつ実らせる
                pip.delay = filled ? stagger : 0.0f;
                if (filled) {
                    stagger += 0.02f;
                }
            }
        }

        // 次の 1 マスが普通のレールより高い（橋・岩）ときだけ予告する
        previewPipCount_ = 0;
        if (builder_ !is null && previewEnabled) {
            const float nextCost = builder_.GetNextPlacementCost();
            const float plainRailCost = hunger_.CalculateActionCost(hunger_.railCost);
            if (nextCost > plainRailCost + 0.001f) {
                previewPipCount_ = Min(int(ceil(nextCost / perPip)), filledCount);
            }
        }
        for (uint i = 0; i < pips_.length(); ++i) {
            pips_[i].preview = int(i) < filledCount && filledCount - int(i) <= previewPipCount_;
        }

        // 次の 1 マスすら払えないときは板ごと警告色で脈打たせる
        const bool low = hunger_.GetCurrent() < hunger_.CalculateActionCost(hunger_.railCost);
        lowPulse_ = MoveTowards(lowPulse_, low ? 1.0f : 0.0f, deltaTime * 4.0f);
    }

    private void UpdateAnimation(float deltaTime, float time)
    {
        const float previewPhase = time * 11.0f;
        for (uint i = 0; i < pips_.length(); ++i) {
            StaminaPip@ pip = pips_[i];
            if (pip.delay > 0.0f) {
                pip.delay = Max(0.0f, pip.delay - deltaTime);
            } else {
                pip.grow = MoveTowards(pip.grow, pip.filled ? 1.0f : 0.0f, (pip.filled ? 11.0f : 9.0f) * deltaTime);
            }

            // 実が消えきったら皮へ、生え始めたら実へ絵を差し替える
            const bool showsFruit = pip.grow > 0.0f;
            if (showsFruit != pip.showsFruit) {
                pip.showsFruit = showsFruit;
                pip.image.texture = showsFruit ? kStaminaPip : kStaminaPipEmpty;
                pip.flash = 1.0f;
                pip.flashGain = showsFruit;
            }
            pip.flash = Max(0.0f, pip.flash - deltaTime * 5.0f);

            // 増えた粒は緑、食べられた粒は赤へ一瞬振る。予告は色でなく明滅で示す
            const float glow = pipBrightness * (1.0f + pip.flash * flashStrength);
            const float tintR = pip.flashGain ? (1.0f - pip.flash * 0.45f) : 1.0f;
            const float tintG = pip.flashGain ? 1.0f : (1.0f - pip.flash * 0.55f);
            const float tintB = 1.0f - pip.flash * (pip.flashGain ? 0.55f : 0.65f);
            const float alpha = pip.preview ? 0.35f + 0.65f * (0.5f + 0.5f * sin(previewPhase)) : 1.0f;
            pip.image.color = Vector4(glow * tintR, glow * tintG * (1.0f - lowPulse_ * 0.35f),
                glow * tintB * (1.0f - lowPulse_ * 0.45f), alpha);
        }
    }

    // 減衰する正弦波 1 周期（前半で持ち上がり、後半で沈んで収まる）
    private float GainPopWave() const
    {
        if (!gainPopActive_) {
            return 0.0f;
        }
        const float progress = Clamp(gainPopElapsed_ / Max(0.05f, gainPopDuration), 0.0f, 1.0f);
        return sin(progress * kTau) * (1.0f - progress);
    }

    private void ApplyLayout(float time)
    {
        const float s = Max(0.1f, scale);
        const float gainPop = GainPopWave();
        const float gainStretch = 1.0f + gainPop * gainPopStretch;
        const float pipW = 10.0f * s;
        const float pipH = 30.0f * s;
        const float pitch = 12.0f * s;
        const float bunchGap = 8.0f * s;
        const float capW = 28.0f * s;
        const float panelH = 76.0f * s;
        const float padding = 14.0f * s;
        const float bob = swayBob * s;

        // 粒の送り幅を先に足して、板の幅を粒の数から決める
        float cursor = 0.0f;
        for (int i = 0; i < visiblePipCount_; ++i) {
            cursor += pitch;
            if ((i + 1) % 5 == 0 && i + 1 != visiblePipCount_) {
                cursor += bunchGap;
            }
        }
        const float contentWidth = Max(pipW, cursor - (pitch - pipW));
        const float panelWidth = capW * 2.0f + padding * 2.0f + contentWidth;

        // 登場は板の右端が画面外へ抜ける距離まで左へ寄せてから戻す
        const Vector2 origin(position.x - (1.0f - introReveal_) * (position.x + panelWidth + 48.0f),
            position.y - gainPop * gainPopLift * s);

        board_.anchoredPosition = Vector2(origin.x + capW, origin.y);
        board_.size = Vector2(panelWidth - capW * 2.0f, panelH);
        const float warn = lowPulse_ * (0.5f + 0.5f * sin(time * 9.0f));
        const Vector4 boardColor(1.0f + warn * 0.25f, 1.0f - warn * 0.35f, 1.0f - warn * 0.40f, 1.0f);
        board_.color = boardColor;
        capLeft_.anchoredPosition = Vector2(origin.x, origin.y);
        capLeft_.size = Vector2(capW, panelH);
        capLeft_.color = boardColor;
        capRight_.anchoredPosition = Vector2(origin.x + panelWidth - capW, origin.y);
        capRight_.size = Vector2(capW, panelH);
        capRight_.color = boardColor;

        const Vector4 foliageColor(foliageBrightness, foliageBrightness, foliageBrightness, 1.0f);
        const float pipLeft = origin.x + capW + padding;
        const float pipTop = origin.y + 22.0f * s;
        float x = 0.0f;
        for (uint i = 0; i < pips_.length(); ++i) {
            StaminaPip@ pip = pips_[i];
            if (int(i) >= visiblePipCount_) {
                if (pip.image.gameObject.active) {
                    pip.image.gameObject.active = false;
                }
                continue;
            }
            if (!pip.image.gameObject.active) {
                pip.image.gameObject.active = true;
            }
            const float phase = float(i) * 0.55f;
            const float sway = sin(time * swaySpeed + phase) * swayAmplitude;
            const float pipBob = sin(time * swaySpeed * 0.8f + phase * 0.6f) * bob;
            // 実っている間だけ縦に伸び縮みする（生えるときは少し行き過ぎて戻る）。皮はいつも原寸
            const float height = pip.showsFruit ? pipH * Max(0.0f, GrowEase(pip.grow)) * gainStretch : pipH;
            pip.image.size = Vector2(pipW, height);
            pip.image.anchoredPosition = Vector2(pipLeft + x + pipW * 0.5f, pipTop + pipBob);
            pip.image.rotation = sway;
            x += pitch;
            if ((i + 1) % 5 == 0 && int(i) + 1 != visiblePipCount_) {
                x += bunchGap;
            }
        }

        // 板の縁の蔦。上下交互に並べ、1 本ずつ位相をずらして揺らす
        const float vineW = 104.0f * s;
        const float vineH = 38.0f * s;
        const float overhang = 12.0f * s;
        const float inner = panelWidth - capW * 2.0f;
        for (uint i = 0; i < vines_.length(); ++i) {
            const float t = (float(i) + 0.5f) / float(vines_.length());
            const bool onTop = (i % 2) == 0;
            const float centerY = onTop ? origin.y - overhang + vineH * 0.5f : origin.y + panelH + overhang - vineH * 0.5f;
            const float phase = float(i) * 1.1f;
            vines_[i].size = Vector2(vineW, vineH);
            vines_[i].anchoredPosition = Vector2(origin.x + capW + inner * t,
                centerY + sin(time * swaySpeed * 0.5f + phase) * bob * 0.5f);
            vines_[i].rotation = (onTop ? 0.0f : kPi) + sin(time * swaySpeed * 0.45f + phase) * 0.05f;
            vines_[i].color = foliageColor;
        }

        // 端木に絡んだ葉。房より一段ゆっくり揺らす
        const float leafInset = 8.0f * s;
        for (uint i = 0; i < leaves_.length(); ++i) {
            leaves_[i].size = Vector2(44.0f * s, 26.0f * s);
            leaves_[i].color = foliageColor;
        }
        leaves_[0].anchoredPosition = Vector2(origin.x + leafInset, origin.y + leafInset * 0.7f);
        leaves_[0].rotation = -0.35f + sin(time * swaySpeed * 0.7f) * 0.12f;
        leaves_[1].anchoredPosition = Vector2(origin.x + panelWidth - leafInset, origin.y + panelH - leafInset);
        leaves_[1].rotation = kPi + 0.35f + sin(time * swaySpeed * 0.7f + 1.7f) * 0.12f;
    }
}

// 生えるときだけ少し行き過ぎてから戻る
float GrowEase(float t)
{
    const float inv = Clamp(t, 0.0f, 1.0f) - 1.0f;
    return 1.0f + inv * inv * (inv * 1.2f + 1.2f);
}
