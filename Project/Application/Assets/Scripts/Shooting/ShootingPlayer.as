// 自機：移動・ショット・被弾。残機が尽きたら進行へ知らせる
[DisplayName("シューティング：自機")]
class ShootingPlayer : ScriptComponent
{
    [ObjectRef] [Tooltip("胴体（被弾後の点滅と、落ちたときに隠す見た目）")]
    GameObject@ body;

    [ObjectRef] [Tooltip("翼")]
    GameObject@ wing;

    [ObjectRef] [Tooltip("操縦席")]
    GameObject@ cockpit;

    [ObjectRef] [Tooltip("エンジンの炎（明滅させる）")]
    GameObject@ engine;

    [Asset("Prefab")] [Tooltip("撃つ弾")]
    string bulletPrefab = "Application/Assets/Prefabs/Shooting/PlayerBullet.prefab";

    [Tooltip("移動の速さ")]
    float moveSpeed = 14.0f;

    [Range(0.03f, 1.0f)] [Tooltip("弾を撃つ間隔（秒）")]
    float fireInterval = 0.11f;

    [Tooltip("弾の速さ")]
    float bulletSpeed = 34.0f;

    [Tooltip("左右の弾の間隔の半分")]
    float gunSpacing = 0.55f;

    [Range(1, 9)] [Tooltip("残機")]
    int lives = 3;

    [Tooltip("被弾した後に当たらない秒数")]
    float invincibleTime = 2.0f;

    [Tooltip("動ける範囲の最小（X と Z）")]
    Vector2 areaMin = Vector2(-16.0f, -8.0f);

    [Tooltip("動ける範囲の最大（X と Z）")]
    Vector2 areaMax = Vector2(16.0f, 10.0f);

    [Tooltip("左右へ動いたときに傾ける角度（ラジアン）")]
    float bankAngle = 0.5f;

    private ShootingGameDirector@ director_;
    private ShootingEffects@ effects_;
    private float cooldown_ = 0.0f;
    private float invincible_ = 0.0f;
    private bool dead_ = false;
    private float roll_ = 0.0f;
    private float time_ = 0.0f;
    private Vector3 engineScale_;

    void Start()
    {
        GameObject@ director = owner.FindObject("GameDirector");
        if (director !is null) {
            director.GetComponent(@director_);
        }
        GameObject@ effects = owner.FindObject("Effects");
        if (effects !is null) {
            effects.GetComponent(@effects_);
        }
        if (engine !is null) {
            engineScale_ = engine.transform.scale;
        }
    }

    void Update()
    {
        if (dead_) {
            return;
        }
        const float dt = Time::DeltaTime();
        time_ += dt;
        FlickerEngine();

        const bool canMove = director_ is null || director_.CanMove();
        const Vector2 move = canMove
            ? Input::GetAxis2D(InputAction::MoveLeft, InputAction::MoveRight, InputAction::MoveBack, InputAction::MoveForward)
            : Vector2(0.0f, 0.0f);

        Vector3 position = owner.transform.position + Vector3(move.x, 0.0f, move.y) * (moveSpeed * dt);
        position.x = Clamp(position.x, areaMin.x, areaMax.x);
        position.z = Clamp(position.z, areaMin.y, areaMax.y);
        owner.transform.position = position;

        // 左右へ動いた向きへ機体を傾ける
        roll_ = Lerp(roll_, -move.x * bankAngle, Saturate(dt * 10.0f));
        owner.transform.rotation = Vector3(0.0f, 0.0f, roll_);

        cooldown_ -= dt;
        const bool canFire = director_ is null || director_.IsPlaying();
        if (canFire && cooldown_ <= 0.0f && Input::IsActionPressed(InputAction::Fire)) {
            cooldown_ = fireInterval;
            Fire(-gunSpacing);
            Fire(gunSpacing);
        }

        if (invincible_ > 0.0f) {
            invincible_ -= dt;
            SetVisible(invincible_ <= 0.0f || int(invincible_ * 14.0f) % 2 == 0);
        }
    }

    void OnTriggerEnter(Collision@ other)
    {
        if (dead_ || invincible_ > 0.0f) {
            return;
        }
        if (director_ !is null && !director_.IsPlaying()) {
            return;
        }
        if (other.layer == CollisionLayer::Enemy || other.layer == CollisionLayer::EnemyBullet) {
            TakeHit();
        }
    }

    // 落ちていないか
    bool IsAlive() const
    {
        return !dead_;
    }

    private void TakeHit()
    {
        --lives;
        const Vector3 position = owner.transform.worldPosition;
        if (lives <= 0) {
            lives = 0;
            dead_ = true;
            SetVisible(false);
            owner.collider.enabled = false;
            if (effects_ !is null) {
                effects_.PlayerExplosion(position);
            }
            if (director_ !is null) {
                director_.OnPlayerDestroyed();
            }
            return;
        }
        invincible_ = invincibleTime;
        if (effects_ !is null) {
            effects_.PlayerHit(position);
        }
        if (director_ !is null) {
            director_.OnPlayerHit(lives);
        }
    }

    private void Fire(float offsetX)
    {
        GameObject@ bullet = owner.InstantiatePrefab(bulletPrefab, "PlayerBullet");
        if (bullet is null) {
            return;
        }
        bullet.transform.position = owner.transform.worldPosition + Vector3(offsetX, 0.0f, 1.1f);
        ShootingBullet@ shot;
        if (bullet.GetComponent(@shot)) {
            shot.velocity = Vector3(0.0f, 0.0f, bulletSpeed);
        }
    }

    // 見た目の部品をまとめて出し入れする
    private void SetVisible(bool visible)
    {
        array<GameObject@> parts = { body, wing, cockpit, engine };
        for (uint i = 0; i < parts.length(); ++i) {
            if (parts[i] !is null) {
                parts[i].meshRenderer.enabled = visible;
            }
        }
    }

    // エンジンの炎の大きさを細かく揺らす
    private void FlickerEngine()
    {
        if (engine is null) {
            return;
        }
        const float pulse = 1.0f + 0.18f * sin(time_ * 47.0f) + 0.08f * sin(time_ * 23.0f);
        engine.transform.scale = Vector3(engineScale_.x * pulse, engineScale_.y * pulse, engineScale_.z * (0.9f + 0.4f * pulse));
    }
}
