using namespace CoreEngine;

// メッシュの当たり判定の試験
// 始めると地形（Course と、回して拡大縮小した Course_Turned）の通り道を順に自動で歩き、
// 通り道ごとの結果と合否をコンソールへ出す。終わると WASD で歩ける（R で自動の試験をやり直す）
[DisplayName("試験：メッシュの当たり")]
class MeshColliderProbe : ScriptComponent
{
    [Tooltip("始めると自動で通り道を歩いて記録する")]
    bool autoRun = true;

    [Tooltip("歩く速さ m/s")]
    float walkSpeed = 3.0f;

    [Tooltip("壁へ走る速さ m/s")]
    float runSpeed = 9.0f;

    [Tooltip("跳び上がる速さ m/s")]
    float jumpSpeed = 5.5f;

    private CharacterController@ controller_;
    private GameObject@ camera_;

    // 0 = 地形の読み込み待ち・1 = 通り道を歩く・2 = 手で歩く
    private int phase_ = 0;
    private array<MeshProbeRun@> runs_;
    private int runIndex_ = 0;
    private float runTime_ = 0.0f;
    private bool jumped_ = false;
    private int failures_ = 0;
    private float cameraYaw_ = 0.0f;

    // 1 本ぶんの記録
    private float maxY_ = 0.0f;
    private float minGroundY_ = 0.0f;
    private float maxGroundY_ = 0.0f;
    private int groundSamples_ = 0;
    private int airborneFrames_ = 0;
    private float maxLocalZ_ = 0.0f;
    private float maxInsideY_ = 0.0f;
    private float maxOutsideY_ = 0.0f;
    private float startLocalX_ = 0.0f;

    void Start()
    {
        owner.GetComponent(@controller_);
        @camera_ = owner.FindObject("MainCamera");
        BuildRuns();
        phase_ = autoRun ? 0 : 2;
    }

    void Update()
    {
        if (controller_ is null || !controller_.exists) {
            return;
        }
        if (Input::IsKeyTriggered(Key::R)) {
            failures_ = 0;
            phase_ = 0;
        }

        if (phase_ == 0) {
            WaitForGround();
        } else if (phase_ == 1) {
            StepRun();
        } else {
            WalkByHand();
        }
        UpdateCamera();
        UpdateAnimator();
    }

    // ===== 通り道 =====

    private void AddRun(const string &in name, const string &in course, int kind, float x, float z,
                        float dirX, float dirZ, float speed, float duration, float jumpAt, float jump,
                        float stopZ = 1000.0f)
    {
        MeshProbeRun@ run = MeshProbeRun();
        run.name = name;
        run.course = course;
        run.kind = kind;
        run.x = x;
        run.z = z;
        run.dirX = dirX;
        run.dirZ = dirZ;
        run.speed = speed;
        run.duration = duration;
        run.jumpAt = jumpAt;
        run.jumpSpeed = jump;
        run.stopZ = stopZ;
        runs_.insertLast(run);
    }

    private void BuildRuns()
    {
        runs_.resize(0);
        const array<string> courses = { "Course", "Course_Turned" };
        for (uint i = 0; i < courses.length(); ++i) {
            const string c = courses[i];
            AddRun(c + " 平地の継ぎ目", c, 0, -11.5f, 1.0f, 1.0f, 0.0f, walkSpeed, 7.5f, -1.0f, 0.0f);
            AddRun(c + " 30 度の坂", c, 1, -10.0f, 1.0f, 0.0f, 1.0f, walkSpeed, 6.0f, -1.0f, 0.0f, 16.0f);
            AddRun(c + " 50 度の坂", c, 2, -6.0f, 1.0f, 0.0f, 1.0f, walkSpeed, 4.0f, -1.0f, 0.0f);
            AddRun(c + " 階段", c, 3, -2.0f, 1.0f, 0.0f, 1.0f, walkSpeed, 6.0f, -1.0f, 0.0f, 12.5f);
            AddRun(c + " 段差", c, 4, 2.0f, 1.0f, 0.0f, 1.0f, walkSpeed, 4.0f, -1.0f, 0.0f);
            AddRun(c + " トンネル", c, 5, 6.0f, -2.0f, 0.0f, 1.0f, walkSpeed, 5.0f, 0.2f, jumpSpeed);
            AddRun(c + " 薄い壁", c, 6, 10.0f, 1.0f, 0.0f, 1.0f, runSpeed, 3.0f, -1.0f, 0.0f);
        }
        AddRun("箱の 50 度の坂", "Course", 8, -6.0f, 10.0f, 0.0f, 1.0f, walkSpeed, 4.0f, -1.0f, 0.0f);
        AddRun("片面の板", "", 7, 0.0f, -2.5f, 0.0f, 1.0f, 0.0f, 3.0f, 0.3f, 8.0f);
    }

