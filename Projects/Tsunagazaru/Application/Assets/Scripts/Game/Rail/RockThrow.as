// 石が着いたときに呼ぶ関数
funcdef void RockImpactCallback();

// トロッコから岩へ石を放物線で投げる。時間はスケールされない時間で進める
[DisplayName("投石")]
class RockThrow : ScriptComponent
{
    [DisplayName("飛ぶ時間"), Range(0.01, 5)]
    float duration = 0.6f;

    [DisplayName("放物線の高さ"), Range(0, 20)]
    float arcHeight = 2.0f;

    [DisplayName("石の大きさ"), Range(0.01, 5)]
    float stoneScale = 0.2f;

    [DisplayName("石の回る速さ"), Range(-100, 100)]
    float rotationSpeed = 12.0f;

    private Vector3 start_;
    private Vector3 target_;
    private float elapsed_ = 0.0f;
    private bool isPlaying_ = false;
    private RockImpactCallback@ onImpact_;

    void Start() override
    {
        owner.meshRenderer.enabled = false;
    }

    bool IsPlaying() const
    {
        return isPlaying_;
    }

    bool Play(const Vector3 &in start, const Vector3 &in target, RockImpactCallback@ onImpact)
    {
        if (isPlaying_) {
            return false;
        }
        start_ = start;
        target_ = target;
        @onImpact_ = onImpact;
        elapsed_ = 0.0f;
        isPlaying_ = true;
        owner.transform.position = start_;
        owner.transform.rotation = Vector3(0, 0, 0);
        owner.transform.scale = Vector3(stoneScale, stoneScale, stoneScale);
        owner.meshRenderer.enabled = true;
        return true;
    }

    // GameDirector が Update から呼ぶ
    void Tick()
    {
        if (!isPlaying_) {
            return;
        }
        const float deltaTime = Max(0.0f, Time::UnscaledDeltaTime());
        elapsed_ = Min(elapsed_ + deltaTime, duration);
        const float progress = Clamp(elapsed_ / duration, 0.0f, 1.0f);
        owner.transform.position = Vector3(
            start_.x + (target_.x - start_.x) * progress,
            start_.y + (target_.y - start_.y) * progress + arcHeight * 4.0f * progress * (1.0f - progress),
            start_.z + (target_.z - start_.z) * progress);
        Vector3 rotation = owner.transform.rotation;
        rotation.x += rotationSpeed * deltaTime;
        rotation.z += rotationSpeed * 0.7f * deltaTime;
        owner.transform.rotation = rotation;
        if (elapsed_ < duration) {
            return;
        }

        isPlaying_ = false;
        owner.meshRenderer.enabled = false;
        RockImpactCallback@ onImpact = onImpact_;
        @onImpact_ = null;
        if (onImpact !is null) {
            onImpact();
        }
    }
}
