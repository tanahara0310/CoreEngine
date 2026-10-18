using namespace CoreEngine;

// 横だけ相手を追いかける（ステージの端より外は映さない）
[DisplayName("2D：カメラの追従")]
class CameraFollow2D : ScriptComponent
{
    [ObjectRef] [Tooltip("追いかける相手")]
    GameObject@ target;

    [Range(0.0f, 30.0f)] [Tooltip("大きいほど早く追いつく")]
    float smoothing = 6.0f;

    [Tooltip("カメラの x の下限")]
    float minX = -1.0f;

    [Tooltip("カメラの x の上限")]
    float maxX = 29.5f;

    void LateUpdate()
    {
        if (target is null) {
            return;
        }
        Vector3 position = owner.transform.position;
        const float goal = Clamp(target.transform.position.x, minX, maxX);
        position.x = Lerp(position.x, goal, Saturate(smoothing * Time::DeltaTime()));
        owner.transform.position = position;
    }
}
