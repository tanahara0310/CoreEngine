using namespace CoreEngine;

// レイヤーとマスクの試験
// useLayer が true：cycle 秒ごとに、1/12 で重み 1・5/12 で重み 0.5・9/12 で重み 0 にして、そのたびに Attack のトリガーを立てる
// useLayer が false：上のレイヤー（UpperBody）の重みをいつも 0 にする（脚の動きを比べる見本）
// 上のレイヤーの状態が移るたびと、Swing のイベントを受けるたびにコンソールへ出す
[DisplayName("試験：レイヤー")]
class AnimatorLayerProbe : ScriptComponent
{
    [Tooltip("外すと、上のレイヤーの重みをいつも 0 にして、トリガーも立てない")]
    bool useLayer = true;

    [Tooltip("1 周の秒数")]
    float cycle = 12.0f;

    private float time_ = 0.0f;
    private int step_ = -1;
    private string lastState_ = "";
    private bool footLogged_ = false;

    void Update()
    {
        Animator@ animator = owner.animator;
        if (animator is null) {
            return;
        }

        if (!useLayer) {
            animator.SetLayerWeight("UpperBody", 0.0f);
        } else {
            time_ += Time::DeltaTime();
            if (time_ >= cycle) {
                time_ -= cycle;
                step_ = -1;
            }
            const int step = StepAt(time_);
            if (step != step_) {
                step_ = step;
                if (step >= 0) {
                    const float weight = step == 0 ? 1.0f : (step == 1 ? 0.5f : 0.0f);
                    animator.SetLayerWeight("UpperBody", weight);
                    animator.SetTrigger("Attack");
                    Log("AnimatorLayerProbe（" + owner.name + "）: 重み " + formatFloat(animator.GetLayerWeight("UpperBody"), "", 0, 2) + " で Attack");
                }
            }
        }

        string state = animator.GetLayerState("UpperBody");
        if (state != lastState_) {
            Log("AnimatorLayerProbe（" + owner.name + "）: UpperBody " + (lastState_.isEmpty() ? "（始め）" : lastState_) + " → " + state
                + "・Base は " + animator.currentState);
            lastState_ = state;
        }
    }

    void OnAnimationEvent(AnimationEvent@ event)
    {
        if (event.name == "Swing") {
            Log("AnimatorLayerProbe（" + owner.name + "）: " + event.name + "（レイヤー " + event.layer + "・クリップ " + event.clip + "・状態 " + event.state + "）");
        } else if (!footLogged_) {
            footLogged_ = true;
            Log("AnimatorLayerProbe（" + owner.name + "）: " + event.name + "（レイヤー " + event.layer + "・クリップ " + event.clip + "）");
        }
    }

    private int StepAt(float t)
    {
        const float unit = cycle / 12.0f;
        if (t >= 9.0f * unit) {
            return 2;
        }
        if (t >= 5.0f * unit) {
            return 1;
        }
        if (t >= 1.0f * unit) {
            return 0;
        }
        return -1;
    }
}
