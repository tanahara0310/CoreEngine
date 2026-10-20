using namespace CoreEngine;

// 画面の大きさ（元はウィンドウの大きさ。移植では 1920×1080 に固定）
const float kScreenWidth = 1920.0f;
const float kScreenHeight = 1080.0f;

const string kBgGridMaterial = "Application/Assets/Materials/BgGrid.mat";

// 元の CyberGridBackground：画面を覆う板に、奥へ流れる格子を加算で描く
class CyberGridBackground
{
    private Td3Sprite@ sprite_;
    private ShaderMaterial@ material_;
    private float time_ = 0.0f;

    void Initialize(Td3SpriteFactory@ factory, const string &in name)
    {
        @sprite_ = factory.Create("white1x1.png", name);
        if (sprite_ is null) {
            return;
        }
        sprite_.SetBlendMode(kBlendAdd);
        sprite_.SetSortingLayer(-9);
        sprite_.SetAnchor(Vector2(0.5f, 0.5f));
        sprite_.SetScale(1.0f, 1.0f);
        sprite_.SetTranslate(0.0f, 0.0f);
        sprite_.SetMaterial(kBgGridMaterial);
        @material_ = ShaderMaterial(kBgGridMaterial);
    }

    // カメラのズームと位置に合わせて画面全体を覆う
    void Update(float dt, float zoom, const Vector2 &in cameraPosition)
    {
        if (material_ is null) {
            return;
        }
        time_ += dt;
        material_.SetFloat("time", time_);
        if (sprite_ is null) {
            return;
        }
        sprite_.SetScale(kScreenWidth / zoom, kScreenHeight / zoom);
        sprite_.SetTranslate(cameraPosition);
    }
}

// 元の GameBackgroundEffect：下から湧いて揺れながら昇る四角い粒（加算）
class BackgroundParticle
{
    Td3Sprite@ sprite;
    Vector2 startPosition;
    Vector2 velocity;
    Vector2 scale;
    Vector4 color;
    float swayAmplitude = 0.0f;
    float swayPhase = 0.0f;
    Vector3 rotation;
    Vector3 rotationSpeed;
    float age = 0.0f;
    float lifetime = 1.0f;
}

class GameBackgroundEffect
{
    private array<BackgroundParticle@> particles_;
    private Vector2 areaSize_ = Vector2(1280.0f, 720.0f);
    private float positionOffsetX_ = 0.0f;

    // mapCols / mapRows が 0 のときは 1280×720 の範囲
    void Initialize(Td3SpriteFactory@ factory, int mapCols, int mapRows, int count = 36)
    {
        if (mapCols > 0 && mapRows > 0) {
            const float mapW = float(mapCols) * 64.0f;
            const float mapH = float(mapRows) * 64.0f;
            areaSize_.x = (mapW > 1280.0f) ? mapW : 1280.0f;
            areaSize_.y = (mapH > 720.0f) ? mapH : 720.0f;
        }
        particles_.resize(0);
        for (int i = 0; i < count; ++i) {
            BackgroundParticle particle;
            @particle.sprite = factory.Create("white1x1.png", "GameBgParticle" + i);
            if (particle.sprite is null) {
                continue;
            }
            particle.sprite.SetAnchor(Vector2(0.5f, 0.5f));
            particle.sprite.SetBlendMode(kBlendAdd);
            particle.sprite.SetSortingLayer(-9);
            ResetParticle(particle);
            particles_.insertLast(particle);
        }
    }

    void Update(float dt, float positionOffsetX = 0.0f)
    {
        positionOffsetX_ = positionOffsetX;
        for (uint i = 0; i < particles_.length(); ++i) {
            BackgroundParticle@ p = particles_[i];
            p.age += dt;
            const float t = (p.lifetime > 0.0f) ? Clamp(p.age / p.lifetime, 0.0f, 1.0f) : 1.0f;
            // 見えなくなってから作り直す
            const float fade = 1.0f - Td3EaseApply(t, Td3Ease::EaseInSine);
            if (p.age >= p.lifetime && fade <= 0.0f) {
                ResetParticle(p);
            }
            const float sway = sin((t * kPi * 2.0f) + p.swayPhase) * p.swayAmplitude;
            p.rotation = p.rotation + p.rotationSpeed * dt;
            p.sprite.SetTranslate(positionOffsetX_ + p.startPosition.x + sway, p.startPosition.y + p.velocity.y * p.age);
            p.sprite.SetRotate(p.rotation);
            p.sprite.SetScale(p.scale.x, p.scale.y);
            p.sprite.SetColor(Vector4(p.color.x, p.color.y, p.color.z, p.color.w * fade));
        }
    }

    void SetVisible(bool visible)
    {
        for (uint i = 0; i < particles_.length(); ++i) {
            particles_[i].sprite.SetActive(visible);
        }
    }

    private void ResetParticle(BackgroundParticle@ p)
    {
        const float halfW = areaSize_.x * 0.5f;
        const float halfH = areaSize_.y * 0.5f;
        const int ci = Random::Range(0, 2);
        p.startPosition = Vector2(Random::Range(-halfW, halfW), -halfH - Random::Range(16.0f, 120.0f));
        p.velocity = Vector2(0.0f, Random::Range(70.0f, 105.0f));
        const float size = Random::Range(8.0f, 18.0f);
        p.scale = Vector2(size, size);
        p.color = (ci == 0) ? kNeonPlayerColor : ((ci == 1) ? kNeonHatColor : kNeonBlockColor);
        p.color.w = Random::Range(0.35f, 0.8f);
        p.swayAmplitude = Random::Range(8.0f, 28.0f);
        p.swayPhase = Random::Range(0.0f, kPi * 2.0f);
        p.rotation = Vector3(Random::Range(0.0f, kPi * 2.0f), Random::Range(0.0f, kPi * 2.0f), Random::Range(0.0f, kPi * 2.0f));
        p.rotationSpeed = Vector3(Random::Range(-1.2f, 1.2f), Random::Range(-1.0f, 1.0f), Random::Range(-2.4f, 2.4f));
        p.age = Random::Range(0.0f, 2.6f);
        p.lifetime = Random::Range(3.8f, 4.8f);
        p.sprite.SetRotate(p.rotation);
        p.sprite.SetTranslate(positionOffsetX_ + p.startPosition.x, p.startPosition.y);
        p.sprite.SetScale(p.scale.x, p.scale.y);
        p.sprite.SetColor(p.color);
    }
}
