using namespace CoreEngine;

// 剛体とメッシュの当たりの試験
// 地形（Ground）に当たるようになったら剛体に重力を入れて動かし、物ごとの結果と合否をコンソールへ出す
[DisplayName("試験：剛体とメッシュの当たり")]
class MeshPhysicsProbe : ScriptComponent
{
    [Tooltip("RollBall に始めに与える坂の下向きの速さ m/s")]
    float rollSpeed = 1.5f;

    [Tooltip("FastBallPlate と FastBallGround に与える下向きの速さ m/s")]
    float fastSpeed = 70.0f;

    [Tooltip("DropBox が眠るまでに許す秒数")]
    float sleepLimit = 10.0f;

    // 地形の形（MeshGround と同じ数）
    private float slopeStartZ_ = 4.0f;
    private float slopeHeight_ = 4.0f;
    private float slopeDegrees_ = 20.0f;

    // 0 = 地形の読み込み待ち・1 = 試験中・2 = 終わり
    private int phase_ = 0;
    private float time_ = 0.0f;
    private int reported_ = 0;
    private int failures_ = 0;
    private array<string> bodyNames_ = { "RollBall", "SlopeBox", "DropBox", "CapsuleRoller",
                                         "FastBallPlate", "FastBallGround", "MeshOnBody" };

    // 坂を転がる球
    private bool rollDone_ = false;
    private float rollSlopeMaxGap_ = -1000.0f;
    private float rollSlopeMinGap_ = 1000.0f;
    private float rollFloorMaxGap_ = -1000.0f;
    private float rollFloorMinGap_ = 1000.0f;
    private int rollSlopeSamples_ = 0;
    private int rollFloorSamples_ = 0;

    // 坂に置いた箱
    private bool slopeBoxDone_ = false;
    private Vector3 slopeBoxStart_;

    // 平地に落とす箱
    private bool dropDone_ = false;

    // 倒れて転がるカプセル
    private bool capsuleDone_ = false;
    private Vector3 capsuleStart_;
    private float capsuleMaxSpin_ = 0.0f;
    private float capsuleMinGap_ = 1000.0f;

    // 速い球
    private bool plateDone_ = false;
    private float plateMinY_ = 1000.0f;
    private bool groundDone_ = false;
    private float groundMinY_ = 1000.0f;

    // 剛体と同じオブジェクトのメッシュの形
    private bool meshBodyDone_ = false;

    void Start()
    {
        phase_ = 0;
    }

    void Update()
    {
        if (phase_ == 0) {
            WaitForGround();
            return;
        }
        if (phase_ != 1) {
            return;
        }
        time_ += Time::DeltaTime();

        StepRollBall();
        StepSlopeBox();
        StepDropBox();
        StepCapsule();
        StepFastBalls();
        StepMeshOnBody();

        if (reported_ >= 7) {
            if (failures_ == 0) {
                Log("MeshPhysicsProbe: 全部 OK");
            } else {
                Warn("MeshPhysicsProbe: NG が " + failures_ + " 件");
            }
            phase_ = 2;
        }
    }

    // ===== 始め =====

    private void WaitForGround()
    {
        RaycastHit@ hit = Physics::Raycast(Vector3(0.0f, 2.0f, 0.0f), Vector3(0.0f, -1.0f, 0.0f), 10.0f);
        if (hit is null || hit.gameObject is null || hit.gameObject.name != "Ground") {
            return;
        }
        Log("MeshPhysicsProbe: 地形に当たるようになったので剛体に重力を入れる");

        for (uint i = 0; i < bodyNames_.length(); ++i) {
            Rigidbody@ body = Body(bodyNames_[i]);
            if (body is null) {
                Warn("MeshPhysicsProbe: " + bodyNames_[i] + " が見つからない");
                continue;
            }
            body.useGravity = true;
            body.WakeUp();
        }
        Body("RollBall").velocity = Vector3(0.0f, 0.0f, -rollSpeed);
        Body("FastBallPlate").velocity = Vector3(0.0f, -fastSpeed, 0.0f);
        Body("FastBallGround").velocity = Vector3(fastSpeed * 0.06f, -fastSpeed, fastSpeed * 0.02f);

        slopeBoxStart_ = Position("SlopeBox");
        capsuleStart_ = Position("CapsuleRoller");
        time_ = 0.0f;
        phase_ = 1;
    }

