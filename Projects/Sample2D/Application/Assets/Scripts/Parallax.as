using namespace CoreEngine;

// カメラより遅く動き、遠くにあるように見せる
[DisplayName("2D：遠景")]
class Parallax : ScriptComponent
{
    [ObjectRef] [Tooltip("基準にするカメラのオブジェクト")]
    GameObject@ cameraObject;

    [Range(0.0f, 1.0f)] [Tooltip("1 でカメラと一緒に動く（無限に遠い）、0 で動かない")]
    float follow = 0.5f;

    private float baseX_ = 0.0f;

    void Start()
    {
        baseX_ = owner.transform.position.x;
    }

    void LateUpdate()
    {
        if (cameraObject is null) {
            return;
        }
        Vector3 position = owner.transform.position;
        position.x = baseX_ + cameraObject.transform.position.x * follow;
        owner.transform.position = position;
    }
}
