// 物理の挙動を目で見て確かめるための操作台
// 置いたものを並べ直したり、球を撃ち込んだり、重力と時間の速さを変えたりできる
[DisplayName("物理テスト：操作台")]
class PhysicsLab : ScriptComponent
{
    [Asset("Prefab")] [Tooltip("落とす箱")]
    string boxPrefab = "Application/Assets/Prefabs/Physics/PhysicsBox.prefab";

    [Asset("Prefab")] [Tooltip("落とす球")]
    string ballPrefab = "Application/Assets/Prefabs/Physics/PhysicsBall.prefab";

    [Asset("Prefab")] [Tooltip("動かない台と斜面")]
    string platformPrefab = "Application/Assets/Prefabs/Physics/PhysicsPlatform.prefab";

    [Asset("Prefab")] [Tooltip("氷の坂（Ice.physmat を参照する板）")]
    string icePlatformPrefab = "Application/Assets/Prefabs/Physics/PhysicsIcePlatform.prefab";

    [Range(5.0f, 80.0f)] [Tooltip("撃ち出す球の速さ")]
    float shootSpeed = 28.0f;

    [Range(1.0f, 20.0f)] [Tooltip("落とすものを出す高さ")]
    float dropHeight = 9.0f;

    // 置いたものの控え。並べ直すときにまとめて片付ける
    private array<GameObject@> placed_;
    private array<GameObject@> tower_;
    private int serial_ = 0;

    // 3 段積みが止まっているかの計測
    private array<Vector3> towerLastPos_;
    private float towerStillTime_ = 0.0f;
    private bool towerReported_ = false;

    private float smoothedFps_ = 0.0f;
    private int gravityMode_ = 0;
    private bool slowMotion_ = false;
    private Text3DRenderer@ statusText_;

    void Start()
    {
        GameObject@ status = owner.FindObject("StatusBoard");
        if (status !is null) {
            status.GetComponent(@statusText_);
        }
        BuildAll();
    }

    void Update()
    {
        // フレーム時間を均して fps にする
        const float frameDelta = Time::UnscaledDeltaTime();
        if (frameDelta > 0.0f) {
            const float instant = 1.0f / frameDelta;
            smoothedFps_ = (smoothedFps_ <= 0.0f) ? instant : (smoothedFps_ * 0.9f + instant * 0.1f);
        }

        HandleInput();
        MeasureTower();
        UpdateStatus();
    }

    // ===== 配置 =====

    private void BuildAll()
    {
        ClearAll();
        BuildStack();
        BuildSlope();
        BuildBouncers();
        BuildSeesaw();
        BuildThinWall();
    }

    // 箱を 3 段。10 秒静止すれば合格（結果はログと画面に出す）
    private void BuildStack()
    {
        tower_.resize(0);
        towerLastPos_.resize(0);
        towerStillTime_ = 0.0f;
        towerReported_ = false;

        for (int level = 0; level < 3; ++level) {
            GameObject@ box = Spawn(boxPrefab, "Stack",
                Vector3(0.0f, 0.5f + float(level) * 1.02f, 0.0f));
            if (box is null) {
                continue;
            }
            tower_.insertLast(box);
            towerLastPos_.insertLast(box.transform.worldPosition);
        }
    }

    // 45 度の坂と、その上を転がる球
    private void BuildSlope()
    {
        GameObject@ slope = Spawn(icePlatformPrefab, "Slope", Vector3(-9.0f, 2.6f, 0.0f));
        if (slope !is null) {
            slope.transform.scale = Vector3(9.0f, 0.5f, 5.0f);
            slope.transform.rotation = Vector3(0.0f, 0.0f, 0.7853982f);
        }

        GameObject@ roller = Spawn(ballPrefab, "Roller", Vector3(-7.0f, 7.0f, 0.0f));
        if (roller !is null) {
            roller.transform.scale = Vector3(0.6f, 0.6f, 0.6f);
        }
    }

