using namespace CoreEngine;

// 波紋の試験：決まった間隔で水面の 1 点に石を落とし、円を描いて進む点の後ろに波紋を引く
[DisplayName("試験：波紋")]
class RippleProbe : ScriptComponent
{
    [Tooltip("石を落とす間隔 [s]（0 で落とさない）")]
    float dropInterval = 2.0f;

    [Tooltip("最初の石を落とすまでの時間 [s]（負なら落とす間隔と同じ）")]
    float firstDropDelay = -1.0f;

    [Tooltip("石が水面を押し下げるくぼみの半径 [m]")]
    float dropRadius = 0.3f;

    [Tooltip("石が水面を押し下げるくぼみの深さ [m]")]
    float dropDepth = 0.05f;

    [Tooltip("円を描いて進む点の速さ [m/s]（0 で進めない）")]
    float moverSpeed = 2.0f;

    [Tooltip("進む点が描く円の半径 [m]")]
    float moverCircleRadius = 6.0f;

    [Tooltip("進む点が水面を押すくぼみの半径 [m]")]
    float moverRadius = 0.25f;

    [Tooltip("進む点が 1 秒あたりに押し下げる深さ [m/s]")]
    float moverDepthPerSecond = 0.1f;

    private float dropTimer_ = 0.0f;
    private bool firstDropDone_ = false;
    private float moverAngle_ = 0.0f;

    void Update()
    {
        const float dt = Time::DeltaTime();
        const Vector3 center = owner.transform.position;

        if (dropInterval > 0.0f) {
            dropTimer_ += dt;
            const float wait = (!firstDropDone_ && firstDropDelay >= 0.0f) ? firstDropDelay : dropInterval;
            if (dropTimer_ >= wait) {
                dropTimer_ -= wait;
                firstDropDone_ = true;
                Water::AddRipple(center, dropRadius, dropDepth);
            }
        }

        if (moverSpeed > 0.0f && moverCircleRadius > 0.0f) {
            moverAngle_ += moverSpeed / moverCircleRadius * dt;
            const Vector3 offset(cos(moverAngle_) * moverCircleRadius, 0.0f, sin(moverAngle_) * moverCircleRadius);
            Water::AddRipple(center + offset, moverRadius, moverDepthPerSecond * dt);
        }
    }
}
