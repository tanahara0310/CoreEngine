// リザルトの選択肢の文字 1 つに、選んだとき・決めたとき・選ばれなかったときの演出を付ける。
// 選んでいる間は文字を上下させる。演出の設定は作るときに ResultGauge から写す
class ResultButtonAnimator
{
    private UIText@ text_;
    private string tweenId_;
    private float reactionScale_;
    private float reactionDuration_;
    private float selectedScale_;
    private float confirmScale_;
    private float confirmDuration_;
    private float unselectedFadeScale_;
    private float unselectedFadeDuration_;
    private float selectedBobDistance_;
    private float selectedBobDuration_;
    private Vector4 selectedColor_;

    private Vector4 normalColor_ = Vector4(1.0f, 1.0f, 1.0f, 0.9f);
    private float baseFontSize_ = 40.0f;
    private float basePositionY_ = 0.0f;
    private bool selected_ = false;
    private bool started_ = false;

    ResultButtonAnimator(UIText@ text, const string &in tweenId, ResultGauge@ settings)
    {
        @text_ = text;
        tweenId_ = tweenId;
        reactionScale_ = settings.reactionScale;
        reactionDuration_ = settings.reactionDuration;
        selectedScale_ = settings.selectedScale;
        confirmScale_ = settings.confirmScale;
        confirmDuration_ = settings.confirmDuration;
        unselectedFadeScale_ = settings.unselectedFadeScale;
        unselectedFadeDuration_ = settings.unselectedFadeDuration;
        selectedBobDistance_ = settings.selectedBobDistance;
        selectedBobDuration_ = settings.selectedBobDuration;
        selectedColor_ = settings.selectedColor;
    }

    // 今の文字の色・大きさ・高さを選んでいないときの値として控え、選んでいれば選んだ見た目にして上下させる
    void Start()
    {
        if (text_ is null || !text_.exists) {
            @text_ = null;
            return;
        }

        normalColor_ = text_.color;
        baseFontSize_ = text_.fontSize;
        basePositionY_ = text_.anchoredPosition.y;
        started_ = true;

        text_.fontSize = selected_ ? baseFontSize_ * selectedScale_ : baseFontSize_;
        text_.color = selected_ ? selectedColor_ : normalColor_;
        if (selected_) {
            StartSelectedIdle();
        }
    }

    // 選んでいるかを替え、色を寄せる。選ばれなくなったら演出を止めて大きさと高さを戻す
    void SetSelected(bool selected)
    {
        selected_ = selected;
        if (text_ is null || !started_) {
            return;
        }

        Tween::KillById(tweenId_ + "_color");

        const Vector4 from = text_.color;
        const Vector4 to = selected_ ? selectedColor_ : normalColor_;
        Tween::To(from, to, 0.10f, TweenVector4Setter(this.ApplyColor))
            .SetEase(EaseType::EaseOutCubic)
            .SetLink(text_.gameObject)
            .SetUpdateType(TweenUpdate::Unscaled)
            .SetId(tweenId_ + "_color");

        if (selected_) {
            text_.fontSize = baseFontSize_ * selectedScale_;
        } else {
            Tween::KillById(tweenId_ + "_reaction");
            Tween::KillById(tweenId_ + "_confirm");
            Tween::KillById(tweenId_ + "_bob");
            text_.anchoredPosition = Vector2(text_.anchoredPosition.x, basePositionY_);
            text_.fontSize = baseFontSize_;
        }
    }

    // 選ばれた瞬間に一瞬縮めて戻し、戻ったら上下させる
    void PlaySelectionReaction()
    {
        if (text_ is null || !selected_) {
            return;
        }
        PlayScaleReaction(TweenCallback(this.OnSelectionReactionFinished));
    }

    // 一瞬大きくしてから選んだ大きさへ戻しながら消し、終わったら onFinished を呼ぶ（文字が無ければすぐ呼ぶ）
    void PlayConfirmReaction(TweenCallback@ onFinished)
    {
        if (text_ is null) {
            if (onFinished !is null) {
                onFinished();
            }
            return;
        }

        StopTweens();

        const float duration = confirmDuration_;
        const float growDuration = duration * 0.30f;
        const float fadeDuration = duration - growDuration;
        const float currentFontSize = text_.fontSize;
        const float selectedFontSize = baseFontSize_ * selectedScale_;
        const float peakFontSize = selectedFontSize * confirmScale_;
        const Vector4 startColor = text_.color;
        Vector4 endColor = startColor;
        endColor.w = 0.0f;

        // シーケンスを作ってから、中身の Tween を 1 本ずつ作って足す
        TweenSequence sequence = Tween::Sequence();
        sequence.Append(Tween::To(currentFontSize, peakFontSize, growDuration, TweenFloatSetter(this.SetFontSize))
            .SetEase(EaseType::EaseOutCubic));
        sequence.Append(Tween::To(peakFontSize, selectedFontSize, fadeDuration, TweenFloatSetter(this.SetFontSize))
            .SetEase(EaseType::EaseInCubic));
        sequence.Join(Tween::To(startColor, endColor, fadeDuration, TweenVector4Setter(this.ApplyColor))
            .SetEase(EaseType::EaseInCubic));
        if (onFinished !is null) {
            sequence.AppendCallback(onFinished);
        }
        sequence
            .SetLink(text_.gameObject)
            .SetUpdateType(TweenUpdate::Unscaled)
            .SetId(tweenId_ + "_confirm");
    }

