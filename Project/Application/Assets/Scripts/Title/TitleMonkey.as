// サルをトロッコに乗せておき、トロッコが着いた後にトロッコの中から飛び出させる
[DisplayName("サルの演出")]
class TitleMonkey : TitleIntro
{
    [DisplayName("飛び出す時間（秒）"), Range(0.05, 3)]
    float introDuration = 0.5f;

    [DisplayName("トロッコの中へ沈めておく深さ（m）"), Range(0, 10)]
    float introOffset = 1.5f;

    [DisplayName("乗るトロッコのオブジェクトの名前")]
    string trolleyName = "trolley";

    [DisplayName("ロゴのオブジェクトの名前")]
    string logoName = "title";

    private Transform@ transform_;
    private Vector3 basePosition_;

    // トロッコの Transform を親にする（位置はトロッコから見た位置になる）
    void Awake() override
    {
        GameObject@ trolley = owner.FindObject(trolleyName);
        if (trolley is null) {
            Warn(owner.name + " の TitleMonkey: トロッコ " + trolleyName + " が見つからないので、親を付けません");
            return;
        }
        owner.transform.SetParent(trolley.transform);
    }

    void Start() override
    {
        @transform_ = owner.transform;
        if (!transform_.exists) {
            Warn(owner.name + " の TitleMonkey: Transform が無いので、演出しません");
            return;
        }

        TitleLogo@ logo = FindTitleLogo(owner, logoName);
        TitleTrolley@ trolley = FindTitleTrolley(owner, trolleyName);
        const float logoDuration = (logo !is null) ? logo.introDuration : 0.0f;
        const float trolleyDelay = (trolley !is null) ? trolley.introDelay : 0.0f;
        const float trolleyDuration = (trolley !is null) ? trolley.introDuration : 0.0f;
        const float delay = logoDuration + trolleyDelay + trolleyDuration;

        basePosition_ = transform_.position;
        const float startY = basePosition_.y - introOffset;
        transform_.position = Vector3(basePosition_.x, startY, basePosition_.z);

        Tween::To(startY, basePosition_.y, introDuration, TweenFloatSetter(this.SetPositionY))
            .SetEase(EaseType::EaseOutBack)
            .SetDelay(delay)
            .SetLink(owner)
            .SetUpdateType(TweenUpdate::Unscaled)
            .SetId("title_monkey_intro")
            .OnComplete(TweenCallback(this.OnIntroFinished));
    }

    private void OnIntroFinished()
    {
        NotifyIntroComplete();
    }

    private void SetPositionY(float value)
    {
        Vector3 position = transform_.position;
        position.y = value;
        transform_.position = position;
    }
}
