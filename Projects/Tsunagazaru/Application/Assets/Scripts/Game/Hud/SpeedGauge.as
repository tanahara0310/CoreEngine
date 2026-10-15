// 速度計の 1 桁（今の数字と、送り出している前の数字）
class SpeedDigit
{
    UIText@ current;
    UIText@ outgoing;
    int value = -1;
    int outgoingValue = -1;
    float roll = 1.0f;      // 0 = 送り始め ／ 1 = 送り終わり
    float direction = 1.0f; // 増えたら 1（下から上へ）、減ったら -1
}

// トロッコの速さを km/h のオドメーターで見せる。桁が変わるときは下（または上）から入れ替わる。
// 停車中は 0 を出し、発車したら一気に振り切る。駅で減速したら赤く点滅して沈み、落ちた量を「▼」で出す
[DisplayName("速度計")]
class SpeedGauge : ScriptComponent
{
    [DisplayName("位置（左上から・px）")]
    Vector2 position = Vector2(32.0f, 152.0f);

    [DisplayName("表示の倍率"), Range(0.25, 2.5)]
    float scale = 1.3f;

    [DisplayName("板を緑へ寄せる強さ"), Range(0, 1)]
    float boardGreenTint = 0.22f;

    [DisplayName("1 マスの長さ（m）"), Range(0.1, 20)]
    float metersPerCell = 2.0f;

    [DisplayName("描画順"), Range(0, 5000)]
    int sortOrder = 880;

    [DisplayName("蔦の揺れの速さ"), Range(0, 10)]
    float swaySpeed = 1.8f;

    [DisplayName("減速で赤くなっている時間（秒）"), Range(0.05, 4)]
    float slowdownSeconds = 1.1f;

    [DisplayName("減速で沈む深さ（px）"), Range(0, 60)]
    float slowdownDip = 14.0f;

    [DisplayName("赤く点滅する回数"), Range(0, 8)]
    float slowdownBlinks = 3.0f;

    [DisplayName("演出が最大になる落差（km/h）"), Range(0.5, 100)]
    float slowdownFullDropKmh = 20.0f;

    [DisplayName("減速で板を赤へ寄せる強さ"), Range(0, 1)]
    float slowdownBoardTint = 0.55f;

    [DisplayName("落差の文字が落ちる距離（px）"), Range(0, 200)]
    float slowdownDropFall = 30.0f;

    [DisplayName("落差の文字の大きさ"), Range(4, 48)]
    float slowdownDropFontSize = 15.0f;

    [DisplayName("文字のフォント")]
    string font = "x8y12pxDenkiChip.ttf";

    private TrainMovement@ train_;
    private UIImage@ board_;
    private UIImage@ capLeft_;
    private UIImage@ capRight_;
    private array<UIImage@> vines_;
    private array<SpeedDigit@> digits_;
    private UIText@ dot_;
    private UIText@ unit_;
    private UIText@ dropLabel_;
    private Vector3 lastTrainPosition_;
    private bool hasTrainPosition_ = false;
    private float displayedKmh_ = 0.0f;
    private float elapsed_ = 0.0f;
    private float introReveal_ = 1.0f;
    private float slowdownElapsed_ = -1.0f;
    private float slowdownStrength_ = 0.0f;

    // 元の色（トーンマップの前のリニア値）。画面に出ていた色へ写して使う
    private Vector4 digitColor_;
    private Vector4 dotColor_;
    private Vector4 unitColor_;
    private Vector4 outlineColor_;
    private Vector4 slowdownDigitColor_;
    private Vector4 dropLabelColor_;

    // GameDirector が最初に呼ぶ
    void Setup(TrainMovement@ train)
    {
        @train_ = train;
        digitColor_ = OriginalHudColor(Vector4(0.980f, 0.839f, 0.200f, 1.0f));
        dotColor_ = OriginalHudColor(Vector4(0.573f, 0.341f, 0.024f, 1.0f));
        unitColor_ = OriginalHudColor(Vector4(0.620f, 0.565f, 0.471f, 1.0f));
        outlineColor_ = OriginalHudColor(Vector4(0.031f, 0.020f, 0.012f, 1.0f));
        slowdownDigitColor_ = OriginalHudColor(Vector4(0.300f, 0.010f, 0.006f, 1.0f));
        dropLabelColor_ = OriginalHudColor(Vector4(0.420f, 0.014f, 0.008f, 1.0f));
        BuildParts();
    }

    void SetIntroReveal(float reveal)
    {
        introReveal_ = Clamp(reveal, 0.0f, 2.0f);
    }

