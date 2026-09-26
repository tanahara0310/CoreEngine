// シューターの進行：敵を出し、スコアを数える。
// スクリプトの API（当たり判定・プレハブ・パーティクル・3D テキスト）の確かめを兼ね、集計を一定の間隔でログへ出す
[DisplayName("シューター：進行")]
class ShooterDirector : ScriptComponent
{
    [Asset("Prefab")] [Tooltip("出す敵のプレハブ")]
    string enemyPrefab = "Application/Assets/Prefabs/ScriptShooterTest/Enemy.prefab";

    [Range(0.2f, 10.0f)] [Tooltip("敵を出す間隔（秒）")]
    float spawnInterval = 1.0f;

    [Tooltip("敵を出す奥行き")]
    float spawnDistance = 40.0f;

    [Tooltip("敵を出す横幅")]
    float spawnWidth = 18.0f;

    [Range(1.0f, 60.0f)] [Tooltip("集計をログへ出す間隔（秒）")]
    float reportInterval = 10.0f;

    private float spawnTimer_ = 0.0f;
    private float reportTimer_ = 0.0f;
    private float elapsed_ = 0.0f;
    private int score_ = 0;
    private int spawned_ = 0;
    private int kills_ = 0;
    private int hits_ = 0;
    private int shots_ = 0;
    private int blocked_ = 0;
    private int breaches_ = 0;

    void Awake()
    {
        // 弾と敵、敵と砲台が当たるようにする（既定では Default レイヤーだけが全レイヤーと当たる）
        Physics::SetLayerCollision(CollisionLayer::PlayerBullet, CollisionLayer::Enemy, true);
        Physics::SetLayerCollision(CollisionLayer::Enemy, CollisionLayer::Player, true);
    }

    void Start()
    {
        GameCamera@ camera = Scene::GetGameCamera();
        if (camera !is null && camera.exists) {
            camera.position = Vector3(0.0f, 26.0f, -22.0f);
            camera.LookAt(Vector3(0.0f, 0.0f, 16.0f));
        }
        UpdateScoreBoard();
        Log("シューター: 開始（弾と敵が当たる=" + Physics::GetLayerCollision(CollisionLayer::PlayerBullet, CollisionLayer::Enemy)
            + " / 敵と砲台が当たる=" + Physics::GetLayerCollision(CollisionLayer::Enemy, CollisionLayer::Player) + "）");
    }

    void Update()
    {
        const float dt = Time::DeltaTime();
        elapsed_ += dt;

        spawnTimer_ += dt;
        if (spawnTimer_ >= spawnInterval) {
            spawnTimer_ -= spawnInterval;
            Spawn();
        }

        reportTimer_ += dt;
        if (reportTimer_ >= reportInterval) {
            reportTimer_ -= reportInterval;
            Log("シューター: 経過 " + int(elapsed_) + " 秒 / 敵 " + spawned_ + " / 撃った弾 " + shots_
                + " / 命中 " + hits_ + " / 撃破 " + kills_ + " / 壁で見送り " + blocked_
                + " / 突破 " + breaches_ + " / スコア " + score_);
        }
    }

    // 弾が敵に当たった（倒せなくても数える）
    void CountHit()
    {
        ++hits_;
    }

    // 敵を倒した
    void AddScore(int points)
    {
        score_ += points;
        ++kills_;
        UpdateScoreBoard();
    }

    // 弾を撃った
    void CountShot()
    {
        ++shots_;
    }

    // 壁に遮られて撃たなかった
    void CountBlocked()
    {
        ++blocked_;
    }

    // 敵が砲台まで来た
    void CountBreach()
    {
        ++breaches_;
    }

    private void Spawn()
    {
        GameObject@ enemy = owner.InstantiatePrefab(enemyPrefab, "Enemy");
        if (enemy is null) {
            Warn("シューター: 敵のプレハブを読めませんでした: " + enemyPrefab);
            return;
        }
        const float x = Random::Range(-spawnWidth * 0.5f, spawnWidth * 0.5f);
        enemy.transform.position = Vector3(x, 1.0f, spawnDistance);
        ++spawned_;
    }

    private void UpdateScoreBoard()
    {
        GameObject@ board = owner.FindObject("ScoreBoard");
        if (board !is null) {
            board.text3DRenderer.text = "SCORE " + score_;
        }
    }
}