    private void WaitForGround()
    {
        MeshProbeRun@ first = runs_[0];
        const Vector3 start = ToWorld(first.course, Vector3(first.x, 0.0f, first.z));
        Teleport(start, 0.0f);
        RaycastHit@ hit = Physics::Raycast(start + Vector3(0.0f, 5.0f, 0.0f), Vector3(0.0f, -1.0f, 0.0f), 20.0f);
        if (hit is null) {
            return;
        }
        Log("MeshColliderProbe: 地形に当たるようになったので試験を始める");
        CheckRays();
        runIndex_ = 0;
        BeginRun();
        phase_ = 1;
    }

    private void BeginRun()
    {
        MeshProbeRun@ run = runs_[runIndex_];
        const Vector3 start = ToWorld(run.course, Vector3(run.x, 0.0f, run.z));
        const Vector3 dir = DirectionToWorld(run.course, run.dirX, run.dirZ);
        Teleport(start + Vector3(0.0f, 0.03f, 0.0f), atan2(dir.x, dir.z));
        runTime_ = 0.0f;
        jumped_ = false;
        maxY_ = -1000.0f;
        minGroundY_ = 1000.0f;
        maxGroundY_ = -1000.0f;
        groundSamples_ = 0;
        airborneFrames_ = 0;
        maxLocalZ_ = -1000.0f;
        maxInsideY_ = -1000.0f;
        maxOutsideY_ = -1000.0f;
        startLocalX_ = run.x;
    }

    private void StepRun()
    {
        MeshProbeRun@ run = runs_[runIndex_];
        const float settle = 0.3f;
        runTime_ += Time::DeltaTime();

        Vector3 dir = DirectionToWorld(run.course, run.dirX, run.dirZ);
        if (runTime_ < settle) {
            controller_.SimpleMove(Vector3(0.0f, 0.0f, 0.0f));
        } else {
            controller_.SimpleMove(dir * run.speed);
        }
        if (run.jumpAt >= 0.0f && !jumped_ && runTime_ >= settle + run.jumpAt) {
            jumped_ = true;
            controller_.Jump(run.jumpSpeed);
            if (owner.animator !is null) {
                owner.animator.SetTrigger("Jump");
            }
        }
        // トンネルの中でもう一度跳ぶ
        if (run.kind == 5 && jumped_ && runTime_ >= settle + 3.3f && controller_.grounded && maxInsideY_ < -100.0f) {
            maxInsideY_ = 0.0f;
            controller_.Jump(run.jumpSpeed);
            if (owner.animator !is null) {
                owner.animator.SetTrigger("Jump");
            }
        }

        const Vector3 position = owner.transform.position;
        const Vector3 local = ToLocal(run.course, position);
        if (runTime_ >= settle) {
            if (position.y > maxY_) { maxY_ = position.y; }
            if (local.z > maxLocalZ_) { maxLocalZ_ = local.z; }
            if (run.kind == 0 && runTime_ >= settle + 0.2f) {
                if (controller_.grounded) {
                    if (position.y < minGroundY_) { minGroundY_ = position.y; }
                    if (position.y > maxGroundY_) { maxGroundY_ = position.y; }
                    ++groundSamples_;
                } else {
                    ++airborneFrames_;
                }
            }
            // 段差は接地している間の高さで、どの段に乗ったかを見る（角を上った勢いの跳ねは数えない）
            if (run.kind == 4 && controller_.grounded && position.y > maxGroundY_) { maxGroundY_ = position.y; }
            if (run.kind == 5) {
                if (runTime_ < settle + 1.6f && position.y > maxOutsideY_) { maxOutsideY_ = position.y; }
                if (maxInsideY_ > -100.0f && position.y > maxInsideY_) { maxInsideY_ = position.y; }
            }
        }

        if (runTime_ >= settle + run.duration || local.z >= run.stopZ) {
            Report(run, position, local);
            ++runIndex_;
            if (runIndex_ >= int(runs_.length())) {
                if (failures_ == 0) {
                    Log("MeshColliderProbe: 全部 OK（WASD で歩ける・R でやり直す）");
                } else {
                    Warn("MeshColliderProbe: NG が " + failures_ + " 件（WASD で歩ける・R でやり直す）");
                }
                phase_ = 2;
                return;
            }
            BeginRun();
        }
    }

