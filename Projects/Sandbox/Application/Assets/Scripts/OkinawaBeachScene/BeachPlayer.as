// 沖縄の砂浜を歩き回るプレイヤー
// 足元は高さの表に合わせ、物との当たりはキャラクタコントローラで解く。カメラは後ろから追う
[DisplayName("沖縄の砂浜：プレイヤー")]
class BeachPlayer : ScriptComponent
{
    [ObjectRef] [Tooltip("見た目の人型（Idle / Walk / Run のクリップを持つアニメーター付きの子）")]
    GameObject@ model;

    [ObjectRef] [Tooltip("後ろから追うカメラ")]
    GameObject@ cameraObject;

    [Asset("Json")] [Tooltip("歩ける地面の高さの表（Tools/OkinawaBeach/bake_player_walk.py が書き出す）")]
    string groundData = "Application/Assets/Data/OkinawaBeach/BeachGround.json";

    [Range(0.5f, 4.0f)] [Tooltip("歩く速さ m/s（Walk のクリップの歩幅に合わせてある）")]
    float walkSpeed = 1.4f;

    [Range(1.0f, 8.0f)] [Tooltip("走る速さ m/s（Run のクリップの歩幅に合わせてある）")]
    float runSpeed = 3.5f;

    [Range(1.0f, 40.0f)] [Tooltip("速さが変わるときの加速度 m/s²")]
    float acceleration = 14.0f;

    [Range(1.0f, 30.0f)] [Tooltip("進む向きへ体を回す速さ rad/s")]
    float turnSpeed = 12.0f;

    [Range(0.0f, 1.5f)] [Tooltip("入れる水の深さ m（水面は y=0）")]
    float maxWaterDepth = 0.45f;

    [Range(0.1f, 2.0f)] [Tooltip("1m 進むあいだに上れる高さ m")]
    float maxClimb = 0.8f;

    [Range(0.0f, 1.0f)] [Tooltip("アニメーションを切り替えるときに混ぜる時間 s")]
    float blendTime = 0.2f;

    [Range(0.5f, 2.0f)] [Tooltip("カプセルの中心から足元までの高さ m")]
    float footOffset = 0.9f;

    [Range(1.5f, 15.0f)] [Tooltip("カメラの距離 m（ホイールで変えられる）")]
    float cameraDistance = 5.0f;

    [Range(0.0f, 2.0f)] [Tooltip("カメラが見る高さ（カプセルの中心から）m")]
    float lookHeight = 0.65f;

    [Range(0.0005f, 0.02f)] [Tooltip("右ドラッグでカメラを回す速さ rad/px")]
    float mouseSensitivity = 0.005f;

    private CharacterController@ controller_;
    private Animator@ animator_;

    private array<int>@ heights_;
    private float originX_ = 0.0f;
    private float originZ_ = 0.0f;
    private float cell_ = 0.5f;
    private int countX_ = 0;
    private int countZ_ = 0;
    private int noGround_ = -9999;

    private Vector3 moveDirection_ = Vector3(0.0f, 0.0f, 1.0f);
    private float speed_ = 0.0f;
    private float actualSpeed_ = 0.0f;
    private float yaw_ = 0.0f;
    private float cameraYaw_ = 0.0f;
    private float cameraPitch_ = 0.3f;
    private string clip_ = "Idle";

    void Start()
    {
        Connect();
        LoadGround();
        SnapToGround();
    }

    void Update()
    {
        const float dt = Time::DeltaTime();
        if (dt <= 0.0f) {
            return;
        }
        TurnCamera(dt);
        Walk(dt);
        UpdateClip();
        PlaceCamera();
    }

    void OnScriptReloaded()
    {
        Connect();
        LoadGround();
    }

    // コントローラとアニメーターを引き、向きと再生中のクリップを今の状態から取る
    private void Connect()
    {
        owner.GetComponent(@controller_);
        if (controller_ !is null && controller_.exists) {
            // 上下は高さの表で決めるので、コントローラには水平の当たりだけを解かせる
            controller_.useGravity = false;
        } else {
            Warn("BeachPlayer: CharacterController が無いので物に当たりません");
        }

        @animator_ = null;
        if (model !is null) {
            @animator_ = model.animator;
        }
        if (animator_ is null || !animator_.exists) {
            Warn("BeachPlayer: model にアニメーターが無いので、歩き・走りのアニメーションを切り替えません");
        } else if (animator_.GetCurrentClipName() != "") {
            clip_ = animator_.GetCurrentClipName();
        }
        if (cameraObject is null) {
            Warn("BeachPlayer: cameraObject が無いのでカメラが追いません");
        }

        yaw_ = owner.transform.rotation.y;
        cameraYaw_ = yaw_;
    }

    // ===== 地面 =====

