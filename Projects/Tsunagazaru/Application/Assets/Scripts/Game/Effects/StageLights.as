using namespace CoreEngine;

// 夕方から夜にかけて、レールのカーソルとトロッコに追従する点光源を灯す。
// 太陽の高さで点灯の割合（0〜1・角を丸める）を出し、光の強さに掛ける。昼は消しておく
[DisplayName("夜の灯り")]
class StageLights : ScriptComponent
{
    [DisplayName("灯り始める太陽の高さ（度）"), Range(-20, 30)]
    float onElevationDeg = 5.0f;

    [DisplayName("全灯になる太陽の高さ（度）"), Range(-30, 20)]
    float fullElevationDeg = -6.0f;

    [DisplayName("太陽のオブジェクトの名前")]
    string sunName = "Sun";

    [DisplayName("カーソルに灯りを付ける")]
    bool builderEnabled = true;

    [DisplayName("カーソルの灯りの位置（相対・m）")]
    Vector3 builderOffset = Vector3(0.0f, 2.0f, 0.0f);

    [DisplayName("カーソルの灯りの色")]
    Vector3 builderColor = Vector3(1.0f, 0.86f, 0.62f);

    [DisplayName("カーソルの灯りの光度（cd）"), Range(0, 100000)]
    float builderIntensity = 2500.0f;

    [DisplayName("カーソルの灯りの届く距離（m）"), Range(0.5, 100)]
    float builderRange = 8.0f;

    [DisplayName("トロッコに灯りを付ける")]
    bool trainEnabled = true;

    [DisplayName("トロッコの灯りの位置（相対・m）")]
    Vector3 trainOffset = Vector3(0.0f, 1.6f, 0.0f);

    [DisplayName("トロッコの灯りの色")]
    Vector3 trainColor = Vector3(1.0f, 0.95f, 0.85f);

    [DisplayName("トロッコの灯りの光度（cd）"), Range(0, 100000)]
    float trainIntensity = 2000.0f;

    [DisplayName("トロッコの灯りの届く距離（m）"), Range(0.5, 100)]
    float trainRange = 3.5f;

    private Light@ sun_;
    private GameObject@ builder_;
    private GameObject@ train_;
    private GameObject@ builderLight_;
    private GameObject@ trainLight_;

    void Start() override
    {
        GameObject@ sunObject = owner.FindObject(sunName);
        if (sunObject !is null) {
            @sun_ = sunObject.light;
        }
        @builder_ = owner.FindObject("RailBuilder");
        @train_ = owner.FindObject("Train");
        @builderLight_ = owner.FindObject("BuilderLight");
        @trainLight_ = owner.FindObject("TrainLight");
    }

    void LateUpdate() override
    {
        const float lit = ComputeLitRatio();
        Follow(builderLight_, builder_, builderEnabled, builderOffset, builderColor, builderIntensity, builderRange, lit);
        Follow(trainLight_, train_, trainEnabled, trainOffset, trainColor, trainIntensity, trainRange, lit);
    }

    private void Follow(GameObject@ lightObject, GameObject@ target, bool enabled, const Vector3 &in offset,
        const Vector3 &in color, float intensity, float range, float lit)
    {
        if (lightObject is null) {
            return;
        }
        Light@ light = lightObject.light;
        const bool on = enabled && target !is null && lit > 0.001f;
        if (light.enabled != on) {
            light.enabled = on;
        }
        if (!on) {
            return;
        }
        lightObject.transform.position = target.transform.worldPosition + offset;
        light.color = Vector4(color.x, color.y, color.z, 1.0f);
        light.intensity = intensity * lit;
        light.range = range;
    }

    // 0 = 昼（消灯）／ 1 = 夜（全灯）
    private float ComputeLitRatio() const
    {
        float elevation = 90.0f;
        if (sun_ !is null && sun_.exists) {
            const Vector3 direction = Normalize(sun_.direction);
            elevation = asin(Clamp(-direction.y, -1.0f, 1.0f)) * (180.0f / kPi);
        }
        float t = 0.0f;
        if (onElevationDeg > fullElevationDeg) {
            t = Clamp((onElevationDeg - elevation) / (onElevationDeg - fullElevationDeg), 0.0f, 1.0f);
        } else {
            t = elevation <= onElevationDeg ? 1.0f : 0.0f;
        }
        return t * t * (3.0f - 2.0f * t);
    }
}
