// スタートの案内の文字を下から浮かび上がらせ、待機中は少し大きくしたり戻したりする。
// 決定したら大きくしてから小さくしながら消す
[DisplayName("スタートの案内の演出")]
class TitleHint : TitleIntro
{
    [DisplayName("出始めるまでの待ち（秒）"), Range(0, 5)]
    float introDelay = 0.75f;

    [DisplayName("下から滑らせる距離（px）"), Range(0, 200)]
    float slideDistance = 12.0f;

    [DisplayName("出てくる時間（秒）"), Range(0.05, 3)]
    float introDuration = 0.35f;

    [DisplayName("待機中に大きくする倍率"), Range(1, 1.2)]
    float idleScale = 1.025f;

    [DisplayName("待機中の片道の時間（秒）"), Range(0.1, 5)]
    float idleDuration = 1.15f;

    [DisplayName("決定で大きくする倍率"), Range(1, 3)]
    float reactionScale = 1.35f;

    [DisplayName("決定の演出の時間（秒）"), Range(0.05, 1)]
    float reactionDuration = 0.25f;

    [DisplayName("Tween の ID")]
    string tweenId = "title_start_hint_intro";

    private UIText@ text_;
    private float baseFontSize_ = 32.0f;
    private bool reactionStarted_ = false;

    void Start() override
    {
        @text_ = owner.uiText;
        if (!text_.exists) {
            Warn(owner.name + " の TitleHint: UIText ではないので、演出しません");
            @text_ = null;
            return;
        }

        baseFontSize_ = text_.fontSize;
        const Vector2 endPosition = text_.anchoredPosition;
        const Vector4 endColor = text_.color;
        const Vector2 startPosition = Vector2(endPosition.x, endPosition.y + slideDistance);
        Vector4 startColor = endColor;
        startColor.w = 0.0f;

        text_.anchoredPosition = startPosition;
        text_.color = startColor;

        // シーケンスを作ってから、中身の Tween を 1 本ずつ作って足す
        TweenSequence intro = Tween::Sequence();
        intro.Append(Tween::To(startPosition, endPosition, introDuration, TweenVector2Setter(this.SetAnchoredPosition))
            .SetEase(EaseType::EaseOutCubic));
        intro.Join(Tween::To(startColor, endColor, introDuration, TweenVector4Setter(this.SetColor))
            .SetEase(EaseType::EaseOutCubic));
        intro.SetDelay(introDelay)
            .SetLink(owner)
            .SetUpdateType(TweenUpdate::Unscaled)
            .SetId(tweenId)
            .OnComplete(TweenCallback(this.OnIntroFinished));
    }

    // 決定の演出を始め、終わったら onFinished を呼ぶ（2 回目からと、文字が無いときはすぐ呼ぶ）
    void PlayStartReaction(TweenCallback@ onFinished)
    {
        if (reactionStarted_) {
            if (onFinished !is null) {
                onFinished();
            }
            return;
        }
        reactionStarted_ = true;

        Tween::KillById(tweenId + "_idle");

        if (text_ is null) {
            if (onFinished !is null) {
                onFinished();
            }
            return;
        }

        Tween::KillById(tweenId);
        NotifyIntroComplete();

        const float startFontSize = text_.fontSize;
        const float peakFontSize = baseFontSize_ * reactionScale;
        const Vector4 startColor = text_.color;
        Vector4 midColor = startColor;
        midColor.w *= 0.5f;
        Vector4 endColor = startColor;
        endColor.w = 0.0f;

        const float growDuration = reactionDuration * 0.35f;
        const float shrinkDuration = reactionDuration - growDuration;

        // シーケンスを作ってから、中身の Tween を 1 本ずつ作って足す
        TweenSequence reaction = Tween::Sequence();
        reaction.Append(Tween::To(startFontSize, peakFontSize, growDuration, TweenFloatSetter(this.SetFontSize))
            .SetEase(EaseType::EaseOutCubic));
        reaction.Join(Tween::To(startColor, midColor, growDuration, TweenVector4Setter(this.SetColor))
            .SetEase(EaseType::EaseInCubic));
        reaction.Append(Tween::To(peakFontSize, baseFontSize_, shrinkDuration, TweenFloatSetter(this.SetFontSize))
            .SetEase(EaseType::EaseInCubic));
        reaction.Join(Tween::To(midColor, endColor, shrinkDuration, TweenVector4Setter(this.SetColor))
            .SetEase(EaseType::EaseOutCubic));
        if (onFinished !is null) {
            reaction.AppendCallback(onFinished);
        }
        reaction
            .SetLink(owner)
            .SetUpdateType(TweenUpdate::Unscaled)
            .SetId(tweenId + "_start_reaction");
    }

    private void OnIntroFinished()
    {
        StartIdleAnimation();
        NotifyIntroComplete();
    }

    // 文字の大きさを元の大きさと倍率を掛けた大きさの間で往復させる
    private void StartIdleAnimation()
    {
        if (text_ is null) {
            return;
        }

        Tween::KillById(tweenId + "_idle");

        Tween::To(baseFontSize_, baseFontSize_ * idleScale, idleDuration, TweenFloatSetter(this.SetFontSize))
            .SetEase(EaseType::EaseInOutSine)
            .SetLoops(-1, TweenLoop::Yoyo)
            .SetLink(owner)
            .SetUpdateType(TweenUpdate::Unscaled)
            .SetId(tweenId + "_idle");
    }

    private void SetAnchoredPosition(const Vector2 &in value)
    {
        if (text_ !is null) {
            text_.anchoredPosition = value;
        }
    }

    private void SetColor(const Vector4 &in value)
    {
        if (text_ !is null) {
            text_.color = value;
        }
    }

    private void SetFontSize(float value)
    {
        if (text_ !is null) {
            text_.fontSize = value;
        }
    }
}

// from と同じシーンから名前でオブジェクトを探し、付いている TitleHint を返す（無ければ警告して null）
TitleHint@ FindTitleHint(GameObject@ from, const string &in name)
{
    TitleHint@ hint;
    GameObject@ object = from.FindObject(name);
    if (object is null || !object.GetComponent(@hint)) {
        Warn(from.name + ": " + name + " の TitleHint が見つかりません");
    }
    return hint;
}
