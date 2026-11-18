using namespace CoreEngine;

// アニメーターでスプライトアニメを動かす試験
// mode 0（状態）：8 秒ごとに Speed を 0（0〜2 秒）→ 1（2〜5 秒）→ 0 にし、5 秒で Jump のトリガーを立てる
// mode 1（ブレンドツリー）：8 秒で Speed を 0 → 1 → 0 と往復させる
// 状態・スプライトのクリップ・コマが変わるたびにコンソールへ出し、Step のイベントを 1 秒ごとに数えて出す
[DisplayName("試験：スプライトのアニメーター")]
class AnimatorSpriteProbe : ScriptComponent
{
    [Tooltip("0 なら状態の切り替え、1 ならブレンドツリー")]
    int mode = 0;

    [Tooltip("外すと、パラメータを書かない（アニメーターの窓で値を変えて試す）")]
    bool autoDrive = true;

    [Tooltip("1 周の秒数")]
    float cycle = 8.0f;

    [Tooltip("コマが変わるたびに出す")]
    bool logFrames = true;

    private float time_ = 0.0f;
    private float report_ = 0.0f;
    private bool jumped_ = false;
    private int steps_ = 0;
    private string lastState_ = "";
    private string lastClip_ = "";
    private int lastFrame_ = -1;

    void Update()
    {
        Animator@ animator = owner.animator;
        if (animator is null) {
            return;
        }

        if (autoDrive) {
            time_ += Time::DeltaTime();
            if (time_ >= cycle) {
                time_ -= cycle;
                jumped_ = false;
            }
            animator.SetFloat("Speed", SpeedAt(time_));
            if (mode == 0 && !jumped_ && time_ >= cycle * 5.0f / 8.0f) {
                jumped_ = true;
                animator.SetTrigger("Jump");
            }
        }

        const string state = animator.currentState;
        const string clip = owner.spriteAnimator.clip;
        const int frame = owner.spriteRenderer.frame;
        if (state != lastState_) {
            Log("AnimatorSpriteProbe（" + owner.name + "）: 状態 " + (lastState_.isEmpty() ? "（始め）" : lastState_) + " → " + state
                + "・クリップ " + clip + "・コマ " + frame);
            lastState_ = state;
        } else if (clip != lastClip_ || (logFrames && frame != lastFrame_)) {
            Log("AnimatorSpriteProbe（" + owner.name + "）: クリップ " + clip + "・コマ " + frame
                + "・Speed " + formatFloat(animator.GetFloat("Speed"), "", 0, 2));
        }
        lastClip_ = clip;
        lastFrame_ = frame;

        report_ += Time::DeltaTime();
        if (report_ >= 1.0f) {
            report_ -= 1.0f;
            Log("AnimatorSpriteProbe（" + owner.name + "）: 1 秒の Step " + steps_);
            steps_ = 0;
        }
    }

    void OnAnimationEvent(AnimationEvent@ event)
    {
        if (event.name == "Step" && event.clip == "Run" && event.intValue == 1 && event.stringValue == "run") {
            ++steps_;
        }
    }

    private float SpeedAt(float t)
    {
        const float unit = cycle / 8.0f;
        if (mode == 0) {
            return t >= 2.0f * unit && t < 5.0f * unit ? 1.0f : 0.0f;
        }
        return t < 4.0f * unit ? t / (4.0f * unit) : 2.0f - t / (4.0f * unit);
    }
}
