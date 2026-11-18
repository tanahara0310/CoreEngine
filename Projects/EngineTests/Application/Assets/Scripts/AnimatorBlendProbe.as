using namespace CoreEngine;

// ブレンドツリーとアニメーションのイベントの試験
// 1D（strafe が false）：Speed を 0 → 4 → 0 と cycle 秒で往復させる（0〜2 秒 0・2〜6 秒 上げる・6〜8 秒 4・8〜12 秒 下げる）
// 2D（strafe が true）：向き (MoveX, MoveY) を前 → 右 → 後ろ → 左と cycle 秒で 1 周させ、その向きへ動かす
// 足音のイベント（FootL・FootR）を数え、1 秒ごとにコンソールへ出す
[DisplayName("試験：ブレンドツリー")]
class AnimatorBlendProbe : ScriptComponent
{
    [Tooltip("外すと、パラメータを書かない（アニメーターの窓で値を変えて試す）")]
    bool autoDrive = true;

    [Tooltip("true なら 2D の状態（Strafe）、false なら 1D の状態（Locomotion）")]
    bool strafe = false;

    [Tooltip("1 周の秒数")]
    float cycle = 12.0f;

    [Tooltip("2D のとき動く速さ（m/s）。0 ならその場で足踏み")]
    float moveSpeed = 0.6f;

    private float time_ = 0.0f;
    private float report_ = 0.0f;
    private int left_ = 0;
    private int right_ = 0;
    private string lastClip_ = "";
    private float lastValue_ = 0.0f;
    private string lastState_ = "";

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
            }
            animator.SetBool("Strafe", strafe);
            if (strafe) {
                DriveStrafe(animator);
            } else {
                animator.SetFloat("Speed", SpeedAt(time_));
            }
        }

        string state = animator.currentState;
        if (state != lastState_) {
            Log("AnimatorBlendProbe（" + owner.name + "）: 状態 " + (lastState_.isEmpty() ? "（始め）" : lastState_) + " → " + state);
            lastState_ = state;
        }

        report_ += Time::DeltaTime();
        if (report_ >= 1.0f) {
            report_ -= 1.0f;
            string value = strafe
                ? "MoveX " + formatFloat(animator.GetFloat("MoveX"), "", 0, 2) + "・MoveY " + formatFloat(animator.GetFloat("MoveY"), "", 0, 2)
                : "Speed " + formatFloat(animator.GetFloat("Speed"), "", 0, 2);
            Log("AnimatorBlendProbe（" + owner.name + "）: " + value + "・足音 L " + left_ + "・R " + right_
                + (lastClip_.isEmpty() ? "" : "（最後は " + lastClip_ + "・数 " + formatFloat(lastValue_, "", 0, 2) + "）"));
            left_ = 0;
            right_ = 0;
            lastClip_ = "";
        }
    }

    void OnAnimationEvent(AnimationEvent@ event)
    {
        if (event.name == "FootL" && event.intValue == 0 && event.stringValue == "left") {
            ++left_;
        } else if (event.name == "FootR" && event.intValue == 1 && event.stringValue == "right") {
            ++right_;
        }
        lastClip_ = event.clip;
        lastValue_ = event.floatValue;
    }

    private float SpeedAt(float t)
    {
        const float unit = cycle / 12.0f;
        if (t < 2.0f * unit) {
            return 0.0f;
        }
        if (t < 6.0f * unit) {
            return 4.0f * (t - 2.0f * unit) / (4.0f * unit);
        }
        if (t < 8.0f * unit) {
            return 4.0f;
        }
        return 4.0f * (1.0f - (t - 8.0f * unit) / (4.0f * unit));
    }

    private void DriveStrafe(Animator@ animator)
    {
        const float angle = time_ / cycle * 6.2831853f;
        const float x = sin(angle);
        const float y = cos(angle);
        animator.SetFloat("MoveX", x);
        animator.SetFloat("MoveY", y);
        // 人形は -Z を向き、右手は -X 側
        Vector3 position = owner.transform.position;
        position.x -= x * moveSpeed * Time::DeltaTime();
        position.z -= y * moveSpeed * Time::DeltaTime();
        owner.transform.position = position;
    }
}