    // 駅で減速したとき（落ちた速さ・マス/秒）。落差に比例した強さで、毎回頭から鳴らし直す
    void PlaySlowdownFlash(float droppedCellsPerSecond)
    {
        if (droppedCellsPerSecond <= 0.0f) {
            return;
        }
        const float droppedKmh = droppedCellsPerSecond * Max(0.0f, metersPerCell) * 3.6f;
        slowdownStrength_ = Max(Clamp(droppedKmh / Max(slowdownFullDropKmh, 0.01f), 0.0f, 1.0f), 0.35f);
        slowdownElapsed_ = 0.0f;
        if (dropLabel_ !is null) {
            const int scaled = int(Clamp(droppedKmh, 0.0f, 999.9f) * 10.0f + 0.5f);
            dropLabel_.text = "▼" + (scaled / 10) + "." + (scaled % 10);
        }
    }

    void Update() override
    {
        if (board_ is null || GamePause::paused) {
            return;
        }
        const float deltaTime = Time::UnscaledDeltaTime();
        elapsed_ += deltaTime;
        if (slowdownElapsed_ >= 0.0f) {
            slowdownElapsed_ += deltaTime;
            if (slowdownElapsed_ >= slowdownSeconds) {
                slowdownElapsed_ = -1.0f;
            }
        }

        // 発車前と停止中は 0。上がるときは毎秒 22km/h、下がるときは毎秒 38km/h で追う
        const bool moving = UpdateTrainMovingState();
        const float target = moving ? train_.GetMoveSpeed() * Max(0.0f, metersPerCell) * 3.6f : 0.0f;
        displayedKmh_ = MoveTowards(displayedKmh_, target, (target > displayedKmh_ ? 22.0f : 38.0f) * deltaTime);
        UpdateDigits(displayedKmh_, deltaTime);
        LayoutParts(elapsed_);
    }

    // ---------------------------------------------------------------- 作る

    private void BuildParts()
    {
        @board_ = owner.uiImage;
        if (!board_.exists) {
            Error("SpeedGauge: UI 画像に付けてください");
            @board_ = null;
            return;
        }
        board_.anchor = UIAnchor::TopLeft;
        board_.pivot = Vector2(0, 0);
        board_.sortOrder = sortOrder;
        @capLeft_ = SpawnHudImage(owner, kStaminaCapL, "SpeedGaugeCapL", sortOrder + 1, Vector2(0, 0), Vector2(28, 76));
        @capRight_ = SpawnHudImage(owner, kStaminaCapR, "SpeedGaugeCapR", sortOrder + 1, Vector2(0, 0), Vector2(28, 76));
        for (int i = 0; i < 6; ++i) {
            vines_.insertLast(SpawnHudImage(owner, kStaminaVine, "SpeedGaugeVine_" + i, sortOrder + 2, Vector2(0.5f, 0.5f), Vector2(104, 38)));
        }

        // 整数 3 桁と小数 1 桁
        for (int i = 0; i < 4; ++i) {
            SpeedDigit@ digit = SpeedDigit();
            @digit.current = SpawnLabel("0", "SpeedGaugeDigit_" + i, 38.0f, digitColor_, sortOrder + 4);
            @digit.outgoing = SpawnLabel("0", "SpeedGaugeDigitOut_" + i, 38.0f, digitColor_, sortOrder + 3);
            digit.outgoing.gameObject.active = false;
            digits_.insertLast(digit);
        }
        @dot_ = SpawnLabel(".", "SpeedGaugeDot", 38.0f, dotColor_, sortOrder + 4);
        @unit_ = SpawnLabel("km/h", "SpeedGaugeUnit", 22.0f, unitColor_, sortOrder + 4);
        unit_.pivot = Vector2(0.0f, 0.5f);
        @dropLabel_ = SpawnLabel("▼0.0", "SpeedGaugeDrop", slowdownDropFontSize, dropLabelColor_, sortOrder + 5);
        dropLabel_.gameObject.active = false;
    }

    private UIText@ SpawnLabel(const string &in text, const string &in name, float fontSize, const Vector4 &in color, int order)
    {
        UIText@ label = SpawnHudText(owner, font, text, name, order, fontSize);
        label.color = color;
        label.SetOutline(outlineColor_, 0.045f);
        return label;
    }

    // ---------------------------------------------------------------- 毎フレーム

    // 投石で y だけ動くことがあるので、水平方向だけを見る
    private bool UpdateTrainMovingState()
    {
        const Vector3 position = train_.GetWorldPosition();
        if (!hasTrainPosition_) {
            lastTrainPosition_ = position;
            hasTrainPosition_ = true;
            return false;
        }
        const float dx = position.x - lastTrainPosition_.x;
        const float dz = position.z - lastTrainPosition_.z;
        lastTrainPosition_ = position;
        return dx * dx + dz * dz > 1.0e-8f;
    }

