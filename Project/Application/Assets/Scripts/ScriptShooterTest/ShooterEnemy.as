// 敵：砲台へ向かって進む。弾に当たると光り、決めた回数当たるとパーティクルを出して消える
[DisplayName("シューター：敵")]
class ShooterEnemy : ScriptComponent
{
    [Tooltip("進む速さ")]
    float speed = 5.0f;

    [Tooltip("倒れるまでに当てる回数")]
    int hitPoints = 2;

    [Tooltip("倒したときのスコア")]
    int points = 100;

    [Asset("Prefab")] [Tooltip("当たったときに出すパーティクルのプレハブ")]
    string hitEffectPrefab = "Application/Assets/Prefabs/ScriptShooterTest/HitEffect.prefab";

    private float flash_ = 0.0f;
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
        const float dt = Time::DeltaTime();
        Vector3 position = owner.transform.position;
        position.z -= speed * dt;
        owner.transform.position = position;

        if (flash_ > 0.0f) {
            flash_ -= dt;
            const float glow = flash_ > 0.0f ? flash_ * 4.0f : 0.0f;
            owner.material.emissive = Vector4(glow, glow, glow, 1.0f);
        }
    }

    void OnTriggerEnter(Collision@ other)
    {
        if (other.layer == CollisionLayer::Player) {
            // 砲台まで来た
            if (director_ !is null) {
                director_.CountBreach();
            }
            owner.Destroy();
            return;
        }
        if (other.layer != CollisionLayer::PlayerBullet) {
            return;
        }
        if (director_ !is null) {
            director_.CountHit();
        }
        --hitPoints;
        if (hitPoints > 0) {
            flash_ = 0.25f;
            owner.material.emissive = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
            SpawnEffect(12);
            return;
        }
        SpawnEffect(48);
        if (director_ !is null) {
            director_.AddScore(points);
        }
        owner.Destroy();
    }

    private void SpawnEffect(int count)
    {
        GameObject@ effect = owner.InstantiatePrefab(hitEffectPrefab, "HitEffect");
        if (effect is null) {
            return;
        }
        effect.transform.position = owner.transform.position;
        ShooterHitEffect@ sparks;
        if (effect.GetComponent(@sparks)) {
            sparks.count = count;
        }
    }
}