    // ===== 物ごと =====

    // 坂を転がり下りて平地を進む間、地面からの浮き（跳ね）を測る
    private void StepRollBall()
    {
        if (rollDone_) {
            return;
        }
        const Vector3 p = Position("RollBall");
        const float radius = 0.5f;
        const float slopeEndZ = slopeStartZ_ + slopeHeight_ / tan(slopeDegrees_ * 3.14159265f / 180.0f);
        if (p.z > slopeStartZ_ + 1.5f && p.z < slopeEndZ - 1.5f) {
            const float gap = SlopeDistance(p) - radius;
            if (gap > rollSlopeMaxGap_) { rollSlopeMaxGap_ = gap; }
            if (gap < rollSlopeMinGap_) { rollSlopeMinGap_ = gap; }
            ++rollSlopeSamples_;
        }
        if (p.z < slopeStartZ_ - 1.5f && p.z > -10.5f) {
            const float gap = p.y - radius;
            if (gap > rollFloorMaxGap_) { rollFloorMaxGap_ = gap; }
            if (gap < rollFloorMinGap_) { rollFloorMinGap_ = gap; }
            ++rollFloorSamples_;
        }
        if (p.z > -10.5f && time_ < 15.0f) {
            return;
        }
        rollDone_ = true;
        const bool ok = rollSlopeSamples_ > 0 && rollFloorSamples_ > 0
            && rollSlopeMaxGap_ <= 0.01f && rollSlopeMinGap_ >= -0.03f
            && rollFloorMaxGap_ <= 0.01f && rollFloorMinGap_ >= -0.03f;
        Result(ok, "坂を転がる球", "坂の上の浮き 最大 " + Mm(rollSlopeMaxGap_) + "・最小 " + Mm(rollSlopeMinGap_)
            + "（" + rollSlopeSamples_ + " フレーム）／平地 最大 " + Mm(rollFloorMaxGap_) + "・最小 " + Mm(rollFloorMinGap_)
            + "（" + rollFloorSamples_ + " フレーム）・" + F(time_, 1) + " 秒で手前の壁へ");
    }

    // 20 度の坂に置いた箱が摩擦で止まる
    private void StepSlopeBox()
    {
        if (slopeBoxDone_ || time_ < 6.0f) {
            return;
        }
        slopeBoxDone_ = true;
        const Vector3 p = Position("SlopeBox");
        const float moved = Length(p - slopeBoxStart_);
        Rigidbody@ body = Body("SlopeBox");
        const float speed = Length(body.velocity);
        const bool ok = moved < 0.1f && speed < 0.05f;
        Result(ok, "坂で止まる箱", "動いた距離 " + Mm(moved) + "・速さ " + F(speed, 3) + " m/s"
            + (body.sleeping ? "・眠った" : "・起きている"));
    }

    // 平地に回して落とした箱が面で止まり、決めた秒数のうちに眠る
    private void StepDropBox()
    {
        if (dropDone_) {
            return;
        }
        Rigidbody@ body = Body("DropBox");
        if (!body.sleeping && time_ < sleepLimit) {
            return;
        }
        dropDone_ = true;
        const Vector3 p = Position("DropBox");
        const bool ok = body.sleeping && p.y > 0.48f && p.y < 0.52f;
        Result(ok, "平地に落とした箱", (body.sleeping ? F(time_, 2) + " 秒で眠った" : F(sleepLimit, 0) + " 秒で眠らない")
            + "・中心の高さ " + F(p.y, 4) + " m");
    }

    // 坂に傾けて立てたカプセルが倒れ、横になって転がり下りる
    private void StepCapsule()
    {
        if (capsuleDone_) {
            return;
        }
        const Vector3 p = Position("CapsuleRoller");
        Rigidbody@ body = Body("CapsuleRoller");
        const float spin = Length(body.angularVelocity);
        if (spin > capsuleMaxSpin_) { capsuleMaxSpin_ = spin; }
        const float gap = SurfaceDistance(p);
        if (gap < capsuleMinGap_) { capsuleMinGap_ = gap; }
        if (time_ < 8.0f) {
            return;
        }
        capsuleDone_ = true;
        const float downhill = capsuleStart_.z - p.z;
        const float lying = SurfaceDistance(p);
        const bool ok = lying < 0.45f && downhill > 2.0f && capsuleMaxSpin_ > 2.0f && capsuleMinGap_ > 0.2f;
        Result(ok, "倒れて転がるカプセル", "地面から中心まで " + F(lying, 3) + " m（半径 0.3 m）・坂の下へ "
            + F(downhill, 2) + " m・回る速さ 最大 " + F(capsuleMaxSpin_, 1) + " rad/s・中心の最低 " + F(capsuleMinGap_, 3) + " m");
    }

