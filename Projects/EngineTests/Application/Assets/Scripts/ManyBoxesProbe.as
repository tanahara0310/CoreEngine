using namespace CoreEngine;

// コライダーの多いシーンの試験
// 始めると、柱（Pillars）に箱 900 個を足し、別々の箱 100 個と落ちる球 20 個をプレハブから置く。
// キャラを柱の間の通路で四角く歩かせながら、毎フレーム レイ 8 本・OverlapSphere・OverlapBox を投げ、
// 歩き終えたら結果と合否をコンソールへ出す
[DisplayName("試験：コライダーの多いシーン")]
class ManyBoxesProbe : ScriptComponent
{
    [Tooltip("歩く速さ m/s")]
    float walkSpeed = 4.0f;

    [Tooltip("通路を回る周の数")]
    int laps = 2;

    [Tooltip("歩き終えるまでに許す秒数")]
    float timeLimit = 60.0f;

    private string cratePrefab_ = "Application/Assets/Prefabs/ManyBoxCrate.prefab";
    private string ballPrefab_ = "Application/Assets/Prefabs/ManyBoxBall.prefab";

    private CharacterController@ controller_;
    private GameObject@ camera_;
    private array<Vector3> waypoints_ = { Vector3(0.0f, 0.0f, 16.0f), Vector3(16.0f, 0.0f, 16.0f),
                                          Vector3(16.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 0.0f) };
    private array<GameObject@> balls_;

    // 0 = 置く前・1 = 歩く・2 = 終わり
    private int phase_ = 0;
    private int target_ = 0;
    private int lap_ = 0;
    private float time_ = 0.0f;
    private float yaw_ = 0.0f;

    private int pillarShapes_ = 0;
    private int crates_ = 0;
    private int frames_ = 0;
    private int rayHits_ = 0;
    private int sphereHits_ = 0;
    private int boxHits_ = 0;
    private int failures_ = 0;

    void Start()
    {
        owner.GetComponent(@controller_);
        @camera_ = owner.FindObject("MainCamera");
        BuildPillars();
        PlaceCrates();
        DropBalls();
        Log("ManyBoxesProbe: 柱の箱 " + pillarShapes_ + " 個・別々の箱 " + crates_ + " 個・球 " + balls_.length() + " 個を置いた");
        phase_ = 1;
    }

    void Update()
    {
        if (controller_ is null || !controller_.exists) {
            return;
        }
        if (phase_ == 1) {
            time_ += Time::DeltaTime();
            Walk();
            Query();
            if (lap_ >= laps || time_ > timeLimit) {
                Finish();
            }
        } else {
            controller_.SimpleMove(Vector3(0.0f, 0.0f, 0.0f));
        }
        UpdateCamera();
        UpdateAnimator();
    }

    // ===== 置く =====

    // 柱の見た目（PillarField）と同じ並びで、Pillars に箱を足す
    private void BuildPillars()
    {
        GameObject@ pillars = owner.FindObject("Pillars");
        if (pillars is null || pillars.collider is null) {
            Warn("ManyBoxesProbe: Pillars のコライダーが見つからない");
            return;
        }
        Collider@ collider = pillars.collider;
        for (int i = 0; i < 30; ++i) {
            for (int j = 0; j < 30; ++j) {
                const float height = 1.0f + float((7 * i + 13 * j) % 5) * 0.4f;
                const Vector3 center = Vector3(-29.0f + 2.0f * float(i), height * 0.5f, -29.0f + 2.0f * float(j));
                collider.AddBox(Vector3(0.5f, height, 0.5f), CollisionLayer::Default, center);
            }
        }
        collider.isTrigger = false;
        collider.isStatic = true;
        pillarShapes_ = collider.count;
    }

    // 柱の奥に、別々の物の箱を 2 列に並べる
    private void PlaceCrates()
    {
        for (int row = 0; row < 2; ++row) {
            for (int k = 0; k < 50; ++k) {
                GameObject@ crate = owner.InstantiatePrefab(cratePrefab_, "Crate_" + (row * 50 + k));
                if (crate is null) {
                    continue;
                }
                crate.transform.position = Vector3(-29.4f + 1.2f * float(k), 0.4f, 33.0f + 2.0f * float(row));
                crate.transform.scale = Vector3(0.8f, 0.8f, 0.8f);
                ++crates_;
            }
        }
    }