    private void Report(MeshProbeRun@ run, const Vector3 &in position, const Vector3 &in local)
    {
        const float sy = CourseScaleY(run.course);
        string text;
        bool ok = false;
        if (run.kind == 0) {
            const float span = (maxGroundY_ - minGroundY_) * 1000.0f;
            ok = groundSamples_ > 0 && span < 1.0f && airborneFrames_ == 0;
            text = "上下の揺れ " + F(span, 3) + " mm・接地が切れたフレーム " + airborneFrames_ + "・接地のフレーム " + groundSamples_;
        } else if (run.kind == 1) {
            ok = maxY_ >= 2.0f * sy - 0.05f && local.z > 14.0f && position.y < 0.05f;
            text = "いちばん高い所 " + F(maxY_, 3) + " m（台は " + F(2.0f * sy, 2) + " m）・終わりの奥行き " + F(local.z, 2) + "・高さ " + F(position.y, 3);
        } else if (run.kind == 2) {
            ok = maxY_ < 1.0f * sy && local.z < 4.0f + 2.0f / tan(50.0f * 0.0174533f);
            text = "いちばん高い所 " + F(maxY_, 3) + " m（坂の上は " + F(2.0f * sy, 2) + " m）・終わりの奥行き " + F(local.z, 2);
        } else if (run.kind == 3) {
            ok = maxY_ >= 1.6f * sy - 0.05f && local.z > 10.9f && position.y < 0.05f;
            text = "いちばん高い所 " + F(maxY_, 3) + " m（踊り場は " + F(1.6f * sy, 2) + " m）・終わりの奥行き " + F(local.z, 2) + "・高さ " + F(position.y, 3);
        } else if (run.kind == 4) {
            ok = maxGroundY_ >= 0.25f * sy - 0.02f && maxGroundY_ < 0.6f * sy - 0.1f && local.z < 8.0f;
            text = "乗った高さ " + F(maxGroundY_, 3) + " m（低い段 " + F(0.25f * sy, 3) + "・高い段 " + F(0.6f * sy, 3) + "）・いちばん高い所 " + F(maxY_, 3) + " m・止まった奥行き " + F(local.z, 2);
        } else if (run.kind == 5) {
            const float ceiling = 2.3f * sy - 1.8f;
            ok = maxInsideY_ > -100.0f && maxInsideY_ <= ceiling + 0.01f && maxOutsideY_ > ceiling + 0.3f;
            text = "外で跳んだ高さ " + F(maxOutsideY_, 3) + " m・中で跳んだ高さ " + F(maxInsideY_, 3) + " m（天井まで " + F(ceiling, 3) + " m）";
        } else if (run.kind == 6) {
            const float drift = local.x - startLocalX_;
            ok = maxLocalZ_ < 12.95f && (drift > 0.3f || drift < -0.3f);
            text = "いちばん奥 " + F(maxLocalZ_, 3) + "（壁は 12.95〜13.05）・横へ滑った量 " + F(drift, 2);
        } else if (run.kind == 8) {
            ok = maxY_ < 1.0f && local.z < 13.0f + 1.0f / tan(50.0f * 0.0174533f);
            text = "いちばん高い所 " + F(maxY_, 3) + " m（坂の上は 3.06 m）・終わりの奥行き " + F(local.z, 2) + "（坂の足は 13.0）";
        } else {
            ok = position.y > 2.15f && position.y < 2.25f && controller_.grounded;
            text = "いちばん高い所 " + F(maxY_, 3) + " m・終わりの高さ " + F(position.y, 3) + " m（板は 2.2 m）・" + (controller_.grounded ? "接地" : "空中");
        }
        Result(run.name, ok, text);
    }

