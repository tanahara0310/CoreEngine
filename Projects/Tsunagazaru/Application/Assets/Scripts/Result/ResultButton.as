using namespace CoreEngine;

// 選択中の文字の色（元の値。露出を打ち消さずに描いていたので、今の露出を掛けて画面の色にする）
const Vector4 kResultSelectedColor = Vector4(0.30f, 0.72f, 1.0f, 1.0f);

// リザルトの選択肢（「もういちど」「タイトルへ」）の文字の動き。
// 選ぶと色が変わって少し大きくなり上下に揺れる。決定すると膨らんでから薄れて消え、選ばなかった方は縮んで消える
class ResultButton
{
    float reactionScale = 0.88f;
    float reactionDuration = 0.22f;
    float selectedScale = 1.08f;
    float confirmScale = 1.28f;
    float confirmDuration = 0.36f;
    float unselectedFadeScale = 0.82f;
    float unselectedFadeDuration = 0.18f;
    float selectedBobDistance = 7.0f;
    float selectedBobDuration = 0.75f;

    UIText@ text;
    private string tweenId_;
    private Vector4 normalColor_;
    private float baseFontSize_ = 0.0f;
    private float basePositionY_ = 0.0f;
    private bool selected_ = false;
    private bool started_ = false;

    ResultButton(UIText@ label, const string &in tweenId)
    {
        @text = label;
        tweenId_ = tweenId;
    }

    float GetBaseFontSize() const { return baseFontSize_; }

    // 文字の色・大きさ・高さが決まった後に 1 度呼ぶ（それを選んでいないときの見た目として覚える）
    void Start()
    {
        normalColor_ = text.color;
        baseFontSize_ = text.fontSize;
        basePositionY_ = text.anchoredPosition.y;
        started_ = true;
        text.fontSize = selected_ ? baseFontSize_ * selectedScale : baseFontSize_;
        ApplyColor(selected_ ? SelectedColor() : normalColor_);
        if (selected_) {
            StartSelectedIdle();
        }
    }

    void SetSelected(bool selected)
    {
        selected_ = selected;
        if (!started_) {
            return;
        }
        Tween::KillById(tweenId_ + "_color");
        Tween::To(text.color, selected_ ? SelectedColor() : normalColor_, 0.10f, TweenVector4Setter(this.ApplyColor))
            .SetEase(EaseType::EaseOutCubic).SetLink(text.gameObject).SetUpdateType(TweenUpdate::Unscaled).SetId(tweenId_ + "_color");
        if (selected_) {
            text.fontSize = baseFontSize_ * selectedScale;
        } else {
            Tween::KillById(tweenId_ + "_reaction");
            Tween::KillById(tweenId_ + "_confirm");
            Tween::KillById(tweenId_ + "_bob");
            text.anchoredPosition = Vector2(text.anchoredPosition.x, basePositionY_);
            text.fontSize = baseFontSize_;
        }
    }

    // 選び直した瞬間に一度縮んで戻り、そのあと上下に揺れ始める
    void PlaySelectionReaction()
    {
        if (!selected_) {
            return;
        }
        StopTweens();
        const float growDuration = reactionDuration * 0.35f;
        const float selectedFontSize = baseFontSize_ * selectedScale;
        const float pressedFontSize = selectedFontSize * reactionScale;
        TweenSequence sequence = Tween::Sequence();
        sequence.Append(Tween::To(text.fontSize, pressedFontSize, growDuration, TweenFloatSetter(this.SetFontSize))
            .SetEase(EaseType::EaseInCubic));
        sequence.Append(Tween::To(pressedFontSize, selectedFontSize, reactionDuration - growDuration, TweenFloatSetter(this.SetFontSize))
            .SetEase(EaseType::EaseOutBack));
        sequence.AppendCallback(TweenCallback(this.OnReactionFinished));
        sequence.SetLink(text.gameObject).SetUpdateType(TweenUpdate::Unscaled).SetId(tweenId_ + "_reaction");
    }

