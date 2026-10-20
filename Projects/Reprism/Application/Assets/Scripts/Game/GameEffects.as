using namespace CoreEngine;

// ゲーム画面の演出（元の GameActionSoundController・CameraShakeController・SwitchParticleSystem・
// BlockDisappearEffect・EdgeInvasionController）

// 操作の効果音
class GameActionSoundController
{
    void PlayChangeToHat()
    {
        PlaySe("SE_Change.mp3");
        PlaySe("SE_Change2.mp3");
    }

    void PlayChangeToPlayer()
    {
        PlaySe("SE_Change.mp3");
        PlaySe("SE_Change2.mp3");
    }

    void PlayError() { PlaySe("SE_Error.mp3"); }
    void PlayGoal() { PlaySe("SE_Goal.wav"); }
    void PlayJump() { PlaySe("SE_Jump.wav"); }
    void PlayDead() { PlaySe("SE_Dead.mp3"); }
    void PlayUndo() { PlaySe("SE_Undo.mp3"); }
}

// カメラの揺れ。揺れている間だけカメラの位置をずらし、終わったら原点へ戻す
class CameraShakeController
{
    private float time_ = 0.0f;
    private float duration_ = 0.0f;
    private float strength_ = 0.0f;
    Vector2 offset;

    void Start(float duration, float strength)
    {
        duration_ = duration;
        time_ = duration;
        strength_ = strength;
    }

    void Update(float dt)
    {
        if (time_ <= 0.0f) {
            return;
        }
        time_ -= dt;
        if (time_ <= 0.0f) {
            time_ = 0.0f;
            offset = Vector2(0.0f, 0.0f);
            return;
        }
        // 終わりに向かって振れ幅を 0 へ
        const float decay = 1.0f - (time_ / duration_);
        const float amp = strength_ * (1.0f - decay);
        offset = Vector2(Td3RandSigned() * amp, Td3RandSigned() * amp);
    }
}

// 操作を切り替えたとき、元から先へまっすぐ飛ぶ粒（加算）
class SwitchParticle
{
    Td3Sprite@ sprite;
    Vector2 startPos;
    Vector2 endPos;
    float age = 0.0f;
    float lifetime = 0.0f;
    float startSize = 0.0f;
    float rotationSpeed = 0.0f;
    float selfRotation = 0.0f;
}

class SwitchParticleSystem
{
    Td3SpritePool@ pool;
    private array<SwitchParticle@> particles_;

    void Spawn(const Vector2 &in from, const Vector2 &in to)
    {
        for (int i = 0; i < 10; ++i) {
            const float size = Random::Range(10.0f, 18.0f);
            const float life = Random::Range(0.22f, 0.38f);

            Td3Sprite@ sprite = pool.Acquire();
            sprite.SetAnchor(Vector2(0.5f, 0.5f));
            sprite.SetBlendMode(kBlendAdd);
            sprite.SetSortingLayer(3);
            sprite.SetColor(Vector4(1.0f, 1.0f, 1.0f, 1.0f));
            sprite.SetScale(size, size);
            sprite.SetRotateZ(0.0f);
            sprite.SetTranslate(from);

            SwitchParticle p;
            @p.sprite = sprite;
            p.startPos = from;
            p.endPos = to;
            p.lifetime = life;
            p.startSize = size;
            p.rotationSpeed = Random::Range(-6.0f, 6.0f);
            particles_.insertLast(p);
        }
    }

    void Update(float dt)
    {
        for (int i = int(particles_.length()) - 1; i >= 0; --i) {
            SwitchParticle@ p = particles_[uint(i)];
            p.age += dt;
            const float t = (p.lifetime > 0.0f) ? Clamp(p.age / p.lifetime, 0.0f, 1.0f) : 1.0f;
            const float ease = Td3EaseApply(t, Td3Ease::EaseInQuad);
            p.sprite.SetTranslate(p.startPos.x + (p.endPos.x - p.startPos.x) * ease,
                p.startPos.y + (p.endPos.y - p.startPos.y) * ease);
            p.selfRotation += p.rotationSpeed * dt;
            p.sprite.SetRotateZ(p.selfRotation);
            // 後半で縮みながら消える
            const float fade = (t < 0.5f) ? 1.0f : 1.0f - Td3EaseApply((t - 0.5f) / 0.5f, Td3Ease::EaseInQuad);
            const float size = p.startSize * fade;
            p.sprite.SetScale(size, size);
            p.sprite.SetColor(Vector4(1.0f, 1.0f, 1.0f, fade));
            if (p.age >= p.lifetime) {
                pool.Release(p.sprite);
                particles_.removeAt(uint(i));
            }
        }
    }
}

