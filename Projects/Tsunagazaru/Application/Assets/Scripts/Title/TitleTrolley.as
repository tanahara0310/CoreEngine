// トロッコを画面の下から出し、着いた後は上下に浮かせる。出始めるのはロゴの着地の時間と待ちの後
[DisplayName("トロッコの演出")]
class TitleTrolley : TitleIntro
{
    [DisplayName("ロゴの着地から出始めるまでの待ち（秒）"), Range(0, 5)]
    float introDelay = 0.35f;

    [DisplayName("下から出てくる時間（秒）"), Range(0.05, 5)]
    float introDuration = 0.9f;

    [DisplayName("出始めに下へずらす距離（m）"), Range(0, 30)]
    float introOffset = 8.0f;

    [DisplayName("浮かせる始点（元の位置からのずれ）"), Range(-10, 10)]
    Vector3 bobStart = Vector3(0.0f, 0.0f, 0.0f);

    [DisplayName("浮かせる終点（元の位置からのずれ）"), Range(-10, 10)]
    Vector3 bobEnd = Vector3(0.0f, 0.12f, 0.0f);

    [DisplayName("浮かせる片道の時間（秒）"), Range(0.1, 10)]
    float bobDuration = 1.6f;

    [DisplayName("ロゴのオブジェクトの名前")]
    string logoName = "title";

    private Transform@ transform_;
    private Vector3 basePosition_;

    void Start() override
    {
        @transform_ = owner.transform;
        if (!transform_.exists) {
            Warn(owner.name + " の TitleTrolley: Transform が無いので、演出しません");
            return;
        }

        TitleLogo@ logo = FindTitleLogo(owner, logoName);
        const float logoDuration = (logo !is null) ? logo.introDuration : 0.0f;
        const float delay = logoDuration + introDelay;

        basePosition_ = transform_.position;
        const float startY = basePosition_.y - introOffset;
        transform_.position = Vector3(basePosition_.x, startY, basePosition_.z);

        Tween::To(startY, basePosition_.y, introDuration, TweenFloatSetter(this.SetPositionY))
            .SetEase(EaseType::EaseOutCubic)
            .SetDelay(delay)
            .SetLink(owner)
            .SetUpdateType(TweenUpdate::Unscaled)
            .SetId("title_trolley_intro")
            .OnComplete(TweenCallback(this.OnIntroFinished));
    }

    private void OnIntroFinished()
    {
        StartIdleAnimation();
        NotifyIntroComplete();
    }

    // 元の位置からの始点と終点の間を往復させる
    private void StartIdleAnimation()
    {
        const Vector3 bobStartPosition = basePosition_ + bobStart;
        const Vector3 bobEndPosition = basePosition_ + bobEnd;
        transform_.position = bobStartPosition;

        Tween::To(bobStartPosition, bobEndPosition, bobDuration, TweenVector3Setter(this.SetPosition))
            .SetEase(EaseType::EaseInOutSine)
            .SetLoops(-1, TweenLoop::Yoyo)
            .SetLink(owner)
            .SetUpdateType(TweenUpdate::Unscaled)
            .SetId("title_trolley_bob");
    }

    private void SetPositionY(float value)
    {
        Vector3 position = transform_.position;
        position.y = value;
        transform_.position = position;
    }

    private void SetPosition(const Vector3 &in value)
    {
        transform_.position = value;
    }
}

// from と同じシーンから名前でオブジェクトを探し、付いている TitleTrolley を返す（無ければ警告して null）
TitleTrolley@ FindTitleTrolley(GameObject@ from, const string &in name)
{
    TitleTrolley@ trolley;
    GameObject@ object = from.FindObject(name);
    if (object is null || !object.GetComponent(@trolley)) {
        Warn(from.name + ": " + name + " の TitleTrolley が見つかりません");
    }
    return trolley;
}
