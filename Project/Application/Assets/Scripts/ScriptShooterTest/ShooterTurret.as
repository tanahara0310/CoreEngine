// 砲台：いちばん近い敵を探し、壁に遮られていなければ弾を撃つ
[DisplayName("シューター：砲台")]
class ShooterTurret : ScriptComponent
{
    [Asset("Prefab")] [Tooltip("撃つ弾のプレハブ")]
    string bulletPrefab = "Application/Assets/Prefabs/ScriptShooterTest/Bullet.prefab";

    [Range(0.05f, 2.0f)] [Tooltip("撃つ間隔（秒）")]
    float fireInterval = 0.25f;

    [Tooltip("敵を探す距離")]
    float range = 45.0f;

    [Tooltip("弾の速さ")]
    float bulletSpeed = 32.0f;

    private float cooldown_ = 0.0f;
    private ShooterDirector@ director_;

    void Start()
    {
        GameObject@ director = owner.FindObject("Director");
        if (director !is null) {
            director.GetComponent(@director_);
        }
    }

    void Update()
    {
        cooldown_ -= Time::DeltaTime();
        if (cooldown_ > 0.0f) {
            return;
        }
        GameObject@ target = FindNearestEnemy();
        if (target is null) {
            return;
        }
        const Vector3 origin = owner.transform.worldPosition;
        const Vector3 toTarget = target.transform.worldPosition - origin;

        // 壁（Environment）が敵より手前にあれば撃たない
        RaycastHit@ block = Physics::Raycast(origin, toTarget, Length(toTarget), Physics::LayerMask(CollisionLayer::Environment));
        cooldown_ = fireInterval;
        if (block !is null) {
            if (director_ !is null) {
                director_.CountBlocked();
            }
            return;
        }
        Fire(Normalize(toTarget));
    }

    private GameObject@ FindNearestEnemy()
    {
        const Vector3 origin = owner.transform.worldPosition;
        array<GameObject@>@ enemies = Physics::OverlapSphere(origin, range, Physics::LayerMask(CollisionLayer::Enemy));
        GameObject@ nearest = null;
        float best = range * range;
        for (uint i = 0; i < enemies.length(); ++i) {
            const float distanceSq = LengthSquared(enemies[i].transform.worldPosition - origin);
            if (distanceSq < best) {
                best = distanceSq;
                @nearest = enemies[i];
            }
        }
        return nearest;
    }

    private void Fire(const Vector3 &in direction)
    {
        GameObject@ bullet = owner.InstantiatePrefab(bulletPrefab, "Bullet");
        if (bullet is null) {
            return;
        }
        bullet.transform.position = owner.transform.worldPosition + direction * 1.5f;
        ShooterBullet@ shot;
        if (bullet.GetComponent(@shot)) {
            shot.velocity = direction * bulletSpeed;
        }
        if (director_ !is null) {
            director_.CountShot();
        }
    }
}
