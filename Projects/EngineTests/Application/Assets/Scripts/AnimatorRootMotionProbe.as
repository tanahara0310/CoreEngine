using namespace CoreEngine;

// クリップの移動（ルートモーション）の試験
// 始めのフレームで state の状態へ切り替え、2 秒ごとに始めの場所からの距離と進む向きをコンソールへ出す
// resetDistance が 0 より大きいと、始めの場所からその距離を越えたところで始めの場所へ戻す
[DisplayName("試験：クリップの移動")]
class AnimatorRootMotionProbe : ScriptComponent
{
    [Tooltip("始めに流す状態（Forward・Turn・Idle）")]
    string state = "Forward";

    [Tooltip("始めの場所からこの距離（m）を越えたら始めの場所へ戻す。0 なら戻さない")]
    float resetDistance = 0.0f;

    private bool started_ = false;
    private Vector3 start_;
    private Vector3 last_;
    private float heading_ = 0.0f;
    private float time_ = 0.0f;
    private float report_ = 0.0f;

    void Update()
    {
        Animator@ animator = owner.animator;
        if (animator is null) {
            return;
        }
        if (!started_) {
            started_ = true;
            animator.Play(state);
            start_ = owner.transform.position;
            last_ = start_;
            Log("AnimatorRootMotionProbe（" + owner.name + "）: " + state + " を流す・クリップの移動 " + (animator.applyRootMotion ? "あり" : "なし"));
            return;
        }

        const Vector3 position = owner.transform.position;
        const float stepX = position.x - last_.x;
        const float stepZ = position.z - last_.z;
        if (stepX * stepX + stepZ * stepZ > 1e-8f) {
            heading_ = atan2(stepX, stepZ) * 57.29578f;
        }
        last_ = position;

        time_ += Time::DeltaTime();
        report_ += Time::DeltaTime();
        const float dx = position.x - start_.x;
        const float dz = position.z - start_.z;
        const float distance = sqrt(dx * dx + dz * dz);
        if (report_ >= 2.0f) {
            report_ -= 2.0f;
            Log("AnimatorRootMotionProbe（" + owner.name + "）: " + formatFloat(time_, "", 0, 1) + " 秒・始めから "
                + formatFloat(distance, "", 0, 2) + " m・進む向き " + formatFloat(heading_, "", 0, 0) + " 度・高さ "
                + formatFloat(position.y, "", 0, 3));
        }
        if (resetDistance > 0.0f && distance >= resetDistance) {
            owner.transform.position = start_;
            last_ = start_;
            Log("AnimatorRootMotionProbe（" + owner.name + "）: " + formatFloat(distance, "", 0, 2) + " m 進んだので始めの場所へ戻す");
        }
    }
}