    private void UpdateDigits(float kmh, float deltaTime)
    {
        // 小数 1 桁まで見せるので、10 倍した整数を桁にばらす（添字 0 が百の位）
        int scaled = int(Clamp(kmh, 0.0f, 999.9f) * 10.0f + 0.5f);
        array<int> wanted(4);
        for (int i = 0; i < 4; ++i) {
            wanted[3 - i] = scaled % 10;
            scaled /= 10;
        }
        for (uint i = 0; i < digits_.length(); ++i) {
            SpeedDigit@ digit = digits_[i];
            if (digit.value != wanted[i]) {
                // 9 から 0 へ繰り上がるときも、見た目は上へ送る
                const bool increased = digit.value < 0
                    || (wanted[i] > digit.value ? (wanted[i] - digit.value) <= 5 : (digit.value - wanted[i]) > 5);
                digit.direction = increased ? 1.0f : -1.0f;
                digit.outgoingValue = digit.value;
                digit.value = wanted[i];
                digit.roll = digit.outgoingValue < 0 ? 1.0f : 0.0f;
                digit.current.text = "" + digit.value;
                const bool showsOutgoing = digit.outgoingValue >= 0;
                digit.outgoing.gameObject.active = showsOutgoing;
                if (showsOutgoing) {
                    digit.outgoing.text = "" + digit.outgoingValue;
                }
            }
            if (digit.roll < 1.0f) {
                digit.roll = Min(1.0f, digit.roll + deltaTime / 0.14f);
                if (digit.roll >= 1.0f) {
                    digit.outgoing.gameObject.active = false;
                }
            }
        }
    }

    private float GetSlowdownProgress() const
    {
        return slowdownElapsed_ < 0.0f ? 1.0f : Clamp(slowdownElapsed_ / Max(slowdownSeconds, 0.01f), 0.0f, 1.0f);
    }