    // 決定：膨らんでから薄れて消え、終わったら onFinished を呼ぶ
    void PlayConfirmReaction(TweenCallback@ onFinished)
    {
        StopTweens();
        const float growDuration = confirmDuration * 0.30f;
        const float fadeDuration = confirmDuration - growDuration;
        const float selectedFontSize = baseFontSize_ * selectedScale;
        const float peakFontSize = selectedFontSize * confirmScale;
        const Vector4 startColor = text.color;
        const Vector4 endColor = Vector4(startColor.x, startColor.y, startColor.z, 0.0f);
        TweenSequence sequence = Tween::Sequence();
        sequence.Append(Tween::To(text.fontSize, peakFontSize, growDuration, TweenFloatSetter(this.SetFontSize))
            .SetEase(EaseType::EaseOutCubic));
        sequence.Append(Tween::To(peakFontSize, selectedFontSize, fadeDuration, TweenFloatSetter(this.SetFontSize))
            .SetEase(EaseType::EaseInCubic));
        sequence.Join(Tween::To(startColor, endColor, fadeDuration, TweenVector4Setter(this.ApplyColor))
            .SetEase(EaseType::EaseInCubic));
        sequence.AppendCallback(onFinished);
        sequence.SetLink(text.gameObject).SetUpdateType(TweenUpdate::Unscaled).SetId(tweenId_ + "_confirm");
    }

    // 選ばなかった方：縮みながら薄れて消える
    void PlayUnselectedFade()
    {
        StopTweens();
        const Vector4 startColor = text.color;
        Tween::To(text.fontSize, baseFontSize_ * unselectedFadeScale, unselectedFadeDuration, TweenFloatSetter(this.SetFontSize))
            .SetEase(EaseType::EaseInCubic).SetLink(text.gameObject).SetUpdateType(TweenUpdate::Unscaled).SetId(tweenId_ + "_unselected_fade");
        Tween::To(startColor, Vector4(startColor.x, startColor.y, startColor.z, 0.0f), unselectedFadeDuration, TweenVector4Setter(this.ApplyColor))
            .SetEase(EaseType::EaseInCubic).SetLink(text.gameObject).SetUpdateType(TweenUpdate::Unscaled).SetId(tweenId_ + "_unselected_fade_color");
    }

    private void OnReactionFinished()
    {
        if (selected_) {
            StartSelectedIdle();
        }
    }

    private void StartSelectedIdle()
    {
        Tween::KillById(tweenId_ + "_bob");
        Tween::To(basePositionY_, basePositionY_ - selectedBobDistance, selectedBobDuration, TweenFloatSetter(this.SetPositionY))
            .SetEase(EaseType::EaseInOutSine).SetLoops(-1, TweenLoop::Yoyo).SetLink(text.gameObject)
            .SetUpdateType(TweenUpdate::Unscaled).SetId(tweenId_ + "_bob");
    }

    private void SetFontSize(float value)
    {
        text.fontSize = value;
    }

    private void SetPositionY(float value)
    {
        text.anchoredPosition = Vector2(text.anchoredPosition.x, value);
    }

    // 縁取りのアルファも一緒に動かす（文字だけ薄れて輪郭が残らないように）
    private void ApplyColor(const Vector4 &in color)
    {
        text.color = color;
        const Vector4 outline = text.outlineColor;
        text.SetOutline(Vector4(outline.x, outline.y, outline.z, color.w), text.outlineWidth);
    }

    private Vector4 SelectedColor() const
    {
        const float exposure = exp2(Rendering::GetAutoExposureEV());
        return ToneMappedColor(Vector4(kResultSelectedColor.x * exposure, kResultSelectedColor.y * exposure,
            kResultSelectedColor.z * exposure, kResultSelectedColor.w));
    }

    private void StopTweens()
    {
        Tween::KillById(tweenId_ + "_reaction");
        Tween::KillById(tweenId_ + "_confirm");
        Tween::KillById(tweenId_ + "_unselected_fade");
        Tween::KillById(tweenId_ + "_unselected_fade_color");
        Tween::KillById(tweenId_ + "_bob");
    }
}
