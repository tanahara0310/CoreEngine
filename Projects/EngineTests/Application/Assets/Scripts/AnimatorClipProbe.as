using namespace CoreEngine;

// 設定を指さない Animator の試験：clips の 3 本を、SwitchWithBlend と Switch で順に切り替える
// 0 秒 Idle・2 秒 Walk（混ぜる）・4 秒 Attack（ループしない・すぐ）・5 秒 Idle（混ぜる）・6 秒で頭から
[DisplayName("試験：クリップの切り替え")]
class AnimatorClipProbe : ScriptComponent
{
    [Tooltip("混ぜる秒数")]
    float blend = 0.3f;

    private float time_ = 0.0f;
    private float previous_ = 0.0f;

    void Update()
    {
        Animator@ animator = owner.animator;
        if (animator is null) {
            return;
        }
        previous_ = time_;
        time_ += Time::DeltaTime();
        if (time_ >= 6.0f) {
            time_ -= 6.0f;
            previous_ = 0.0f;
        }
        if (Passed(2.0f)) {
            Report(animator.SwitchWithBlend("Walk", blend, true), animator);
        }
        if (Passed(4.0f)) {
            Report(animator.Switch("Attack", false), animator);
        }
        if (Passed(5.0f)) {
            Report(animator.SwitchWithBlend("Idle", blend, true), animator);
        }
    }

    private bool Passed(float at)
    {
        return previous_ < at && time_ >= at;
    }

    private void Report(bool switched, Animator@ animator)
    {
        Log("AnimatorClipProbe: " + (switched ? "切り替え " : "切り替えられない ") + animator.GetCurrentClipName());
    }
}