    // 柱の上から剛体の球を落とす（柱の上・通路・床のどこかで止まる）
    private void DropBalls()
    {
        for (int b = 0; b < 20; ++b) {
            GameObject@ ball = owner.InstantiatePrefab(ballPrefab_, "Ball_" + b);
            if (ball is null) {
                continue;
            }
            ball.transform.position = Vector3(-13.4f + 7.0f * float(b % 5), 6.0f + 0.3f * float(b), -13.6f + 7.0f * float(b / 5));
            ball.transform.scale = Vector3(0.35f, 0.35f, 0.35f);
            balls_.insertLast(ball);
        }
    }

    // ===== 歩く =====

    private void Walk()
    {
        const Vector3 position = owner.transform.position;
        const Vector3 goal = waypoints_[target_];
        const float dx = goal.x - position.x;
        const float dz = goal.z - position.z;
        const float distance = sqrt(dx * dx + dz * dz);
        if (distance < 0.25f) {
            target_ = (target_ + 1) % int(waypoints_.length());
            if (target_ == 0) {
                ++lap_;
            }
            return;
        }
        const Vector3 direction = Vector3(dx / distance, 0.0f, dz / distance);
        controller_.SimpleMove(direction * walkSpeed);
        yaw_ = atan2(direction.x, direction.z);
        owner.transform.rotation = Vector3(0.0f, yaw_, 0.0f);
    }

    // レイ 8 本と重なりの問い合わせ（ゲームのスクリプトがよく投げる量）
    private void Query()
    {
        const Vector3 chest = owner.transform.position + Vector3(0.0f, 1.0f, 0.0f);
        for (int k = 0; k < 8; ++k) {
            const float angle = float(k) * 0.7853982f + 0.3f;
            const Vector3 direction = Vector3(sin(angle), 0.0f, cos(angle));
            // 自分のカプセルの外から撃つ
            RaycastHit@ hit = Physics::Raycast(chest + direction * 0.5f, direction, 20.0f);
            if (hit !is null) {
                ++rayHits_;
            }
        }
        array<GameObject@>@ nearSphere = Physics::OverlapSphere(chest, 2.0f);
        sphereHits_ += int(nearSphere.length());
        array<GameObject@>@ nearBox = Physics::OverlapBox(chest, Vector3(3.0f, 2.0f, 3.0f));
        boxHits_ += int(nearBox.length());
        ++frames_;
    }

    // ===== 結果 =====

    private void Finish()
    {
        phase_ = 2;
        Result(pillarShapes_ == 900 && crates_ == 100,
               "置いた数", "柱の箱 " + pillarShapes_ + "・別々の箱 " + crates_);
        Result(lap_ >= laps,
               "通路を歩く", lap_ + " 周・" + F(time_, 1) + " 秒");

        const float frames = float(frames_ > 0 ? frames_ : 1);
        Result(rayHits_ > 0 && sphereHits_ > 0 && boxHits_ > 0,
               "問い合わせ", frames_ + " フレーム・1 フレームあたり レイの当たり " + F(float(rayHits_) / frames, 2)
               + " / 8・OverlapSphere " + F(float(sphereHits_) / frames, 2) + "・OverlapBox " + F(float(boxHits_) / frames, 2));

        int resting = 0;
        float lowest = 1000.0f;
        for (uint b = 0; b < balls_.length(); ++b) {
            const float y = balls_[b].transform.position.y;
            lowest = Min(lowest, y);
            if (y > 0.3f && y < 4.0f) {
                ++resting;
            }
        }
        Result(resting == int(balls_.length()) && balls_.length() == 20,
               "落ちた球", "床か柱の上にある球 " + resting + " / " + balls_.length() + "・いちばん低い中心 " + F(lowest, 3) + " m");

        if (failures_ == 0) {
            Log("ManyBoxesProbe: 全部 OK");
        } else {
            Warn("ManyBoxesProbe: NG が " + failures_ + " 件");
        }
    }

    // ===== 見た目 =====

    private void UpdateCamera()
    {
        if (camera_ is null) {
            return;
        }
        const Vector3 target = owner.transform.position;
        camera_.transform.position = target + Vector3(-sin(yaw_) * 8.0f, 5.5f, -cos(yaw_) * 8.0f);
        camera_.transform.rotation = Vector3(0.5f, yaw_, 0.0f);
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

    // ===== 道具 =====

    private void Result(bool ok, const string &in name, const string &in text)
    {
        if (ok) {
            Log("ManyBoxesProbe: OK " + name + ": " + text);
        } else {
            ++failures_;
            Warn("ManyBoxesProbe: NG " + name + ": " + text);
        }
    }

    private float Min(float a, float b)
    {
        return a < b ? a : b;
    }

    private string F(float value, uint digits)
    {
        return formatFloat(value, "", 0, digits);
    }
}
