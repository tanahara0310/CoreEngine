// 飾りのオブジェクトを回し、上下にゆっくり揺らす
[DisplayName("シューティング：回転と揺れ")]
class ShootingSpinner : ScriptComponent
{
    [Tooltip("1 秒あたりの回転（ラジアン）")]
    Vector3 spin = Vector3(0.0f, 0.8f, 0.0f);

    [Tooltip("上下に揺れる幅")]
    float bobHeight = 0.25f;

    [Tooltip("1 秒あたりの上下の往復の回数")]
    float bobSpeed = 0.4f;

    private Vector3 basePosition_;
    private float time_ = 0.0f;
    private bool started_ = false;
    private bool halted_ = false;

    void Update()
    {
        if (halted_) {
            return;
        }
        if (!started_) {
            started_ = true;
            basePosition_ = owner.transform.position;
        }
        const float dt = Time::DeltaTime();
        time_ += dt;
        owner.transform.rotation = owner.transform.rotation + spin * dt;
        const float offset = sin(time_ * bobSpeed * 6.2831855f) * bobHeight;
        owner.transform.position = basePosition_ + Vector3(0.0f, offset, 0.0f);
    }

    // 回転と揺れを止める
    void Halt()
    {
        halted_ = true;
    }
}
