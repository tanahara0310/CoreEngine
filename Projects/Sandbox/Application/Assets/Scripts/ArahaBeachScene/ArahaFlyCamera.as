using namespace CoreEngine;

// 広い範囲を見渡すために自由に飛ぶカメラ（付けたオブジェクトの Transform を動かす）
// 前後左右は水平に進み、上下は上昇・下降の操作で動く。地面より下へは潜らない
[DisplayName("アラハビーチ：広域カメラ")]
class ArahaFlyCamera : ScriptComponent
{
    [ObjectRef] [Tooltip("地面の高さを引くプレイヤー（BeachPlayer を付けたオブジェクト）")]
    GameObject@ player;

    [Range(1.0f, 500.0f)] [Tooltip("進む速さ m/s（ホイールでも変えられる）")]
    float moveSpeed = 30.0f;

    [Range(1.0f, 10.0f)] [Tooltip("速くする操作を押している間に掛ける倍率")]
    float boostMultiplier = 4.0f;

    [Range(1.0f, 300.0f)] [Tooltip("上昇・下降の速さ m/s")]
    float climbSpeed = 20.0f;

    [Range(0.2f, 4.0f)] [Tooltip("向きを変える速さの倍率")]
    float lookSensitivity = 1.0f;

    [Range(20.0f, 100.0f)] [Tooltip("視野角（度）")]
    float fieldOfView = 60.0f;

    [Range(0.0f, 1.0f)] [Tooltip("止まるまでのなめらかさ s（0 で入力どおりに止まる）")]
    float smoothing = 0.2f;

    [Range(0.5f, 50.0f)] [Tooltip("地面と水面から離しておく高さ m")]
    float minClearance = 2.0f;

    [Range(100.0f, 5000.0f)] [Tooltip("上がれる高さ m")]
    float maxHeight = 2000.0f;

    [Range(0.5f, 6.0f)] [Tooltip("矢印キー・右スティックで向きを変える速さ rad/s")]
    float turnSpeed = 1.6f;

    [Range(0.0005f, 0.02f)] [Tooltip("右ドラッグで向きを変える速さ rad/px")]
    float mouseSensitivity = 0.004f;

    private BeachPlayer@ ground_;
    private bool controlled_ = false;
    private float yaw_ = 0.0f;
    private float pitch_ = 0.0f;
    private Vector3 velocity_ = Vector3(0.0f, 0.0f, 0.0f);
    private float followFov_ = 55.0f;

    void Start()
    {
        Connect();
    }

    void Update()
    {
        if (!controlled_) {
            return;
        }
        const float dt = Time::DeltaTime();
        if (dt <= 0.0f) {
            return;
        }
        Look(dt);
        Fly(dt);
        Camera@ camera = owner.camera;
        if (camera !is null && camera.exists) {
            camera.fov = fieldOfView;
        }
    }

    void OnScriptReloaded()
    {
        Connect();
        TakePose();
    }

    // 操作を受け持つか。受け持つときは今の構図から始め、離すときは視野角を戻す
    void SetControlled(bool on)
    {
        if (on == controlled_) {
            return;
        }
        controlled_ = on;
        Camera@ camera = owner.camera;
        if (on) {
            TakePose();
            velocity_ = Vector3(0.0f, 0.0f, 0.0f);
            if (camera !is null && camera.exists) {
                followFov_ = camera.fov;
            }
        } else if (camera !is null && camera.exists) {
            camera.fov = followFov_;
        }
    }

    bool IsControlled() const
    {
        return controlled_;
    }

    // 地面（無ければ水面）からの高さ m
    float HeightAboveGround()
    {
        const Vector3 position = owner.transform.position;
        return position.y - FloorAt(position.x, position.z);
    }

    // 向いている先の水平な向き rad
    float GetYaw() const
    {
        return yaw_;
    }

    private void Connect()
    {
        @ground_ = null;
        if (player !is null) {
            player.GetComponent(@ground_);
        }
        if (ground_ is null) {
            Warn("ArahaFlyCamera: player に BeachPlayer が無いので、地面より下へ潜ります");
        }
    }

    private void TakePose()
    {
        const Vector3 rotation = owner.transform.rotation;
        pitch_ = rotation.x;
        yaw_ = rotation.y;
    }

    // 視点の操作（矢印キー・右スティック）と右ドラッグで向きを変え、ホイールで速さを変える
    private void Look(float dt)
    {
        const Vector2 look = Input::GetAxis2D(
            InputAction::LookLeft, InputAction::LookRight,
            InputAction::LookDown, InputAction::LookUp);
        yaw_ += look.x * turnSpeed * lookSensitivity * dt;
        pitch_ -= look.y * turnSpeed * lookSensitivity * dt;

        if (Input::IsPointerOverGame()) {
            if (Input::IsMouseButtonPressed(MouseButton::Right)) {
                const Vector2 delta = Input::GetMouseDelta();
                yaw_ += delta.x * mouseSensitivity * lookSensitivity;
                pitch_ += delta.y * mouseSensitivity * lookSensitivity;
            }
            const float wheel = Input::GetWheelDelta();
            if (wheel != 0.0f) {
                moveSpeed = Clamp(moveSpeed * pow(1.15f, wheel), 1.0f, 500.0f);
            }
        }
        pitch_ = Clamp(pitch_, -1.45f, 1.45f);
        owner.transform.rotation = Vector3(pitch_, yaw_, 0.0f);
    }

    private void Fly(float dt)
    {
        const Vector2 stick = Input::GetAxis2D(
            InputAction::MoveLeft, InputAction::MoveRight,
            InputAction::MoveBack, InputAction::MoveForward);
        const float rise = Input::GetAxis(InputAction::MoveDown, InputAction::MoveUp);
        const float boost = Input::IsActionPressed(InputAction::Sprint) ? boostMultiplier : 1.0f;

        const Vector3 forward = Vector3(sin(yaw_), 0.0f, cos(yaw_));
        const Vector3 right = Vector3(cos(yaw_), 0.0f, -sin(yaw_));
        const Vector3 target = (forward * stick.y + right * stick.x) * (moveSpeed * boost)
                             + Vector3(0.0f, rise * climbSpeed * boost, 0.0f);
        if (smoothing <= 0.001f) {
            velocity_ = target;
        } else {
            velocity_ = Lerp(velocity_, target, 1.0f - exp(-dt / smoothing));
        }

        Vector3 position = owner.transform.position + velocity_ * dt;
        const float lowest = FloorAt(position.x, position.z) + minClearance;
        if (position.y < lowest) {
            position.y = lowest;
            velocity_.y = Max(velocity_.y, 0.0f);
        }
        if (position.y > maxHeight) {
            position.y = maxHeight;
            velocity_.y = Min(velocity_.y, 0.0f);
        }
        owner.transform.position = position;
    }

    // 地面の高さ（表の外と水の上は水面の 0）
    private float FloorAt(float x, float z)
    {
        float ground = 0.0f;
        if (ground_ !is null && ground_.GroundHeight(x, z, ground)) {
            return Max(ground, 0.0f);
        }
        return 0.0f;
    }
}