// ハットの置いたブロックが消えるとき、縮む板と飛び散る粒（ハットの色・加算）
class DisappearingBlock
{
    Td3Sprite@ sprite;
    float age = 0.0f;
    float baseScale = 64.0f;
}

class BlockDisappearParticle
{
    Td3Sprite@ sprite;
    Vector2 startPos;
    Vector2 velocity;
    float age = 0.0f;
    float lifetime = 0.0f;
    float startSize = 0.0f;
    float rotationSpeed = 0.0f;
    float selfRotation = 0.0f;
}

class BlockDisappearEffect
{
    Td3SpritePool@ blockPool;
    Td3SpritePool@ particlePool;
    private array<DisappearingBlock@> blocks_;
    private array<BlockDisappearParticle@> particles_;

    void StartDisappear(const Vector2 &in worldPos)
    {
        Td3Sprite@ sprite = blockPool.Acquire();
        sprite.SetAnchor(Vector2(0.5f, 0.5f));
        sprite.SetBlendMode(kBlendAdd);
        sprite.SetSortingLayer(2);
        sprite.SetColor(Vector4(kNeonHatColorRGB.x, kNeonHatColorRGB.y, kNeonHatColorRGB.z, 1.0f));
        sprite.SetScale(64.0f, 64.0f);
        sprite.SetTranslate(worldPos);

        DisappearingBlock block;
        @block.sprite = sprite;
        blocks_.insertLast(block);

        SpawnParticles(worldPos);
    }

    private void SpawnParticles(const Vector2 &in pos)
    {
        for (int i = 0; i < 8; ++i) {
            const float angle = Random::Range(0.0f, 6.28318530718f);
            const float speed = Random::Range(80.0f, 150.0f);
            const float size = Random::Range(6.0f, 12.0f);
            const float life = Random::Range(0.3f, 0.5f);

            Td3Sprite@ sprite = particlePool.Acquire();
            sprite.SetAnchor(Vector2(0.5f, 0.5f));
            sprite.SetBlendMode(kBlendAdd);
            sprite.SetSortingLayer(3);
            sprite.SetColor(Vector4(kNeonHatColorRGB.x, kNeonHatColorRGB.y, kNeonHatColorRGB.z, 1.0f));
            sprite.SetScale(size, size);
            sprite.SetRotateZ(0.0f);
            sprite.SetTranslate(pos);

            BlockDisappearParticle p;
            @p.sprite = sprite;
            p.startPos = pos;
            p.velocity = Vector2(cos(angle) * speed, sin(angle) * speed);
            p.lifetime = life;
            p.startSize = size;
            p.rotationSpeed = Random::Range(-8.0f, 8.0f);
            particles_.insertLast(p);
        }
    }

    void Update(float dt)
    {
        for (int i = 0; i < int(blocks_.length()); ) {
            DisappearingBlock@ b = blocks_[uint(i)];
            b.age += dt;
            const float t = Clamp(b.age / 0.25f, 0.0f, 1.0f);
            const float ease = Td3EaseApply(t, Td3Ease::EaseInQuad);
            const float scale = b.baseScale * (1.0f - ease);
            b.sprite.SetScale(scale, scale);
            b.sprite.SetColor(Vector4(kNeonHatColorRGB.x, kNeonHatColorRGB.y, kNeonHatColorRGB.z, 1.0f - ease));
            if (b.age >= 0.25f) {
                blockPool.Release(b.sprite);
                blocks_.removeAt(uint(i));
            } else {
                ++i;
            }
        }

        for (int i = int(particles_.length()) - 1; i >= 0; --i) {
            BlockDisappearParticle@ p = particles_[uint(i)];
            p.age += dt;
            const float t = Clamp(p.age / p.lifetime, 0.0f, 1.0f);
            // 進むほど遅くなる
            const float velocityRatio = 1.0f - t * 0.5f;
            p.sprite.SetTranslate(p.startPos.x + p.velocity.x * p.age * velocityRatio,
                p.startPos.y + p.velocity.y * p.age * velocityRatio);
            p.selfRotation += p.rotationSpeed * dt;
            p.sprite.SetRotateZ(p.selfRotation);
            const float fade = (t < 0.5f) ? 1.0f : 1.0f - Td3EaseApply((t - 0.5f) / 0.5f, Td3Ease::EaseInQuad);
            const float size = p.startSize * fade;
            p.sprite.SetScale(size, size);
            p.sprite.SetColor(Vector4(kNeonHatColorRGB.x, kNeonHatColorRGB.y, kNeonHatColorRGB.z, fade));
            if (p.age >= p.lifetime) {
                particlePool.Release(p.sprite);
                particles_.removeAt(uint(i));
            }
        }
    }
}