    private void LoadGround()
    {
        @heights_ = null;
        DataFile@ file = DataFile(groundData);
        if (!file.exists) {
            Warn("BeachPlayer: 地面の高さの表 " + groundData + " がありません");
            return;
        }
        originX_ = file.GetFloat("originX");
        originZ_ = file.GetFloat("originZ");
        cell_ = file.GetFloat("cell", 0.5f);
        countX_ = file.GetInt("countX");
        countZ_ = file.GetInt("countZ");
        noGround_ = file.GetInt("noGround", -9999);
        array<int>@ heights = file.GetIntArray("heights");
        if (heights is null || int(heights.length()) != countX_ * countZ_ || cell_ <= 0.0f) {
            Warn("BeachPlayer: 地面の高さの表の大きさが合いません");
            return;
        }
        @heights_ = heights;
    }

    // 表を双一次で引く（表の外と地面の無い所は false）
    private bool GroundAt(float x, float z, float &out height)
    {
        height = 0.0f;
        if (heights_ is null) {
            return false;
        }
        const float fx = (x - originX_) / cell_;
        const float fz = (z - originZ_) / cell_;
        const int i = int(floor(fx));
        const int j = int(floor(fz));
        if (i < 0 || j < 0 || i + 1 >= countX_ || j + 1 >= countZ_) {
            return false;
        }
        const int h00 = heights_[j * countX_ + i];
        const int h01 = heights_[j * countX_ + i + 1];
        const int h10 = heights_[(j + 1) * countX_ + i];
        const int h11 = heights_[(j + 1) * countX_ + i + 1];
        if (h00 == noGround_ || h01 == noGround_ || h10 == noGround_ || h11 == noGround_) {
            return false;
        }
        const float tx = fx - float(i);
        const float tz = fz - float(j);
        const float near = Lerp(float(h00), float(h01), tx);
        const float far = Lerp(float(h10), float(h11), tx);
        height = Lerp(near, far, tz) * 0.01f;
        return true;
    }

    // そこへ立てるか（表の中・深すぎない・急すぎない）
    private bool CanStand(float x, float z, float fromHeight, float distance, float &out height)
    {
        if (!GroundAt(x, z, height)) {
            return false;
        }
        if (height < -maxWaterDepth) {
            return false;
        }
        return height - fromHeight <= maxClimb * distance + 0.02f;
    }

    private void SnapToGround()
    {
        const Vector3 position = owner.transform.position;
        float ground;
        if (GroundAt(position.x, position.z, ground)) {
            owner.transform.position = Vector3(position.x, ground + footOffset, position.z);
        }
    }

    // ===== 移動 =====

    private void Walk(float dt)
    {
        // 入力はカメラの向きを前として扱う
        const Vector2 stick = Input::GetAxis2D(
            InputAction::MoveLeft, InputAction::MoveRight,
            InputAction::MoveBack, InputAction::MoveForward);
        const bool pressed = Length(stick) > 0.15f;
        if (pressed) {
            const Vector3 forward = Vector3(sin(cameraYaw_), 0.0f, cos(cameraYaw_));
            const Vector3 right = Vector3(cos(cameraYaw_), 0.0f, -sin(cameraYaw_));
            moveDirection_ = Normalize(forward * stick.y + right * stick.x);
        }

        const float target = pressed ? (Input::IsActionPressed(InputAction::Sprint) ? runSpeed : walkSpeed) : 0.0f;
        speed_ = Approach(speed_, target, acceleration * dt);

        const Vector3 before = owner.transform.position;
        float here;
        if (!GroundAt(before.x, before.z, here)) {
            here = before.y - footOffset;
        }

        // 立てない所へは進まない。斜めに当たったら、立てる向きの成分だけ進む
        const Vector3 step = Restrict(before, moveDirection_ * (speed_ * dt), here);
        if (controller_ !is null && controller_.exists) {
            controller_.Move(step);
        } else {
            owner.transform.position = before + step;
        }

        Vector3 after = owner.transform.position;
        const float moved = Length(Vector3(after.x - before.x, 0.0f, after.z - before.z));
        float ground;
        if (!CanStand(after.x, after.z, here, moved, ground)) {
            after = before;
            ground = here;
        }
        owner.transform.position = Vector3(after.x, ground + footOffset, after.z);

        const float measured = Length(Vector3(after.x - before.x, 0.0f, after.z - before.z)) / dt;
        actualSpeed_ = Lerp(actualSpeed_, measured, Min(1.0f, dt * 12.0f));

        // 進む向きへ体を回す
        if (speed_ > 0.05f) {
            yaw_ = TurnToward(yaw_, atan2(moveDirection_.x, moveDirection_.z), turnSpeed * dt);
            owner.transform.rotation = Vector3(0.0f, yaw_, 0.0f);
        }
    }