    // 選ばれなかった文字を縮めながら消す
    void PlayUnselectedFade()
    {
        if (text_ is null) {
            return;
        }

        StopTweens();

        const float duration = unselectedFadeDuration_;
        const float currentFontSize = text_.fontSize;
        const float targetFontSize = baseFontSize_ * unselectedFadeScale_;
        const Vector4 startColor = text_.color;
        Vector4 endColor = startColor;
        endColor.w = 0.0f;

        Tween::To(currentFontSize, targetFontSize, duration, TweenFloatSetter(this.SetFontSize))
            .SetEase(EaseType::EaseInCubic)
            .SetLink(text_.gameObject)
            .SetUpdateType(TweenUpdate::Unscaled)
            .SetId(tweenId_ + "_unselected_fade");

        Tween::To(startColor, endColor, duration, TweenVector4Setter(this.ApplyColor))
            .SetEase(EaseType::EaseInCubic)
            .SetLink(text_.gameObject)
            .SetUpdateType(TweenUpdate::Unscaled)
            .SetId(tweenId_ + "_unselected_fade_color");
    }

    private void PlayScaleReaction(TweenCallback@ onFinished)
    {
        StopTweens();

        const float duration = reactionDuration_;
        const float growDuration = duration * 0.35f;
        const float restoreDuration = duration - growDuration;
        const float selectedFontSize = baseFontSize_ * selectedScale_;
        const float pressedFontSize = selectedFontSize * reactionScale_;
        const float currentFontSize = text_.fontSize;

        // シーケンスを作ってから、中身の Tween を 1 本ずつ作って足す
        TweenSequence sequence = Tween::Sequence();
        sequence.Append(Tween::To(currentFontSize, pressedFontSize, growDuration, TweenFloatSetter(this.SetFontSize))
            .SetEase(EaseType::EaseInCubic));
        sequence.Append(Tween::To(pressedFontSize, selectedFontSize, restoreDuration, TweenFloatSetter(this.SetFontSize))
            .SetEase(EaseType::EaseOutBack));
        sequence.AppendCallback(onFinished);
        sequence
            .SetLink(text_.gameObject)
            .SetUpdateType(TweenUpdate::Unscaled)
            .SetId(tweenId_ + "_reaction");
    }

    // 控えた高さと、そこから上げた高さの間で文字を往復させる
    private void StartSelectedIdle()
    {
        if (text_ is null || !selected_) {
            return;
        }

        Tween::KillById(tweenId_ + "_bob");

        const float fromY = basePositionY_;
        const float toY = basePositionY_ - selectedBobDistance_;
        Tween::To(fromY, toY, selectedBobDuration_, TweenFloatSetter(this.SetPositionY))
            .SetEase(EaseType::EaseInOutSine)
            .SetLoops(-1, TweenLoop::Yoyo)
            .SetLink(text_.gameObject)
            .SetUpdateType(TweenUpdate::Unscaled)
            .SetId(tweenId_ + "_bob");
    }

    private void OnSelectionReactionFinished()
    {
        if (selected_) {
            StartSelectedIdle();
        }
    }

    private void StopTweens()
    {
        Tween::KillById(tweenId_ + "_reaction");
        Tween::KillById(tweenId_ + "_confirm");
        Tween::KillById(tweenId_ + "_unselected_fade");
        Tween::KillById(tweenId_ + "_unselected_fade_color");
        Tween::KillById(tweenId_ + "_bob");
    }

    // 文字の色と、縁取りの色のアルファを揃えて入れる
    private void ApplyColor(const Vector4 &in color)
    {
        if (text_ is null) {
            return;
        }
        text_.color = color;
        Vector4 outline = text_.outlineColor;
        outline.w = color.w;
        text_.SetOutline(outline, text_.outlineWidth);
    }

    private void SetFontSize(float value)
    {
        if (text_ !is null) {
            text_.fontSize = value;
        }
    }

    private void SetPositionY(float value)
    {
        if (text_ !is null) {
            text_.anchoredPosition = Vector2(text_.anchoredPosition.x, value);
        }
    }
}
