using namespace CoreEngine;

// メッシュの形とトリガーの知らせの試験
// 持ち主を low と high の高さの間で上下させ、重なり始め・離れたときにコンソールへ出す
[DisplayName("試験：メッシュとトリガー")]
class MeshTriggerProbe : ScriptComponent
{
    [Tooltip("いちばん低い高さ（m）")]
    float low = -0.1f;

    [Tooltip("いちばん高い高さ（m）")]
    float high = 1.2f;

    [Tooltip("1 往復の秒数")]
    float period = 3.0f;

    private float time_ = 0.0f;
    private int stayFrames_ = 0;

    void Update()
    {
        time_ += Time::DeltaTime();
        const float phase = 0.5f + 0.5f * cos(time_ * 6.2831853f / period);
        Vector3 position = owner.transform.position;
        position.y = low + (high - low) * phase;
        owner.transform.position = position;
    }

    void OnTriggerEnter(Collision@ other)
    {
        stayFrames_ = 0;
        Log("MeshTriggerProbe（" + owner.name + "）: " + other.gameObject.name + " に入った・高さ "
            + formatFloat(owner.transform.position.y, "", 0, 3));
    }

    void OnTriggerStay(Collision@ other)
    {
        ++stayFrames_;
    }

    void OnTriggerExit(Collision@ other)
    {
        Log("MeshTriggerProbe（" + owner.name + "）: " + other.gameObject.name + " から出た・高さ "
            + formatFloat(owner.transform.position.y, "", 0, 3) + "・重なっていたフレーム " + stayFrames_);
    }
}