    private Vector3 Restrict(const Vector3 &in from, const Vector3 &in step, float here)
    {
        const float length = Length(step);
        if (length <= 0.0f) {
            return step;
        }
        float height;
        if (CanStand(from.x + step.x, from.z + step.z, here, length, height)) {
            return step;
        }
        if (abs(step.x) > abs(step.z)) {
            if (CanStand(from.x + step.x, from.z, here, abs(step.x), height)) { return Vector3(step.x, 0.0f, 0.0f); }
            if (CanStand(from.x, from.z + step.z, here, abs(step.z), height)) { return Vector3(0.0f, 0.0f, step.z); }
        } else {
            if (CanStand(from.x, from.z + step.z, here, abs(step.z), height)) { return Vector3(0.0f, 0.0f, step.z); }
            if (CanStand(from.x + step.x, from.z, here, abs(step.x), height)) { return Vector3(step.x, 0.0f, 0.0f); }
        }
        return Vector3(0.0f, 0.0f, 0.0f);
    }

    // ===== アニメーション =====

    // 実際に進んだ速さでクリップを選ぶ（境目で行き来しないよう、上げるときと下げるときで境を変える）
    private void UpdateClip()
    {
        if (animator_ is null || !animator_.exists) {
            return;
        }
        const float runLine = (walkSpeed + runSpeed) * 0.5f;
        string next = clip_;
        if (clip_ == "Idle") {
            if (actualSpeed_ > runLine) { next = "Run"; }
            else if (actualSpeed_ > 0.35f) { next = "Walk"; }
        } else if (clip_ == "Walk") {
            if (actualSpeed_ < 0.2f) { next = "Idle"; }
            else if (actualSpeed_ > runLine + 0.1f) { next = "Run"; }
        } else {
            if (actualSpeed_ < 0.2f) { next = "Idle"; }
            else if (actualSpeed_ < runLine - 0.1f) { next = "Walk"; }
        }
        // 切り替えは変わったときだけ（同じ名前を渡し続けると毎回頭から再生し直す）
        if (next != clip_ && animator_.SwitchWithBlend(next, blendTime, true)) {
            clip_ = next;
        }
    }

    // ===== カメラ =====

    // 右ドラッグ・右スティック・Q/E で回し、ホイールで寄る
    private void TurnCamera(float dt)
    {
        if (Input::IsPointerOverGame()) {
            if (Input::IsMouseButtonPressed(MouseButton::Right)) {
                const Vector2 delta = Input::GetMouseDelta();
                cameraYaw_ += delta.x * mouseSensitivity;
                cameraPitch_ += delta.y * mouseSensitivity;
            }
            cameraDistance = Clamp(cameraDistance - Input::GetWheelDelta() * 0.5f, 1.5f, 15.0f);
        }
        const Vector2 stick = Input::GetRightStick();
        cameraYaw_ += stick.x * 2.5f * dt;
        cameraPitch_ -= stick.y * 1.5f * dt;
        if (Input::IsKeyPressed(Key::Q)) { cameraYaw_ -= 2.0f * dt; }
        if (Input::IsKeyPressed(Key::E)) { cameraYaw_ += 2.0f * dt; }
        cameraPitch_ = Clamp(cameraPitch_, -0.15f, 1.2f);
    }

    // カメラの構図はオブジェクトの Transform が持つので、そちらへ書く
    private void PlaceCamera()
    {
        if (cameraObject is null) {
            return;
        }
        const Vector3 target = owner.transform.position + Vector3(0.0f, lookHeight, 0.0f);
        const float flat = cos(cameraPitch_);
        Vector3 eye = target - Vector3(sin(cameraYaw_) * flat, -sin(cameraPitch_), cos(cameraYaw_) * flat) * cameraDistance;

        // 砂と水面より下へは潜らせない
        float floorHeight = 0.0f;
        float ground;
        if (GroundAt(eye.x, eye.z, ground)) {
            floorHeight = Max(floorHeight, ground);
        }
        eye.y = Max(eye.y, floorHeight + 0.4f);

        const Vector3 view = target - eye;
        cameraObject.transform.position = eye;
        cameraObject.transform.rotation = Vector3(atan2(-view.y, Length(Vector3(view.x, 0.0f, view.z))), atan2(view.x, view.z), 0.0f);
    }

    // ===== 計算 =====

    private float Approach(float value, float target, float amount)
    {
        if (value < target) {
            return Min(value + amount, target);
        }
        return Max(value - amount, target);
    }

    // 角度を近い回り方で target へ amount だけ寄せる
    private float TurnToward(float angle, float target, float amount)
    {
        const float pi = 3.14159265f;
        float diff = fmod(target - angle + pi, pi * 2.0f);
        if (diff < 0.0f) {
            diff += pi * 2.0f;
        }
        diff -= pi;
        if (abs(diff) <= amount) {
            return target;
        }
        return angle + (diff > 0.0f ? amount : -amount);
    }
}
