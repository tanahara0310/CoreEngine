using namespace CoreEngine;

// アニメーター設定の試験：時刻の表どおりにパラメータを書き、状態が移るたびにログへ出す
// 0〜2 秒 待機・2〜5 秒 歩き（Speed 1.5）・5〜7 秒 走り（Speed 4）・7 秒 待機・8 秒 攻撃・10.5〜12 秒 空中・13 秒 ジャンプ・15 秒で頭から
[DisplayName("試験：アニメーター設定")]
class AnimatorStateMachineProbe : ScriptComponent
{
    [Tooltip("外すと、パラメータを書かない（アニメーターの窓で値を変えて試す）")]
    bool autoDrive = true;

    [Tooltip("時刻の表の 1 周の秒数")]
    float cycle = 15.0f;

    private float time_ = 0.0f;
    private float previous_ = 0.0f;
    private string lastState_ = "";

    void Update()
    {
        Animator@ animator = owner.animator;
        if (animator is null) {
            return;
        }

        if (autoDrive) {
            previous_ = time_;
            time_ += Time::DeltaTime();
            if (time_ >= cycle) {
                time_ -= cycle;
                previous_ = 0.0f;
            }
            float speed = 0.0f;
            if (time_ >= 2.0f && time_ < 5.0f) {
                speed = 1.5f;
            } else if (time_ >= 5.0f && time_ < 7.0f) {
                speed = 4.0f;
            }
            animator.SetFloat("Speed", speed);
            animator.SetBool("Grounded", !(time_ >= 10.5f && time_ < 12.0f));
            if (Passed(8.0f)) {
                animator.SetTrigger("Attack");
            }
            if (Passed(13.0f)) {
                animator.SetTrigger("Jump");
            }
        }

        string state = animator.currentState;
        if (state != lastState_) {
            Log("AnimatorStateMachineProbe: " + (lastState_.isEmpty() ? "（始め）" : lastState_) + " → " + state
                + "（表の時刻 " + formatFloat(time_, "", 0, 2) + " 秒）");
            lastState_ = state;
        }
    }

    private bool Passed(float at)
    {
        return previous_ < at && time_ >= at;
    }
}
