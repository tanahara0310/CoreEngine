// 1 軸ぶんの揺れの進み具合。間隔が来たら揺れ始め、揺れている間は包絡をかけた正弦波のずれを返す
class ResultShakeAxis
{
    float intervalTimer = 0.0f;
    float burstTimer = -1.0f;
    float phase = 0.0f;

    float Advance(float deltaTime, float amplitude, float interval, float duration, float frequency)
    {
        if (amplitude <= 0.0f || interval <= 0.0f || duration <= 0.0f || frequency <= 0.0f) {
            burstTimer = -1.0f;
            return 0.0f;
        }

        intervalTimer += deltaTime;
        if (burstTimer < 0.0f && intervalTimer >= interval) {
            intervalTimer = fmod(intervalTimer, interval);
            burstTimer = 0.0f;
        }

        if (burstTimer < 0.0f) {
            return 0.0f;
        }

        burstTimer += deltaTime;
        const float progress = Clamp(burstTimer / duration, 0.0f, 1.0f);
        const float envelope = sin(progress * 3.14159265358979323846f);
        const float offset = sin(phase + burstTimer * frequency) * amplitude * envelope;
        if (burstTimer >= duration) {
            burstTimer = -1.0f;
            return 0.0f;
        }
        return offset;
    }
}

// リザルトのサルを X 軸と Z 軸で別々に、決まった間隔で小刻みに揺らす。何体目かで揺れ始めと位相をずらす
[DisplayName("リザルトのサルの揺れ")]
class ResultMonkeyShake : ScriptComponent
{
    [DisplayName("何体目のサルか"), Range(0, 1000)]
    int monkeyIndex = 0;

    [DisplayName("X 軸の揺れ幅（m）"), Range(0, 1)]
    float xAmplitude = 0.06f;

    [DisplayName("Z 軸の揺れ幅（m）"), Range(0, 1)]
    float zAmplitude = 0.05f;

    [DisplayName("X 軸の揺れの間隔（秒）"), Range(0.1, 10)]
    float xInterval = 1.15f;

    [DisplayName("Z 軸の揺れの間隔（秒）"), Range(0.1, 10)]
    float zInterval = 1.65f;

    [DisplayName("X 軸の揺れの時間（秒）"), Range(0.05, 2)]
    float xDuration = 0.28f;

    [DisplayName("Z 軸の揺れの時間（秒）"), Range(0.05, 2)]
    float zDuration = 0.34f;

    [DisplayName("X 軸の揺れの周波数"), Range(1, 100)]
    float xFrequency = 34.0f;

    [DisplayName("Z 軸の揺れの周波数"), Range(1, 100)]
    float zFrequency = 27.0f;

    private Transform@ transform_;
    private Vector3 baseTranslation_;
    private ResultShakeAxis xAxis_;
    private ResultShakeAxis zAxis_;

    void Start() override
    {
        @transform_ = owner.transform;
        if (!transform_.exists) {
            Warn(owner.name + " の ResultMonkeyShake: Transform が無いので揺らしません");
            @transform_ = null;
            return;
        }

        baseTranslation_ = transform_.position;

        const float index = float(monkeyIndex);
        xAxis_.intervalTimer = fmod(index * 0.37f, Max(0.1f, xInterval));
        zAxis_.intervalTimer = fmod(0.42f + index * 0.53f, Max(0.1f, zInterval));
        xAxis_.phase = index * 1.73f;
        zAxis_.phase = 0.65f + index * 2.11f;
    }

    void Update() override
    {
        if (transform_ is null) {
            return;
        }

        const float deltaTime = Max(0.0f, Time::UnscaledDeltaTime());
        Vector3 translation = baseTranslation_;
        translation.x += xAxis_.Advance(deltaTime, Max(0.0f, xAmplitude), xInterval, xDuration, xFrequency);
        translation.z += zAxis_.Advance(deltaTime, Max(0.0f, zAmplitude), zInterval, zDuration, zFrequency);

        transform_.position = translation;
        transform_.UpdateMatrix();
    }
}
