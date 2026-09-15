// ロゴが着地する進捗（EaseOutBounce の区切り）と、そのときの揺れと音の強さ
const array<float> kTitleLogoBounceProgress = { 1.0f / 2.75f, 2.0f / 2.75f, 2.5f / 2.75f, 1.0f };
const array<float> kTitleLogoBounceIntensity = { 1.0f, 0.55f, 0.3f, 0.18f };

// タイトルロゴを上から落として弾ませ、着地のたびにカメラを揺らして音を鳴らす。
// 着地した後は上下に浮かせて左右に揺らす
[DisplayName("タイトルロゴの演出")]
class TitleLogo : TitleIntro
{
    [DisplayName("落ちて着地するまでの時間（秒）"), Range(0.05, 5)]
    float introDuration = 1.05f;

    [DisplayName("落ち始めの大きさ（倍率）"), Range(0.1, 1)]
    float introScale = 0.82f;

    [DisplayName("落とし始める高さ（m）"), Range(0, 20)]
    float dropHeight = 3.5f;

    [DisplayName("浮かせる高さ（m）"), Range(0, 2)]
    float bobHeight = 0.12f;

    [DisplayName("浮かせる片道の時間（秒）"), Range(0.1, 10)]
    float bobDuration = 1.6f;

    [DisplayName("左右に揺らす角度（ラジアン）"), Range(0, 1)]
    float rotationAmplitude = 0.035f;

    [DisplayName("着地で揺らすカメラの強さ"), Range(0, 2)]
    float shakeStrength = 1.0f;

    [DisplayName("着地の音")]
    string boundSePath = "Sounds/SE/title_bound.mp3";

    private Transform@ transform_;
    private Vector3 basePosition_;
    private Vector3 baseRotation_;
    private Vector3 baseScale_ = Vector3(1.0f, 1.0f, 1.0f);
    private uint nextBounceIndex_ = 0;

    void Start() override
    {
        @transform_ = owner.transform;
        if (!transform_.exists) {
            Warn(owner.name + " の TitleLogo: Transform が無いので、演出しません");
            return;
        }

        basePosition_ = transform_.position;
        baseRotation_ = transform_.rotation;
        baseScale_ = transform_.scale;
        nextBounceIndex_ = 0;

        const float startY = basePosition_.y + dropHeight;
        transform_.position = Vector3(basePosition_.x, startY, basePosition_.z);
        transform_.scale = baseScale_ * introScale;
        transform_.rotation = Vector3(baseRotation_.x, baseRotation_.y - rotationAmplitude, baseRotation_.z);

        // シーケンスを作ってから、中身の Tween を 1 本ずつ作って足す
        TweenSequence intro = Tween::Sequence();
        intro.Append(Tween::To(startY, basePosition_.y, introDuration, TweenFloatSetter(this.SetPositionY))
            .SetEase(EaseType::EaseOutBounce)
            .OnUpdate(TweenProgressCallback(this.OnIntroProgress)));
        intro.Join(Tween::ScaleTo(owner, baseScale_, introDuration).SetEase(EaseType::EaseOutBack));
        intro.Join(Tween::RotateTo(owner, baseRotation_, introDuration).SetEase(EaseType::EaseOutCubic));
        intro.AppendCallback(TweenCallback(this.OnIntroFinished));
        intro.SetLink(owner)
            .SetUpdateType(TweenUpdate::Unscaled)
            .SetId("title_logo_intro");
    }

    // 落下の進捗が着地の区切りを越えるたびに、揺れと音を 1 回ずつ出す
    private void OnIntroProgress(float progress)
    {
        while (nextBounceIndex_ < kTitleLogoBounceProgress.length()
            && progress >= kTitleLogoBounceProgress[nextBounceIndex_]) {
            PlayBounceShake(kTitleLogoBounceIntensity[nextBounceIndex_]);
            ++nextBounceIndex_;
        }
    }

    // 着地の揺れ（Landing に強さを掛けたもの）と音を出す。音量は着地の強さ
    private void PlayBounceShake(float intensity)
    {
        CameraShakeParams shake = CameraShakePresets::Landing();
        const float totalIntensity = intensity * shakeStrength;
        shake.positionAmplitude *= totalIntensity;
        shake.rotationAmplitude *= totalIntensity;
        CameraShake::Play(shake);

        PlayParams sound;
        sound.bus = AudioBus::SE;
        sound.volume = intensity;
        Audio::PlayOneShot(boundSePath, sound);
    }

    private void OnIntroFinished()
    {
        StartIdleAnimation();
        NotifyIntroComplete();
    }

    // 位置の高さと回転の y を別々の Tween で往復させる
    private void StartIdleAnimation()
    {
        Tween::To(basePosition_.y, basePosition_.y + bobHeight, bobDuration, TweenFloatSetter(this.SetPositionY))
            .SetEase(EaseType::EaseInOutSine)
            .SetLoops(-1, TweenLoop::Yoyo)
            .SetLink(owner)
            .SetUpdateType(TweenUpdate::Unscaled)
            .SetId("title_logo_bob");

        Tween::To(baseRotation_.y, baseRotation_.y + rotationAmplitude, bobDuration * 1.25f, TweenFloatSetter(this.SetRotationY))
            .SetEase(EaseType::EaseInOutSine)
            .SetLoops(-1, TweenLoop::Yoyo)
            .SetLink(owner)
            .SetUpdateType(TweenUpdate::Unscaled)
            .SetId("title_logo_sway");
    }

    private void SetPositionY(float value)
    {
        Vector3 position = transform_.position;
        position.y = value;
        transform_.position = position;
    }

    private void SetRotationY(float value)
    {
        Vector3 rotation = transform_.rotation;
        rotation.y = value;
        transform_.rotation = rotation;
    }
}

// from と同じシーンから名前でオブジェクトを探し、付いている TitleLogo を返す（無ければ警告して null）
TitleLogo@ FindTitleLogo(GameObject@ from, const string &in name)
{
    TitleLogo@ logo;
    GameObject@ object = from.FindObject(name);
    if (object is null || !object.GetComponent(@logo)) {
        Warn(from.name + ": " + name + " の TitleLogo が見つかりません");
    }
    return logo;
}