    private void CheckRays()
    {
        const array<string> courses = { "Course", "Course_Turned" };
        for (uint i = 0; i < courses.length(); ++i) {
            const string c = courses[i];
            const float sy = CourseScaleY(c);
            const float sz = CourseScaleZ(c);
            CheckDownRay(c + " のレイ（平地）", ToWorld(c, Vector3(0.0f, 0.0f, 1.0f)), 0.0f, 0.0f, "Course");
            const float run30 = 2.0f / tan(30.0f * 0.0174533f);
            const float angle = atan2(2.0f * sy, run30 * sz) * 57.29578f;
            CheckDownRay(c + " のレイ（30 度の坂の中ほど）", ToWorld(c, Vector3(-10.0f, 0.0f, 4.0f + run30 * 0.5f)), 1.0f * sy, angle, "Course");
            CheckDownRay(c + " のレイ（階段の踊り場）", ToWorld(c, Vector3(-2.0f, 0.0f, 7.4f)), 1.6f * sy, 0.0f, "Course");
        }

        RaycastHit@ below = Physics::Raycast(Vector3(0.0f, 0.5f, -2.5f), Vector3(0.0f, 1.0f, 0.0f), 5.0f);
        const bool passed = below is null || below.gameObject.name != "OneWayPlate";
        Result("片面の板のレイ（下から）", passed, below is null ? "当たらない" : "当たった物 " + below.gameObject.name);
        CheckDownRay("片面の板のレイ（上から）", Vector3(0.0f, 0.0f, -2.5f), 2.2f, 0.0f, "OneWayPlate");
    }

    private void CheckDownRay(const string &in name, const Vector3 &in at, float height, float angle, const string &in expected)
    {
        RaycastHit@ hit = Physics::Raycast(Vector3(at.x, 10.0f, at.z), Vector3(0.0f, -1.0f, 0.0f), 30.0f);
        if (hit is null) {
            Result(name, false, "当たらない");
            return;
        }
        const float hitAngle = acos(Clamp01(hit.normal.y)) * 57.29578f;
        const bool ok = hit.gameObject.name.findFirst(expected) == 0
            && Near(hit.point.y, height, 0.001f) && Near(hitAngle, angle, 0.1f);
        Result(name, ok, hit.gameObject.name + "・高さ " + F(hit.point.y, 4) + " m（" + F(height, 4) + "）・面の傾き "
            + F(hitAngle, 2) + " 度（" + F(angle, 2) + "）・距離 " + F(hit.distance, 3));
    }

    private void Result(const string &in name, bool ok, const string &in text)
    {
        if (ok) {
            Log("MeshColliderProbe: OK " + name + ": " + text);
        } else {
            ++failures_;
            Warn("MeshColliderProbe: NG " + name + ": " + text);
        }
    }

    // ===== 手で歩く =====

    private void WalkByHand()
    {
        if (Input::IsKeyPressed(Key::Q)) { cameraYaw_ -= 2.0f * Time::DeltaTime(); }
        if (Input::IsKeyPressed(Key::E)) { cameraYaw_ += 2.0f * Time::DeltaTime(); }

        const Vector2 stick = Input::GetAxis2D(
            InputAction::MoveLeft, InputAction::MoveRight,
            InputAction::MoveBack, InputAction::MoveForward);
        const Vector3 forward = Vector3(sin(cameraYaw_), 0.0f, cos(cameraYaw_));
        const Vector3 right = Vector3(cos(cameraYaw_), 0.0f, -sin(cameraYaw_));
        Vector3 move = forward * stick.y + right * stick.x;
        const float length = sqrt(move.x * move.x + move.z * move.z);
        if (length > 0.0001f) {
            move = move * (1.0f / length);
            owner.transform.rotation = Vector3(0.0f, atan2(move.x, move.z), 0.0f);
        }
        const float speed = Input::IsActionPressed(InputAction::Sprint) ? runSpeed : walkSpeed;
        controller_.SimpleMove(move * speed);
        if (Input::IsActionTriggered(InputAction::Jump)) {
            controller_.Jump(jumpSpeed);
            if (owner.animator !is null) {
                owner.animator.SetTrigger("Jump");
            }
        }
        if (Input::IsKeyTriggered(Key::F) || owner.transform.position.y < -20.0f) {
            Teleport(Vector3(0.0f, 0.03f, 0.0f), cameraYaw_);
        }
    }

    // ===== 見た目 =====