    private void LayoutParts(float time)
    {
        const float s = scale;
        const float panelH = 76.0f * s;
        const float capW = 28.0f * s;
        const float padding = 20.0f * s;
        // 整数 3 桁 ＋ 小数点 ＋ 小数 1 桁 ＋ 単位
        const float contentWidth = 36.0f * 3.0f + 5.0f * 2.0f + 4.0f + 14.0f + 4.0f + 36.0f + 12.0f + 64.0f;
        const float panelWidth = capW * 2.0f + padding * 2.0f + contentWidth * s;

        // 駅の減速。板ごと沈め（正弦の半周期）、前半は赤のまま保って後半で戻す
        const float phase = GetSlowdownProgress();
        const float fade = phase < 1.0f ? slowdownStrength_ : 0.0f;
        const float dip = sin(phase * kPi) * slowdownDip * fade;
        const float redDecay = phase <= 0.55f ? 1.0f : 1.0f - (phase - 0.55f) / (1.0f - 0.55f);
        const float envelope = redDecay * fade;
        const float blinks = Max(slowdownBlinks, 0.0f);
        const float red = blinks >= 1.0f ? (0.5f + 0.5f * cos(phase * kTau * blinks)) * envelope : envelope;
        const Vector4 digitColor = Lerp(digitColor_, slowdownDigitColor_, red);

        const Vector2 origin(position.x - (1.0f - introReveal_) * (position.x + panelWidth + 48.0f), position.y + dip);

        // 板は湿ったジャングルの木らしく緑へ寄せる。減速中は赤へ寄せる
        const Vector4 baseBoard(1.0f - boardGreenTint * 0.55f, 1.0f + boardGreenTint * 0.45f, 1.0f - boardGreenTint * 0.95f, 1.0f);
        const Vector4 slowBoard(1.0f, 0.08f, 0.05f, 1.0f);
        const Vector4 boardColor = Lerp(baseBoard, slowBoard, red * Clamp(slowdownBoardTint, 0.0f, 1.0f));
        board_.anchoredPosition = Vector2(origin.x + capW, origin.y);
        board_.size = Vector2(panelWidth - capW * 2.0f, panelH);
        board_.color = boardColor;
        capLeft_.anchoredPosition = Vector2(origin.x, origin.y);
        capLeft_.size = Vector2(capW, panelH);
        capLeft_.color = boardColor;
        capRight_.anchoredPosition = Vector2(origin.x + panelWidth - capW, origin.y);
        capRight_.size = Vector2(capW, panelH);
        capRight_.color = boardColor;

        const float vineW = 104.0f * s;
        const float vineH = 38.0f * s;
        const float overhang = 12.0f * s;
        const float inner = panelWidth - capW * 2.0f;
        for (uint i = 0; i < vines_.length(); ++i) {
            const float t = (float(i) + 0.5f) / float(vines_.length());
            const bool onTop = (i % 2) == 0;
            const float centerY = onTop ? origin.y - overhang + vineH * 0.5f : origin.y + panelH + overhang - vineH * 0.5f;
            vines_[i].size = Vector2(vineW, vineH);
            vines_[i].anchoredPosition = Vector2(origin.x + capW + inner * t, centerY);
            vines_[i].rotation = (onTop ? 0.0f : kPi) + sin(time * swaySpeed * 0.45f + float(i) * 1.1f) * 0.05f;
        }

        // 数字・小数点・単位を左から置く
        const float digitW = 36.0f * s;
        const float digitGap = 5.0f * s;
        const float dotW = 14.0f * s;
        const float dotSideGap = 4.0f * s;
        const float travel = 9.0f * s;
        const float centerY = origin.y + panelH * 0.5f;
        float cursor = origin.x + capW + padding;
        for (uint i = 0; i < digits_.length(); ++i) {
            if (i == digits_.length() - 1) {
                dot_.fontSize = 38.0f * s;
                dot_.anchoredPosition = Vector2(cursor + dotSideGap + dotW * 0.5f, centerY + 10.0f * s);
                cursor += dotSideGap + dotW + dotSideGap;
            }
            SpeedDigit@ digit = digits_[i];
            const float centerX = cursor + digitW * 0.5f;
            const float eased = 1.0f - (1.0f - Clamp(digit.roll, 0.0f, 1.0f)) * (1.0f - Clamp(digit.roll, 0.0f, 1.0f));
            // 縁取りも本体と同じ濃さで薄める
            digit.current.fontSize = 38.0f * s;
            digit.current.anchoredPosition = Vector2(centerX, centerY + (1.0f - eased) * travel * digit.direction);
            digit.current.color = Vector4(digitColor.x, digitColor.y, digitColor.z, eased);
            digit.current.SetOutline(Vector4(outlineColor_.x, outlineColor_.y, outlineColor_.z, eased), 0.045f);
            if (digit.outgoing.gameObject.active) {
                const float fadeOut = 1.0f - eased;
                digit.outgoing.fontSize = 38.0f * s;
                digit.outgoing.anchoredPosition = Vector2(centerX, centerY - eased * travel * digit.direction);
                digit.outgoing.color = Vector4(digitColor.x, digitColor.y, digitColor.z, fadeOut);
                digit.outgoing.SetOutline(Vector4(outlineColor_.x, outlineColor_.y, outlineColor_.z, fadeOut), 0.045f);
            }
            cursor += digitW;
            if (i + 2 < digits_.length()) {
                cursor += digitGap;
            }
        }
        unit_.fontSize = 22.0f * s;
        unit_.anchoredPosition = Vector2(cursor + 12.0f * s, centerY);

        LayoutDropLabel(origin, panelWidth, panelH, s);
    }

    // 「▼18.4」は板の下端から下へ落ち、出た瞬間だけ大きく、後半だけ薄くなる
    private void LayoutDropLabel(const Vector2 &in anchor, float panelWidth, float panelHeight, float s)
    {
        const float phase = GetSlowdownProgress();
        if (phase >= 1.0f) {
            if (dropLabel_.gameObject.active) {
                dropLabel_.gameObject.active = false;
            }
            return;
        }
        const float fall = (1.0f - (1.0f - phase) * (1.0f - phase)) * slowdownDropFall * s;
        const float punch = 1.0f + 0.5f * (1.0f - Min(phase / 0.18f, 1.0f));
        const float alpha = phase <= 0.6f ? 1.0f : 1.0f - (phase - 0.6f) / (1.0f - 0.6f);
        dropLabel_.fontSize = slowdownDropFontSize * s * punch;
        dropLabel_.anchoredPosition = Vector2(anchor.x + panelWidth * 0.5f, anchor.y + panelHeight + 22.0f * s + fall);
        dropLabel_.color = Vector4(dropLabelColor_.x, dropLabelColor_.y, dropLabelColor_.z, alpha);
        dropLabel_.SetOutline(Vector4(outlineColor_.x, outlineColor_.y, outlineColor_.z, alpha), 0.045f);
        if (!dropLabel_.gameObject.active) {
            dropLabel_.gameObject.active = true;
        }
    }
}
