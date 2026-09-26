// 敵：決まった動き方で手前へ進む。弾が当たると光り、体力が尽きると爆発して点数を渡す
[DisplayName("シューティング：敵")]
class ShootingEnemy : ScriptComponent
{
    [Range(0, 2)] [Tooltip("動き方（0 = まっすぐ、1 = 左右に揺れる、2 = 止まって撃つ）")]
    int movement = 0;

    [Tooltip("手前へ進む速さ")]
    float speed = 7.0f;

    [Tooltip("倒れるまでに当てる回数")]
    int hitPoints = 1;

    [Tooltip("倒したときの点数")]
    int points = 100;

    [Tooltip("左右に揺れる幅（動き方 1）")]
    float waveWidth = 3.0f;

    [Tooltip("1 秒あたりの左右の往復の回数（動き方 1）")]
    float waveSpeed = 0.5f;

    [Tooltip("止まる奥行き（動き方 2）")]
    float stopZ = 12.0f;

    [Tooltip("止まる奥行きのばらつき（前後にこの幅だけずらす。動き方 2）")]
    float stopZJitter = 3.0f;

    [Tooltip("止まっている秒数（動き方 2）")]
    float stayTime = 4.0f;

    [Tooltip("弾を撃つ間隔（秒。0 なら撃たない）")]
    float fireInterval = 0.0f;

    [Tooltip("弾の速さ")]
    float bulletSpeed = 10.0f;

    [Asset("Prefab")] [Tooltip("撃つ弾")]
    string bulletPrefab = "Application/Assets/Prefabs/Shooting/EnemyBullet.prefab";

    [Tooltip("1 秒あたりの回転（ラジアン）")]
    Vector3 spin = Vector3(0.0f, 1.5f, 0.0f);

    [Tooltip("倒れたときの爆発の粒の数")]
    int explosionSize = 40;

    [Tooltip("これより手前へ来たら消す")]
    float despawnZ = -16.0f;

    private ShootingGameDirector@ director_;
    private ShootingEffects@ effects_;
    private bool initialized_ = false;
    private bool dead_ = false;
    private float baseX_ = 0.0f;
    private float waveSign_ = 1.0f;
    private float time_ = 0.0f;
    private float flash_ = 0.0f;
    private int gunnerPhase_ = 0;
    private float stayTimer_ = 0.0f;
    private float fireTimer_ = 0.0f;
    private Vector4 baseEmissive_;

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
        baseEmissive_ = owner.material.emissive;
        waveSign_ = Random::Chance(0.5f) ? 1.0f : -1.0f;
        stopZ += Random::Range(-stopZJitter, stopZJitter);
        fireTimer_ = fireInterval * 0.5f;
    }

    void Update()
    {
        if (dead_) {
            return;
        }
        const float dt = Time::DeltaTime();
        Vector3 position = owner.transform.position;
        // 出す側が置いた位置を、最初の更新で揺れの中心として控える
        if (!initialized_) {
            initialized_ = true;
            baseX_ = position.x;
        }
        time_ += dt;

        if (movement == 1) {
            position.z -= speed * dt;
            position.x = baseX_ + sin(time_ * waveSpeed * 6.2831855f) * waveWidth * waveSign_;
        } else if (movement == 2) {
            position = UpdateGunner(position, dt);
        } else {
            position.z -= speed * dt;
        }
        owner.transform.position = position;
        owner.transform.rotation = owner.transform.rotation + spin * dt;

        if (flash_ > 0.0f) {
            flash_ -= dt;
            if (flash_ <= 0.0f) {
                owner.material.emissive = baseEmissive_;
            }
        }

        if (position.z < despawnZ) {
            dead_ = true;
            owner.Destroy();
        }
    }

    void OnTriggerEnter(Collision@ other)
    {
        if (dead_) {
            return;
        }
        if (other.layer == CollisionLayer::Player) {
            Explode();
            return;
        }
        if (other.layer != CollisionLayer::PlayerBullet) {
            return;
        }

        --hitPoints;
        if (hitPoints > 0) {
            flash_ = 0.08f;
            owner.material.emissive = Vector4(1.5f, 1.5f, 1.5f, 1.0f);
            if (effects_ !is null) {
                effects_.HitSpark(owner.transform.worldPosition);
            }
            return;
        }
        if (director_ !is null) {
            director_.AddKill(points, owner.transform.worldPosition);
        }
        Explode();
    }

    // 爆発して消える（点数は渡さない）
    void Explode()
    {
        if (dead_) {
            return;
        }
        dead_ = true;
        if (effects_ !is null) {
            effects_.EnemyExplosion(owner.transform.worldPosition, explosionSize);
        }
        owner.Destroy();
    }

    // 止まって撃つ敵：止まる奥行きまで進む → 左右に揺れながら撃つ → 手前へ抜ける（動かした後の位置を返す）
    private Vector3 UpdateGunner(Vector3 position, float dt)
    {
        if (gunnerPhase_ == 0) {
            position.z = Max(position.z - speed * dt, stopZ);
            if (position.z <= stopZ) {
                gunnerPhase_ = 1;
            }
            return position;
        }
        if (gunnerPhase_ == 1) {
            stayTimer_ += dt;
            position.x = baseX_ + sin(stayTimer_ * 1.5f) * 1.5f;
            fireTimer_ -= dt;
            if (fireInterval > 0.0f && fireTimer_ <= 0.0f) {
                fireTimer_ = fireInterval;
                Fire(position);
            }
            if (stayTimer_ >= stayTime) {
                gunnerPhase_ = 2;
            }
            return position;
        }
        position.z -= speed * 1.8f * dt;
        return position;
    }

    // 自機のいる方へ弾を撃つ
    private void Fire(const Vector3 &in origin)
    {
        if (director_ is null || !director_.IsPlaying()) {
            return;
        }
        Vector3 direction = director_.GetPlayerPosition() - origin;
        direction.y = 0.0f;
        if (LengthSquared(direction) < 0.0001f) {
            direction = Vector3(0.0f, 0.0f, -1.0f);
        }
        direction = Normalize(direction);

        GameObject@ bullet = owner.InstantiatePrefab(bulletPrefab, "EnemyBullet");
        if (bullet is null) {
            return;
        }
        bullet.transform.position = origin + direction * 1.4f;
        ShootingBullet@ shot;
        if (bullet.GetComponent(@shot)) {
            shot.velocity = direction * bulletSpeed;
        }
    }
}
