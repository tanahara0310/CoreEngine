// 演出：シーンに置いたパーティクルを出す場所へ動かして粒を出し、カメラを揺らす
[DisplayName("シューティング：演出")]
class ShootingEffects : ScriptComponent
{
    [ObjectRef] [Tooltip("爆発のパーティクル")]
    GameObject@ explosion;

    [ObjectRef] [Tooltip("火花のパーティクル")]
    GameObject@ spark;

    [Tooltip("敵が爆発したときのカメラの揺れの強さ")]
    float enemyShake = 0.3f;

    // 敵の爆発
    void EnemyExplosion(const Vector3 &in position, int count)
    {
        Emit(explosion, position, count);
        Emit(spark, position, count / 3);
        CameraShake::PlayPreset("Hit", enemyShake);
    }

    // 弾が当たったが倒れなかったときの火花
    void HitSpark(const Vector3 &in position)
    {
        Emit(spark, position, 10);
    }

    // 自機が被弾したとき
    void PlayerHit(const Vector3 &in position)
    {
        Emit(spark, position, 40);
        Emit(explosion, position, 24);
        CameraShake::PlayPreset("HeavyHit", 0.8f);
    }

    // 自機が落ちたとき
    void PlayerExplosion(const Vector3 &in position)
    {
        Emit(explosion, position, 180);
        Emit(spark, position, 80);
        CameraShake::PlayPreset("Explosion", 1.2f);
    }

    private void Emit(GameObject@ emitter, const Vector3 &in position, int count)
    {
        if (emitter is null || count <= 0) {
            return;
        }
        // 出す位置へ動かし、行列を送り直してから粒を出す
        emitter.transform.position = position;
        emitter.transform.UpdateMatrix();
        ParticleSystem@ particles = emitter.particleSystem;
        if (particles.exists) {
            particles.Emit(count);
        }
    }
}