    // 1 刻みで半径より大きく進む球が、厚さの無い板と地面を抜けない
    private void StepFastBalls()
    {
        const Vector3 plate = Position("FastBallPlate");
        if (plate.y < plateMinY_) { plateMinY_ = plate.y; }
        const Vector3 ground = Position("FastBallGround");
        if (ground.y < groundMinY_) { groundMinY_ = ground.y; }
        if (time_ < 3.0f) {
            return;
        }
        const float plateY = Position("ThinPlate").y;
        if (!plateDone_) {
            plateDone_ = true;
            const bool ok = plateMinY_ > plateY + 0.125f && plate.y > plateY + 0.22f && plate.y < plateY + 0.28f;
            Result(ok, "速い球と厚さの無い板", F(fastSpeed, 0) + " m/s で落として、中心の最低 " + F(plateMinY_ - plateY, 3)
                + " m・止まった高さ " + F(plate.y - plateY, 3) + " m（板からの高さ・半径 0.25 m）");
        }
        if (!groundDone_) {
            groundDone_ = true;
            const bool ok = groundMinY_ > 0.125f && ground.y > 0.22f && ground.y < 0.28f;
            Result(ok, "速い球と地面", F(fastSpeed, 0) + " m/s で斜めに落として、中心の最低 " + F(groundMinY_, 3)
                + " m・止まった高さ " + F(ground.y, 3) + " m（半径 0.25 m）");
        }
    }

    // 剛体と同じオブジェクトのメッシュの形は物理が使わず、箱の形だけで床に乗る
    private void StepMeshOnBody()
    {
        if (meshBodyDone_ || time_ < 4.0f) {
            return;
        }
        meshBodyDone_ = true;
        const Vector3 p = Position("MeshOnBody");
        const bool ok = p.y > 0.48f && p.y < 0.52f;
        Result(ok, "メッシュの形を持つ剛体", "箱の形で床に乗った（中心の高さ " + F(p.y, 4) + " m）。メッシュの形は使わない");
    }

    // ===== 道具 =====

    private void Result(bool ok, const string &in name, const string &in text)
    {
        ++reported_;
        if (ok) {
            Log("MeshPhysicsProbe: OK " + name + ": " + text);
        } else {
            ++failures_;
            Warn("MeshPhysicsProbe: NG " + name + ": " + text);
        }
    }

    private Rigidbody@ Body(const string &in name)
    {
        GameObject@ object = owner.FindObject(name);
        return object is null ? null : object.rigidbody;
    }

    private Vector3 Position(const string &in name)
    {
        GameObject@ object = owner.FindObject(name);
        return object is null ? Vector3(0.0f, 0.0f, 0.0f) : object.transform.position;
    }

    // 坂の面からの距離（面の向きに測る）
    private float SlopeDistance(const Vector3 &in p)
    {
        const float angle = slopeDegrees_ * 3.14159265f / 180.0f;
        return (p.y * cos(angle)) - (p.z - slopeStartZ_) * sin(angle);
    }

    // 平地・坂・台のうち、点の真下にある面からの距離
    private float SurfaceDistance(const Vector3 &in p)
    {
        const float slopeEndZ = slopeStartZ_ + slopeHeight_ / tan(slopeDegrees_ * 3.14159265f / 180.0f);
        if (p.z <= slopeStartZ_) {
            return p.y;
        }
        if (p.z >= slopeEndZ) {
            return p.y - slopeHeight_;
        }
        return SlopeDistance(p);
    }

    private string Mm(float meters)
    {
        return formatFloat(meters * 1000.0f, "", 0, 1) + " mm";
    }

    private string F(float value, uint digits)
    {
        return formatFloat(value, "", 0, digits);
    }
}
