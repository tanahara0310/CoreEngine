// タイトルの登場の演出が終わったときに呼ぶ関数
funcdef void TitleIntroCallback();

// タイトルの登場の演出の基底クラス。
// 登場が終わったことを 1 回だけ知らせ、壊れるときに持ち主へ付けた Tween を止める
abstract class TitleIntro : ScriptComponent
{
    private TitleIntroCallback@ onIntroComplete_;
    private bool introCompleteNotified_ = false;

    // 登場が終わったときに呼ぶ関数を渡す
    void SetOnIntroComplete(TitleIntroCallback@ callback)
    {
        @onIntroComplete_ = callback;
    }

    void OnDestroy() override
    {
        @onIntroComplete_ = null;
        Tween::KillByLink(owner);
    }

    // 登場が終わったことを知らせる（2 回目からは何もしない）
    protected void NotifyIntroComplete()
    {
        if (introCompleteNotified_) {
            return;
        }
        introCompleteNotified_ = true;
        if (onIntroComplete_ !is null) {
            onIntroComplete_();
        }
    }
}