    private void UpdateCamera()
    {
        if (camera_ is null || phase_ == 0) {
            return;
        }
        if (phase_ == 1) {
            MeshProbeRun@ run = runs_[runIndex_];
            const Vector3 dir = DirectionToWorld(run.course, run.dirX, run.dirZ);
            cameraYaw_ = atan2(dir.x, dir.z);
        }
        const Vector3 target = owner.transform.position;
        camera_.transform.position = target + Vector3(-sin(cameraYaw_) * 7.0f, 3.8f, -cos(cameraYaw_) * 7.0f);
        camera_.transform.rotation = Vector3(0.38f, cameraYaw_, 0.0f);
    }

    private void UpdateAnimator()
    {
        Animator@ animator = owner.animator;
        if (animator is null) {
            return;
        }
        const Vector3 velocity = controller_.velocity;
        animator.SetFloat("Speed", sqrt(velocity.x * velocity.x + velocity.z * velocity.z));
        animator.SetBool("Grounded", controller_.grounded);
    }

    // ===== 座標 =====

    private void Teleport(const Vector3 &in position, float yaw)
    {
        owner.transform.position = position;
        owner.transform.rotation = Vector3(0.0f, yaw, 0.0f);
        controller_.velocity = Vector3(0.0f, 0.0f, 0.0f);
    }

    private GameObject@ Course(const string &in name)
    {
        if (name.isEmpty()) {
            return null;
        }
        return owner.FindObject(name);
    }

    private float CourseScaleY(const string &in name)
    {
        GameObject@ course = Course(name);
        return course is null ? 1.0f : course.transform.scale.y;
    }

    private float CourseScaleZ(const string &in name)
    {
        GameObject@ course = Course(name);
        return course is null ? 1.0f : course.transform.scale.z;
    }

    // 地形のローカルの点をワールドへ（回転は y 軸まわりだけ）
    private Vector3 ToWorld(const string &in name, const Vector3 &in local)
    {
        GameObject@ course = Course(name);
        if (course is null) {
            return local;
        }
        const Vector3 p = course.transform.position;
        const Vector3 s = course.transform.scale;
        const float yaw = course.transform.rotation.y;
        const float x = local.x * s.x;
        const float z = local.z * s.z;
        return Vector3(p.x + x * cos(yaw) + z * sin(yaw), p.y + local.y * s.y, p.z - x * sin(yaw) + z * cos(yaw));
    }

    private Vector3 ToLocal(const string &in name, const Vector3 &in world)
    {
        GameObject@ course = Course(name);
        if (course is null) {
            return world;
        }
        const Vector3 p = course.transform.position;
        const Vector3 s = course.transform.scale;
        const float yaw = course.transform.rotation.y;
        const float dx = world.x - p.x;
        const float dz = world.z - p.z;
        return Vector3((dx * cos(yaw) - dz * sin(yaw)) / s.x, (world.y - p.y) / s.y, (dx * sin(yaw) + dz * cos(yaw)) / s.z);
    }

    private Vector3 DirectionToWorld(const string &in name, float dirX, float dirZ)
    {
        const Vector3 a = ToWorld(name, Vector3(0.0f, 0.0f, 0.0f));
        const Vector3 b = ToWorld(name, Vector3(dirX, 0.0f, dirZ));
        const float dx = b.x - a.x;
        const float dz = b.z - a.z;
        const float length = sqrt(dx * dx + dz * dz);
        return Vector3(dx / length, 0.0f, dz / length);
    }

    private string F(float value, uint digits)
    {
        return formatFloat(value, "", 0, digits);
    }

    private bool Near(float a, float b, float tolerance)
    {
        const float d = a - b;
        return d <= tolerance && d >= -tolerance;
    }

    private float Clamp01(float value)
    {
        return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
    }
}

// 自動で歩く 1 本
class MeshProbeRun
{
    string name;
    string course;      // 地形のオブジェクトの名前（空ならワールドの座標）
    int kind = 0;       // 0 平地・1 30 度・2 50 度・3 階段・4 段差・5 トンネル・6 壁・7 片面の板・8 箱の 50 度の坂
    float x = 0.0f;     // 地形のローカルの出発点
    float z = 0.0f;
    float dirX = 0.0f;  // 地形のローカルの進む向き
    float dirZ = 1.0f;
    float speed = 0.0f;
    float duration = 0.0f;
    float jumpAt = -1.0f;   // 歩き始めてから跳ぶまでの秒数（負なら跳ばない）
    float jumpSpeed = 0.0f;
    float stopZ = 1000.0f;  // ローカルの奥行きがここに届いたら終える
}