    // 反発係数だけを変えた 3 つの球。跳ね返る高さの違いを見る
    private void BuildBouncers()
    {
        for (int index = 0; index < 3; ++index) {
            GameObject@ ball = Spawn(ballPrefab, "Bouncer",
                Vector3(4.5f + float(index) * 2.2f, 11.0f, 2.5f));
            if (ball is null) {
                continue;
            }
            ball.transform.scale = Vector3(0.7f, 0.7f, 0.7f);

            PhysicsMaterial@ material;
            if (ball.GetComponent(@material)) {
                material.restitution = float(index) * 0.4f;   // 0.0 / 0.4 / 0.8
            }
        }
    }

    // 細い支柱に板を載せたシーソー。端に何か落とすと傾く
    private void BuildSeesaw()
    {
        GameObject@ pivot = Spawn(platformPrefab, "SeesawPivot", Vector3(-4.5f, 0.5f, 7.0f));
        if (pivot !is null) {
            pivot.transform.scale = Vector3(1.0f, 1.0f, 1.0f);
        }

        GameObject@ plank = Spawn(boxPrefab, "SeesawPlank", Vector3(-4.5f, 1.3f, 7.0f));
        if (plank !is null) {
            plank.transform.scale = Vector3(7.0f, 0.3f, 1.6f);

            Rigidbody@ body;
            if (plank.GetComponent(@body)) {
                body.mass = 4.0f;
            }
        }
    }

    // 薄い板。速い球を撃ち込んで、すり抜けないかを見る
    private void BuildThinWall()
    {
        GameObject@ wall = Spawn(platformPrefab, "ThinWall", Vector3(0.0f, 1.25f, -6.0f));
        if (wall !is null) {
            wall.transform.scale = Vector3(6.0f, 2.5f, 0.15f);
        }
    }

    private GameObject@ Spawn(const string &in prefab, const string &in label, const Vector3 &in position)
    {
        ++serial_;
        GameObject@ created = owner.InstantiatePrefab(prefab, label + "_" + serial_);
        if (created is null) {
            Warn("PhysicsLab: プレハブを置けませんでした: " + prefab);
            return null;
        }
        created.transform.position = position;
        placed_.insertLast(created);
        return created;
    }

    private void ClearAll()
    {
        for (uint index = 0; index < placed_.length(); ++index) {
            GameObject@ object = placed_[index];
            if (object !is null && object.isAlive) {
                object.Destroy();
            }
        }
        placed_.resize(0);
        tower_.resize(0);
        towerLastPos_.resize(0);
    }

    // ===== 操作 =====

    private void HandleInput()
    {
        if (Input::IsKeyTriggered(Key::Num1)) {
            DropOne(boxPrefab, "Box");
        }
        if (Input::IsKeyTriggered(Key::Num2)) {
            DropOne(ballPrefab, "Ball");
        }
        if (Input::IsKeyTriggered(Key::Num3)) {
            DropMany(100);
        }
        if (Input::IsKeyTriggered(Key::Space)) {
            ShootFromCamera();
        }
        if (Input::IsKeyTriggered(Key::R)) {
            BuildAll();
        }
        if (Input::IsKeyTriggered(Key::C)) {
            ClearAll();
        }
        if (Input::IsKeyTriggered(Key::T)) {
            slowMotion_ = !slowMotion_;
            Time::SetTimeScale(slowMotion_ ? 0.2f : 1.0f);
        }
        if (Input::IsKeyTriggered(Key::G)) {
            CycleGravity();
        }
    }

    // 少しばらけた位置へ 1 個落とす
    private void DropOne(const string &in prefab, const string &in label)
    {
        const float x = Random::Range(-3.0f, 3.0f);
        const float z = Random::Range(-3.0f, 3.0f);
        GameObject@ dropped = Spawn(prefab, label, Vector3(x, dropHeight, z));
        if (dropped !is null) {
            Rigidbody@ body;
            if (dropped.GetComponent(@body)) {
                body.angularVelocity = Vector3(
                    Random::Range(-2.0f, 2.0f),
                    Random::Range(-2.0f, 2.0f),
                    Random::Range(-2.0f, 2.0f));
            }
        }
    }