enum EdgeInvasionPhase
{
    Idle,
    Invading,
    Holding,
    Retreating
}

// 操作を切り替えたとき、画面の縁から操作する側の色の線が伸びて消える（ポストエフェクト EdgeInvasion）。
// エンジン側とゲーム側の両方から進めるので、1 フレームに 2 回進む
class EdgeInvasionController
{
    private EdgeInvasionPhase phase_ = EdgeInvasionPhase::Idle;
    private Td3Timer invasionTimer_;
    private Td3Timer holdTimer_;
    private Td3Timer retreatTimer_;
    private Vector4 color_ = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
    private float progress_ = 0.0f;
    private int prevMode_ = 0;

    void Setup(int initialMode)
    {
        prevMode_ = initialMode;
        phase_ = EdgeInvasionPhase::Idle;
        PostEffect::SetEnabled("EdgeInvasion", false);
    }

    // mode は 0 = 自機を操作中 ／ 1 = ハットを操作中
    void Update(float dt, int mode)
    {
        Step(dt);
        Step(dt);
        if (mode != prevMode_) {
            const Vector3 rgb = (mode == 1) ? kNeonHatColorRGB : kNeonPlayerColorRGB;
            StartInvasion(rgb);
            prevMode_ = mode;
        }
    }

    private void StartInvasion(const Vector3 &in rgb)
    {
        color_ = Vector4(rgb.x, rgb.y, rgb.z, 1.0f);
        progress_ = 0.0f;
        phase_ = EdgeInvasionPhase::Invading;
        invasionTimer_.Start(0.35f);
        holdTimer_.Start(0.10f);
        holdTimer_.Stop();
        retreatTimer_.Start(1.0f);
        retreatTimer_.Stop();
        PostEffect::SetEnabled("EdgeInvasion", true);
        Apply();
    }

    private void Step(float dt)
    {
        if (phase_ == EdgeInvasionPhase::Idle) {
            return;
        }
        if (phase_ == EdgeInvasionPhase::Invading) {
            invasionTimer_.Update(dt);
            progress_ = invasionTimer_.GetEasedProgress(Td3Ease::EaseOutQuad);
            if (invasionTimer_.IsFinished()) {
                progress_ = 1.0f;
                color_.w = 1.0f;
                phase_ = EdgeInvasionPhase::Holding;
                holdTimer_.Start(holdTimer_.GetDuration());
            }
        } else if (phase_ == EdgeInvasionPhase::Holding) {
            holdTimer_.Update(dt);
            progress_ = 1.0f;
            if (holdTimer_.IsFinished()) {
                phase_ = EdgeInvasionPhase::Retreating;
                retreatTimer_.Start(retreatTimer_.GetDuration());
            }
        } else if (phase_ == EdgeInvasionPhase::Retreating) {
            retreatTimer_.Update(dt);
            // 伸びきったまま薄くなって消える
            progress_ = 1.0f;
            color_.w = 1.0f - retreatTimer_.GetEasedProgress(Td3Ease::EaseOutQuad);
            if (retreatTimer_.IsFinished()) {
                progress_ = 0.0f;
                color_.w = 0.0f;
                phase_ = EdgeInvasionPhase::Idle;
                PostEffect::SetEnabled("EdgeInvasion", false);
            }
        }
        Apply();
    }

    private void Apply()
    {
        ShaderMaterial@ material = PostEffect::GetMaterial("EdgeInvasion");
        if (material is null) {
            return;
        }
        material.SetVector("invasionColor", color_);
        material.SetFloat("progress", progress_);
    }
}
