// ステージより下を雲で埋める高さの霧の、夜の明るさ。
// 霧の値そのものはシーンの環境設定（r.Fog.*）に置き、ここでは太陽の高さから夜の度合いを出して、
// 霧の色の明るさを EV で落とす（夜の自動露出で雲が白く光らないように）。シーンを出るときに元へ戻す
[DisplayName("雲の霧の夜の明るさ")]
class SkyFog : ScriptComponent
{
    [DisplayName("昼の雲の明るさ"), Range(0, 20)]
    float brightness = 0.10f;

    [DisplayName("夜に落とす段数（EV）"), Range(-16, 0)]
    float nightBrightnessEV = -6.4f;

    [DisplayName("暗くし始める太陽の高さ（度）"), Range(-20, 30)]
    float nightStartElevationDeg = 5.0f;

    [DisplayName("暗くしきる太陽の高さ（度）"), Range(-30, 20)]
    float nightFullElevationDeg = -6.0f;

    [DisplayName("太陽のオブジェクトの名前")]
    string sunName = "Sun";

    private Light@ sun_;
    private float savedIntensity_ = 0.0f;
    private float applied_ = -1.0f;

    void Start() override
    {
        savedIntensity_ = CVar::GetFloat("r.Fog.ColorIntensity", brightness);
        GameObject@ sunObject = owner.FindObject(sunName);
        if (sunObject !is null) {
            @sun_ = sunObject.light;
        }
        Apply();
    }

    void Update() override
    {
        Apply();
    }

    void OnDestroy() override
    {
        CVar::SetFloat("r.Fog.ColorIntensity", savedIntensity_);
    }

    private void Apply()
    {
        const float intensity = brightness * exp2(nightBrightnessEV * ComputeNightFactor());
        if (abs(intensity - applied_) > 1.0e-5f) {
            CVar::SetFloat("r.Fog.ColorIntensity", intensity);
            applied_ = intensity;
        }
    }

    // 0 = 昼 ／ 1 = 夜。角を丸めて切り替わりを滑らかにする
    private float ComputeNightFactor() const
    {
        const float elevation = GetSunElevationDeg();
        float t = 0.0f;
        if (nightStartElevationDeg > nightFullElevationDeg) {
            t = Clamp((nightStartElevationDeg - elevation) / (nightStartElevationDeg - nightFullElevationDeg), 0.0f, 1.0f);
        } else {
            t = (elevation <= nightStartElevationDeg) ? 1.0f : 0.0f;
        }
        return t * t * (3.0f - 2.0f * t);
    }

    // 太陽の高度。光の向きは「太陽 → 地面」なので、太陽を見る向きの Y が sin(高度)
    private float GetSunElevationDeg() const
    {
        if (sun_ is null || !sun_.exists) {
            return 90.0f;
        }
        const Vector3 direction = Normalize(sun_.direction);
        return asin(Clamp(-direction.y, -1.0f, 1.0f)) * (180.0f / kPi);
    }
}