    // 負荷を見るためにまとめて落とす
    private void DropMany(int count)
    {
        for (int index = 0; index < count; ++index) {
            const float x = Random::Range(-7.0f, 7.0f);
            const float z = Random::Range(-7.0f, 7.0f);
            const float y = dropHeight + float(index) * 0.35f;
            Spawn((index % 2 == 0) ? boxPrefab : ballPrefab, "Many", Vector3(x, y, z));
        }
        Log("PhysicsLab: " + count + " 個落とした");
    }

    // 見ている向きへ球を撃ち込む
    private void ShootFromCamera()
    {
        GameCamera@ camera = Scene::GetGameCamera();
        if (camera is null || !camera.exists) {
            Warn("PhysicsLab: ゲームカメラが見つかりません");
            return;
        }

        const Vector3 angles = camera.rotation;
        const Vector3 forward = Vector3(
            cos(angles.x) * sin(angles.y),
            -sin(angles.x),
            cos(angles.x) * cos(angles.y));

        GameObject@ bullet = Spawn(ballPrefab, "Shot", camera.position + forward * 2.0f);
        if (bullet is null) {
            return;
        }
        bullet.transform.scale = Vector3(0.5f, 0.5f, 0.5f);

        Rigidbody@ body;
        if (bullet.GetComponent(@body)) {
            body.mass = 3.0f;
            body.velocity = forward * shootSpeed;
        }
    }

    private void CycleGravity()
    {
        gravityMode_ = (gravityMode_ + 1) % 3;
        if (gravityMode_ == 0) {
            Physics::gravity = Vector3(0.0f, -9.81f, 0.0f);
        } else if (gravityMode_ == 1) {
            Physics::gravity = Vector3(0.0f, -1.62f, 0.0f);   // 月
        } else {
            Physics::gravity = Vector3(0.0f, 0.0f, 0.0f);     // 無重力
        }
    }

    // ===== 計測と表示 =====

    // 3 段積みが動かなくなってからの時間を数える
    private void MeasureTower()
    {
        if (tower_.length() != towerLastPos_.length() || tower_.length() == 0) {
            return;
        }

        bool moved = false;
        for (uint index = 0; index < tower_.length(); ++index) {
            GameObject@ box = tower_[index];
            if (box is null || !box.isAlive) {
                return;
            }
            const Vector3 position = box.transform.worldPosition;
            if (Length(position - towerLastPos_[index]) > 0.002f) {
                moved = true;
            }
            towerLastPos_[index] = position;
        }

        if (moved) {
            towerStillTime_ = 0.0f;
            towerReported_ = false;
            return;
        }

        towerStillTime_ += Time::DeltaTime();
        if (!towerReported_ && towerStillTime_ >= 10.0f) {
            towerReported_ = true;
            Log("PhysicsLab: 3 段積みが 10 秒静止しました（高さ "
                + tower_[2].transform.worldPosition.y + "）");
        }
    }

    private void UpdateStatus()
    {
        if (statusText_ is null || !statusText_.exists) {
            return;
        }

        string gravity = "通常";
        if (gravityMode_ == 1) { gravity = "月"; }
        else if (gravityMode_ == 2) { gravity = "無重力"; }

        string stack = "計測中 " + int(towerStillTime_) + " 秒";
        if (towerReported_) {
            stack = "10 秒静止 OK";
        }

        statusText_.text =
            "1:箱  2:球  3:100 個  Space:撃つ\n"
            + "R:並べ直す  C:片付ける  T:スロー  G:重力\n"
            + "重力 " + gravity + "   時間 " + (slowMotion_ ? "0.2 倍" : "等倍")
            + "   " + int(smoothedFps_ + 0.5f) + " fps\n"
            + "剛体 " + Physics::GetBodyCount() + "（眠り " + Physics::GetSleepingCount()
            + "）  接触 " + Physics::GetContactCount() + "\n"
            + "3 段積み " + stack + "   置いたもの " + placed_.length();
    }
}
