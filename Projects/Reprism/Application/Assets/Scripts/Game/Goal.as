using namespace CoreEngine;

// ゴール（元の Goal2D・GoalClearDirector・GoalBorderLineEffect・GoalCloseParticleSystem・GoalSuctionParticleEffect）

const string kGoalMaterial = "Application/Assets/Materials/Goal.mat";
const float kGoalSpriteScale = 2.0f;
const float kGoalPositionYOffset = 10.0f;

class Goal2D
{
    private Td3Sprite@ sprite_;
    private Vector2 position_;
    private ShaderMaterial@ material_;
    private float time_ = 0.0f;

    void Initialize(Td3Sprite@ sprite, const Vector2 &in worldPos)
    {
        @sprite_ = sprite;
        position_ = Vector2(worldPos.x, worldPos.y + kGoalPositionYOffset);
        time_ = 0.0f;
        if (sprite_ is null) {
            return;
        }
        sprite_.SetAnchor(Vector2(0.5f, 0.5f));
        sprite_.SetColor(Vector4(1.0f, 1.0f, 1.0f, 1.0f));
        sprite_.SetBlendMode(kBlendAdd);
        sprite_.SetSortingLayer(1);
        sprite_.SetScale(kTileSize * kGoalSpriteScale, kTileSize * kGoalSpriteScale);
        sprite_.SetTranslate(position_);
        sprite_.SetMaterial(kGoalMaterial);
        @material_ = ShaderMaterial(kGoalMaterial);
    }

    void Update(float dt)
    {
        if (dt <= 0.0f) {
            return;
        }
        time_ += dt;
        if (material_ !is null) {
            material_.SetFloat("time", time_);
        }
    }

    bool IsTouching(const Vector2 &in pos, float halfW, float halfH) const
    {
        const float dx = position_.x - pos.x;
        const float dy = position_.y - pos.y;
        const float overlapX = (kTileSize * 0.5f + halfW) - (dx < 0.0f ? -dx : dx);
        const float overlapY = (kTileSize * 0.5f + halfH) - (dy < 0.0f ? -dy : dy);
        return overlapX > 0.0f && overlapY > 0.0f;
    }

    Vector2 GetPosition() const { return position_; }
    float GetSpriteScale() const { return kGoalSpriteScale; }
}

// 画面の縁を、中央の上から左右へ半周ずつ伸びる線（自機は左・ハットは右）
class GoalBorderSide
{
    Td3Sprite@ topSprite;
    Td3Sprite@ sideSprite;
    Td3Sprite@ bottomSprite;
    Td3Sprite@ leftSideSprite;
    Td3Sprite@ topLeftSprite;
    float fill = 0.0f;
    float easedFill = 0.0f;
}

class GoalBorderLineEffect
{
    private GoalBorderSide player_;
    private GoalBorderSide hat_;
    private Vector4 leftColor_ = Vector4(kNeonPlayerColor.x, kNeonPlayerColor.y, kNeonPlayerColor.z, 0.85f);
    private Vector4 rightColor_ = Vector4(kNeonHatColor.x, kNeonHatColor.y, kNeonHatColor.z, 0.85f);

    void Initialize(Td3SpriteFactory@ factory)
    {
        @player_.topSprite = MakeSprite(factory, "GoalBorder_P_Top");
        @player_.sideSprite = MakeSprite(factory, "GoalBorder_P_Side");
        @player_.bottomSprite = MakeSprite(factory, "GoalBorder_P_Bottom");
        @player_.leftSideSprite = MakeSprite(factory, "GoalBorder_P_LeftSide");
        @player_.topLeftSprite = MakeSprite(factory, "GoalBorder_P_TopLeft");
        @hat_.topSprite = MakeSprite(factory, "GoalBorder_H_Top");
        @hat_.sideSprite = MakeSprite(factory, "GoalBorder_H_Side");
        @hat_.bottomSprite = MakeSprite(factory, "GoalBorder_H_Bottom");
        @hat_.leftSideSprite = MakeSprite(factory, "GoalBorder_H_LeftSide");
        @hat_.topLeftSprite = MakeSprite(factory, "GoalBorder_H_TopLeft");
    }

    private Td3Sprite@ MakeSprite(Td3SpriteFactory@ factory, const string &in name)
    {
        Td3Sprite@ sprite = factory.Create("white1x1.png", name);
        sprite.SetAnchor(Vector2(0.5f, 0.5f));
        sprite.SetSortingLayer(20);
        sprite.SetActive(false);
        return sprite;
    }

    void Update(float dt, bool playerTouch, bool hatTouch, float fillRate, const Vector2 &in cameraPosition, float zoom)
    {
        const float halfW = (kScreenWidth * 0.5f) / zoom;
        const float halfH = (kScreenHeight * 0.5f) / zoom;
        const float thicknessWorld = 16.0f / zoom;
        UpdateSideHalf(player_, playerTouch, dt, fillRate, cameraPosition.x, cameraPosition.y, halfW, halfH,
            thicknessWorld, true, leftColor_);
        UpdateSideHalf(hat_, hatTouch, dt, fillRate, cameraPosition.x, cameraPosition.y, halfW, halfH,
            thicknessWorld, false, rightColor_);
    }

    private float EaseInOutCubic(float t) const
    {
        t = Clamp(t, 0.0f, 1.0f);
        return -(cos(kPi * t) - 1.0f) * 0.5f;
    }

    private float EaseInCubic(float t) const
    {
        t = Clamp(t, 0.0f, 1.0f);
        return t * t * t;
    }

    private void UpdateSideHalf(GoalBorderSide@ side, bool touching, float dt, float fillRate, float camX, float camY,
        float halfW, float halfH, float thicknessWorld, bool isLeft, const Vector4 &in color)
    {
        // 触れている間は接触時間より速く伸び、離すと縮む
        if (touching) {
            side.fill = Min(side.fill + fillRate * dt * 2.5f, 1.0f);
        } else {
            side.fill = Max(side.fill - fillRate * dt * 2.0f, 0.0f);
        }
        side.easedFill = touching ? EaseInOutCubic(side.fill) : EaseInCubic(side.fill);

        if (side.easedFill <= 0.0f) {
            side.topSprite.SetActive(false);
            side.sideSprite.SetActive(false);
            side.bottomSprite.SetActive(false);
            return;
        }

        const float screenTop = camY + halfH;
        const float screenBottom = camY - halfH;
        const float screenLeft = camX - halfW;
        const float screenRight = camX + halfW;
        const float screenMidX = camX;
        const float topLen = halfW;
        const float sideLen = halfH * 2.0f;
        const float bottomLen = halfW;
        const float totalLen = topLen + sideLen + bottomLen;
        const float drawn = side.easedFill * totalLen;

        // 上の辺（中央 → 角）
        {
            const float segLen = Min(drawn, topLen);
            if (segLen > 0.0f) {
                side.topSprite.SetActive(true);
                side.topSprite.SetColor(color);
                const float lineEndX = isLeft ? (screenMidX - segLen) : (screenMidX + segLen);
                side.topSprite.SetTranslate((screenMidX + lineEndX) * 0.5f, screenTop);
                side.topSprite.SetScale(segLen, thicknessWorld);
            } else {
                side.topSprite.SetActive(false);
            }
        }
        // 縦の辺（上の角 → 下の角）
        {
            const float segDrawn = drawn - topLen;
            if (segDrawn > 0.0f) {
                side.sideSprite.SetActive(true);
                side.sideSprite.SetColor(color);
                const float segLen = Min(segDrawn, sideLen);
                const float edgeX = isLeft ? screenLeft : screenRight;
                side.sideSprite.SetTranslate(edgeX, screenTop - segLen * 0.5f);
                side.sideSprite.SetScale(thicknessWorld, segLen);
            } else {
                side.sideSprite.SetActive(false);
            }
        }
        // 下の辺（角 → 中央）
        {
            const float segDrawn = drawn - topLen - sideLen;
            if (segDrawn > 0.0f) {
                side.bottomSprite.SetActive(true);
                side.bottomSprite.SetColor(color);
                const float segLen = Min(segDrawn, bottomLen);
                const float edgeX = isLeft ? screenLeft : screenRight;
                const float lineEndX = isLeft ? (edgeX + segLen) : (edgeX - segLen);
                side.bottomSprite.SetTranslate((edgeX + lineEndX) * 0.5f, screenBottom);
                side.bottomSprite.SetScale(segLen, thicknessWorld);
            } else {
                side.bottomSprite.SetActive(false);
            }
        }
    }
}

// ゴールが閉じたときに 2 重の輪に広がる粒（加算）
class GoalCloseParticle
{
    Td3Sprite@ sprite;
    Vector4 baseColor;
    float orbitAngle = 0.0f;
    float orbitAngularSpeed = 0.0f;
    float selfRotation = 0.0f;
    float rotationSpeed = 0.0f;
    float age = 0.0f;
    float lifetime = 0.0f;
    float startSize = 0.0f;
    float maxRadius = 0.0f;
}

class GoalCloseParticleSystem
{
    Td3SpritePool@ pool;
    float goalRadius = 102.4f;
    private array<GoalCloseParticle@> particles_;

    void Spawn(const Vector2 &in position)
    {
        // 内の輪・外の輪（半径はゴールの大きさに対する倍率）
        array<float> radiusMin = { 0.30f, 0.69f };
        array<float> radiusMax = { 0.59f, 1.08f };
        array<float> fixedSize = { 20.0f, 16.0f };
        array<float> lifeMin = { 0.50f, 0.65f };
        array<float> lifeMax = { 0.75f, 0.95f };
        array<float> orbitSpeed = { 2.2f, -1.6f };
        for (uint ring = 0; ring < 2; ++ring) {
            const float ringMaxRadius = Random::Range(radiusMin[ring] * goalRadius, radiusMax[ring] * goalRadius);
            const float baseLife = Random::Range(lifeMin[ring], lifeMax[ring]);
            for (int i = 0; i < 28; ++i) {
                const float angle = (float(i) / 28.0f) * kPi * 2.0f;
                Td3Sprite@ sprite = pool.Acquire();
                sprite.SetAnchor(Vector2(0.5f, 0.5f));
                sprite.SetBlendMode(kBlendAdd);
                sprite.SetSortingLayer(20);
                const Vector4 particleColor = PickGameplayParticleColor();
                sprite.SetColor(particleColor);
                sprite.SetScale(fixedSize[ring], fixedSize[ring]);
                sprite.SetRotateZ(0.0f);
                sprite.SetTranslate(position);

                GoalCloseParticle p;
                @p.sprite = sprite;
                p.baseColor = particleColor;
                p.orbitAngle = angle;
                p.orbitAngularSpeed = orbitSpeed[ring];
                p.selfRotation = 0.0f;
                p.rotationSpeed = Random::Range(-10.0f, 10.0f);
                p.lifetime = baseLife;
                p.startSize = fixedSize[ring];
                p.maxRadius = ringMaxRadius;
                particles_.insertLast(p);
            }
        }
    }

    void Update(float dt, const Vector2 &in goalPos)
    {
        for (int i = int(particles_.length()) - 1; i >= 0; --i) {
            GoalCloseParticle@ p = particles_[uint(i)];
            p.age += dt;
            const float clampedRatio = (p.lifetime > 0.0f) ? Clamp(p.age / p.lifetime, 0.0f, 1.0f) : 1.0f;
            p.orbitAngle += p.orbitAngularSpeed * dt;
            const float radius = Td3EaseLerp(0.0f, p.maxRadius, clampedRatio, Td3Ease::EaseOutQuart);
            p.sprite.SetTranslate(goalPos.x + cos(p.orbitAngle) * radius, goalPos.y + sin(p.orbitAngle) * radius);
            p.selfRotation += p.rotationSpeed * dt;
            p.sprite.SetRotateZ(p.selfRotation);
            float sizeRatio = 0.0f;
            if (clampedRatio < 0.40f) {
                sizeRatio = Td3EaseApply(clampedRatio / 0.40f, Td3Ease::EaseOutQuint);
            } else {
                sizeRatio = 1.0f - Td3EaseApply((clampedRatio - 0.40f) / 0.60f, Td3Ease::EaseInQuint);
            }
            const float size = p.startSize * sizeRatio;
            p.sprite.SetScale(size, size);
            const float alpha = (clampedRatio < 0.5f) ? 1.0f
                : 1.0f - Td3EaseApply((clampedRatio - 0.5f) / 0.5f, Td3Ease::EaseInQuad);
            p.sprite.SetColor(Vector4(p.baseColor.x, p.baseColor.y, p.baseColor.z, alpha));
            if (p.age >= p.lifetime) {
                pool.Release(p.sprite);
                particles_.removeAt(uint(i));
            }
        }
    }
}

// ゴールのまわりから渦を巻いて吸い込まれる粒（加算・ゴールが閉じるまで出し続ける）
class GoalSuctionParticle
{
    Td3Sprite@ sprite;
    float orbitAngle = 0.0f;
    float orbitRadius = 0.0f;
    float startRadius = 0.0f;
    float baseAngularSpeed = 0.0f;
    float selfRotation = 0.0f;
    float selfRotSpeed = 0.0f;
    float age = 0.0f;
    float lifetime = 0.0f;
    float startSize = 0.0f;
    float startAspectX = 1.0f;
    float startAspectY = 1.0f;
    Vector4 neonColor;
}

class GoalSuctionParticleEffect
{
    Td3SpritePool@ pool;
    float goalRadius = 102.4f;
    private array<GoalSuctionParticle@> particles_;
    private float emitAccumulator_ = 0.0f;
    private float angleAccumulator_ = 0.0f;
    private bool isEmitting_ = false;

    bool IsAlive() const { return isEmitting_ || particles_.length() > 0; }
    void Stop() { isEmitting_ = false; }

    void Play(const Vector2 &in position)
    {
        isEmitting_ = true;
        emitAccumulator_ = 0.0f;
        // 最初は円周に等間隔で出す
        const float step = (2.0f * kPi) / 24.0f;
        for (int i = 0; i < 24; ++i) {
            EmitOne(position, step * float(i));
        }
        angleAccumulator_ = 0.0f;
    }

    private void EmitOne(const Vector2 &in goalPos, float angle)
    {
        const float radius = Random::Range(goalRadius * 0.69f, goalRadius * 0.98f);
        const float lifetime = Random::Range(1.0f, 1.5f);
        const float size = Random::Range(5.0f, 18.0f);
        const float aspectX = Random::Range(0.25f, 1.0f);
        const float aspectY = Random::Range(0.25f, 1.0f);
        const float angSpeed = 2.8f * Random::Range(0.85f, 1.25f);
        const float selfSpin = Random::Range(-8.0f, 8.0f);

        Td3Sprite@ sprite = pool.Acquire();
        sprite.SetAnchor(Vector2(0.5f, 0.5f));
        sprite.SetBlendMode(kBlendAdd);
        sprite.SetSortingLayer(2);
        const Vector4 neonColor = PickGameplayParticleColor();
        sprite.SetColor(neonColor);
        sprite.SetScale(size * aspectX, size * aspectY);
        sprite.SetRotateZ(angle);
        sprite.SetTranslate(goalPos.x + cos(angle) * radius, goalPos.y + sin(angle) * radius);

        GoalSuctionParticle p;
        @p.sprite = sprite;
        p.orbitAngle = angle;
        p.orbitRadius = radius;
        p.startRadius = radius;
        p.baseAngularSpeed = angSpeed;
        p.selfRotation = angle;
        p.selfRotSpeed = selfSpin;
        p.lifetime = lifetime;
        p.startSize = size;
        p.startAspectX = aspectX;
        p.startAspectY = aspectY;
        p.neonColor = neonColor;
        particles_.insertLast(p);
    }

    void Update(float dt, const Vector2 &in goalPos)
    {
        if (isEmitting_) {
            emitAccumulator_ += dt;
            const float interval = 1.0f / 35.0f;
            while (emitAccumulator_ >= interval) {
                emitAccumulator_ -= interval;
                // 少しずつ角度をずらして螺旋に出す
                angleAccumulator_ += kPi * 0.42f;
                EmitOne(goalPos, angleAccumulator_);
            }
        }

        for (int i = 0; i < int(particles_.length()); ++i) {
            GoalSuctionParticle@ p = particles_[uint(i)];
            p.age += dt;
            const float t = (p.lifetime > 0.0f) ? Clamp(p.age / p.lifetime, 0.0f, 1.0f) : 1.0f;
            const float tEase = t * t;
            p.orbitRadius = p.startRadius * (1.0f - tEase);
            // 中心へ近づくほど速く回る
            const float angularSpeed = (p.orbitRadius > 1.0f)
                ? p.baseAngularSpeed * (p.startRadius / p.orbitRadius)
                : p.baseAngularSpeed * p.startRadius;
            p.orbitAngle += Min(angularSpeed, 60.0f) * dt;
            p.sprite.SetTranslate(goalPos.x + cos(p.orbitAngle) * p.orbitRadius, goalPos.y + sin(p.orbitAngle) * p.orbitRadius);
            p.selfRotation += p.selfRotSpeed * dt;
            p.sprite.SetRotateZ(p.selfRotation);
            float size = 0.0f;
            if (t < 0.75f) {
                size = p.startSize * (1.0f - t * 0.27f);
            } else {
                const float localT = (t - 0.75f) / 0.25f;
                size = p.startSize * 0.8f * (1.0f - localT * localT * localT * localT);
            }
            p.sprite.SetScale(size * p.startAspectX, size * p.startAspectY);
            const float alpha = (t < 0.55f) ? 1.0f : 1.0f - ((t - 0.55f) / 0.45f);
            p.sprite.SetColor(Vector4(p.neonColor.x, p.neonColor.y, p.neonColor.z, alpha));
            if (p.age >= p.lifetime) {
                p.sprite.SetActive(false);
            }
        }

        // 寿命の切れた粒を外して板を返す
        for (int i = int(particles_.length()) - 1; i >= 0; --i) {
            if (!particles_[uint(i)].sprite.IsActive()) {
                pool.Release(particles_[uint(i)].sprite);
                particles_.removeAt(uint(i));
            }
        }
    }
}

funcdef void GoalTransitionCallback();

// ゴールに両方が 1.5 秒触れたら：吸い込み → ゴールが閉じる → CLEAR → ブロックが崩れ落ちて次へ
class GoalClearDirector
{
    private Goal2D@ goal_;
    private Td3Sprite@ goalSprite_;
    private Td3Sprite@ playerSprite_;
    private Td3Sprite@ playerLegL_;
    private Td3Sprite@ playerLegR_;
    private Td3Sprite@ hatSprite_;
    private ShaderMaterial@ suctionMaterial_;
    private Td3SpriteTransform@ playerBaseTransform_ = Td3SpriteTransform();
    private Td3SpriteTransform@ hatBaseTransform_ = Td3SpriteTransform();
    private bool isPlayerWarpAnimating_ = false;
    private bool isHatWarpAnimating_ = false;
    private Vector2 playerSuctionStartPos_;
    private Vector2 hatSuctionStartPos_;
    private Vector2 playerSuctionStartScale_;
    private Vector2 hatSuctionStartScale_;
    private float playerSuctionStartRadius_ = 0.0f;
    private float playerSuctionStartAngle_ = 0.0f;
    private float hatSuctionStartRadius_ = 0.0f;
    private float hatSuctionStartAngle_ = 0.0f;
    private bool suctionCaptured_ = false;

    private float goalTouchTimer_ = 0.0f;
    private float goalClearTime_ = 1.5f;
    private float goalDisappearTimer_ = 0.0f;
    private bool goalDisappearStarted_ = false;
    private bool goalCloseParticlesSpawned_ = false;
    private float goalCloseParticleTimer_ = 0.0f;
    private bool goalTransitionRequested_ = false;
    private bool goalTransitionDispatched_ = false;
    private bool goalSePlayed_ = false;
    private float pixelFallTimer_ = 0.0f;
    private bool pixelFallStarted_ = false;

    private Td3Sprite@ clearBannerSprite_;
    private float clearBannerTimer_ = 0.0f;
    private bool clearBannerStarted_ = false;
    private bool clearBannerFinished_ = false;

    private GoalCloseParticleSystem closeParticles_;
    private GoalBorderLineEffect borderLineEffect_;
    private GoalSuctionParticleEffect suctionParticles_;
    private GoalTransitionCallback@ onTransition_;
    private GameActionSoundController@ soundController_;

    void Initialize(Td3SpriteFactory@ factory, GoalTransitionCallback@ onTransition)
    {
        @closeParticles_.pool = Td3SpritePool(factory, "white1x1.png", "GoalCloseParticle");
        @suctionParticles_.pool = Td3SpritePool(factory, "white1x1.png", "GoalSuctionParticle");
        borderLineEffect_.Initialize(factory);
        @onTransition_ = onTransition;

        // CLEAR の文字（UI_Clear.png の帯を切り出す。最初は隠して大きさ 0）
        @clearBannerSprite_ = factory.Create("UI/UI_Clear.png", "clearBanner");
        clearBannerSprite_.SetTextureRect(0.0f, 192.0f, 512.0f, 96.0f);
        clearBannerSprite_.SetTranslate(0.0f, 0.0f);
        clearBannerSprite_.SetScale(0.0f, 0.0f);
        clearBannerSprite_.SetActive(false);
    }

    void SetGoal(Goal2D@ goal) { @goal_ = goal; }
    void SetGoalSprite(Td3Sprite@ sprite) { @goalSprite_ = sprite; }
    void SetSoundController(GameActionSoundController@ controller) { @soundController_ = controller; }
    void SetGoalClearTime(float time) { goalClearTime_ = time; }
    bool IsGoalDisappearStarted() const { return goalDisappearStarted_; }

    void SetPlayerSprites(Td3Sprite@ body, Td3Sprite@ legL, Td3Sprite@ legR)
    {
        @playerSprite_ = body;
        @playerLegL_ = legL;
        @playerLegR_ = legR;
    }

    void SetHatSprite(Td3Sprite@ sprite) { @hatSprite_ = sprite; }
    void SetSuctionMaterial(ShaderMaterial@ material) { @suctionMaterial_ = material; }

    void Update(float dt, bool playerTouch, bool hatTouch, const Vector2 &in cameraPosition, float zoom)
    {
        if (goal_ is null) {
            return;
        }
        goal_.Update(dt);

        const float goalRadius = kTileSize * goal_.GetSpriteScale() * 0.5f;
        suctionParticles_.goalRadius = goalRadius;
        closeParticles_.goalRadius = goalRadius;

        // 両方が触れている間だけ進む（離れると戻る）
        const bool touching = playerTouch && hatTouch;
        UpdateGoalGauge(dt, touching);

        const Vector2 goalPos = goal_.GetPosition();
        if (!goalDisappearStarted_ && !suctionParticles_.IsAlive()) {
            suctionParticles_.Play(goalPos);
        }
        suctionParticles_.Update(dt, goalPos);

        UpdateGoalWarpAnimation(dt, playerTouch, hatTouch);

        const float fillRate = (goalClearTime_ > 0.0f) ? (1.0f / goalClearTime_) : 1.0f;
        borderLineEffect_.Update(dt, playerTouch, hatTouch, fillRate, cameraPosition, zoom);

        closeParticles_.Update(dt, goalPos);

        if (goalTransitionRequested_ && !goalTransitionDispatched_ && onTransition_ !is null) {
            goalTransitionDispatched_ = true;
            onTransition_();
        }
    }

    private void UpdateGoalGauge(float dt, bool touching)
    {
        if (touching) {
            goalTouchTimer_ += dt;
        } else {
            goalTouchTimer_ -= dt;
        }
        goalTouchTimer_ = Clamp(goalTouchTimer_, 0.0f, goalClearTime_);
    }

    // 触れ始めた時の姿を覚え、離れたらその姿へ戻す（自機とハットの色の指定は絵に効かない）
    private void UpdateWarpSprite(Td3Sprite@ sprite, bool touching, Td3SpriteTransform@ baseTransform, bool &out wasAnimating, bool isAnimating)
    {
        wasAnimating = isAnimating;
        if (sprite is null) {
            return;
        }
        if (!touching) {
            if (isAnimating) {
                Td3SpriteTransform restored;
                restored.translate = baseTransform.translate;
                restored.rotate = Vector3(baseTransform.rotate.x, baseTransform.rotate.y, 0.0f);
                restored.scale = baseTransform.scale;
                sprite.SetTransform(restored);
                wasAnimating = false;
            }
            if (suctionMaterial_ !is null) {
                suctionMaterial_.SetFloat("progress", 0.0f);
                suctionMaterial_.SetFloat("swirl", 0.0f);
                suctionMaterial_.SetFloat("stretch", 0.0f);
                suctionMaterial_.SetFloat("alpha", 1.0f);
            }
            return;
        }
        if (!isAnimating) {
            Td3SpriteTransform@ current = sprite.GetTransform();
            baseTransform.translate = current.translate;
            baseTransform.rotate = current.rotate;
            baseTransform.scale = current.scale;
            wasAnimating = true;
        }
    }

    private void UpdateGoalWarpAnimation(float dt, bool playerTouch, bool hatTouch)
    {
        const Vector2 goalPos = goal_.GetPosition();
        const bool completed = goalTouchTimer_ >= goalClearTime_;
        const float kObjectSuctionPhaseEnd = 0.70f;

        if (!goalDisappearStarted_) {
            bool animating;
            UpdateWarpSprite(playerSprite_, playerTouch, playerBaseTransform_, animating, isPlayerWarpAnimating_);
            isPlayerWarpAnimating_ = animating;
            UpdateWarpSprite(hatSprite_, hatTouch, hatBaseTransform_, animating, isHatWarpAnimating_);
            isHatWarpAnimating_ = animating;
        }

        if (completed && !goalDisappearStarted_) {
            goalDisappearStarted_ = true;
            goalDisappearTimer_ = 0.0f;
            suctionParticles_.Stop();
        }

        if (!goalDisappearStarted_) {
            return;
        }

        // 吸い込みの始めの姿を 1 回だけ覚える
        if (!suctionCaptured_) {
            const float goalOuterRadius = kTileSize * goal_.GetSpriteScale() * 0.5f;
            if (playerSprite_ !is null) {
                playerSuctionStartPos_ = playerSprite_.GetTranslate();
                playerSuctionStartScale_ = playerSprite_.GetScale();
                playerSuctionStartRadius_ = goalOuterRadius;
                playerSuctionStartAngle_ = kPi;
            }
            if (hatSprite_ !is null) {
                hatSuctionStartPos_ = hatSprite_.GetTranslate();
                hatSuctionStartScale_ = hatSprite_.GetScale();
                hatSuctionStartRadius_ = goalOuterRadius;
                hatSuctionStartAngle_ = 0.0f;
            }
            suctionCaptured_ = true;
        }

        goalDisappearTimer_ += dt;
        const float disappearRatio = Clamp(goalDisappearTimer_ / 1.8f, 0.0f, 1.0f);
        const float suctionDisappearRatio = Clamp(disappearRatio / kObjectSuctionPhaseEnd, 0.0f, 1.0f);
        const float radiusT = Td3EaseApply(suctionDisappearRatio, Td3Ease::EaseInCubic);
        const float suctionScale = 1.0f - radiusT;
        const float suctionAlpha = 1.0f - Td3EaseApply(suctionDisappearRatio, Td3Ease::EaseInExpo);

        ApplySuction(playerSprite_, playerSuctionStartPos_, playerSuctionStartRadius_, playerSuctionStartAngle_,
            playerSuctionStartScale_, suctionDisappearRatio, suctionScale, suctionAlpha, goalPos);
        ApplySuction(hatSprite_, hatSuctionStartPos_, hatSuctionStartRadius_, hatSuctionStartAngle_,
            hatSuctionStartScale_, suctionDisappearRatio, suctionScale, suctionAlpha, goalPos);
        ApplyPlayerLegSuction(playerLegL_, -14.0f, goalPos);
        ApplyPlayerLegSuction(playerLegR_, 14.0f, goalPos);

        // 吸い込み終わったらゴールだけ一瞬膨らませて消す（色の指定は絵に効かない）
        if (goalSprite_ !is null) {
            float goalScale = 1.0f;
            if (disappearRatio >= kObjectSuctionPhaseEnd) {
                const float goalDisappearRatio = Clamp((disappearRatio - kObjectSuctionPhaseEnd) / (1.0f - kObjectSuctionPhaseEnd), 0.0f, 1.0f);
                const float kSwellPhaseEnd = 0.40f;
                const float kSwellScale = 1.35f;
                if (goalDisappearRatio < kSwellPhaseEnd) {
                    goalScale = Td3EaseLerp(1.0f, kSwellScale, goalDisappearRatio / kSwellPhaseEnd, Td3Ease::EaseOutBack);
                } else {
                    goalScale = Td3EaseLerp(kSwellScale, 0.0f, (goalDisappearRatio - kSwellPhaseEnd) / (1.0f - kSwellPhaseEnd),
                        Td3Ease::EaseInExpo);
                }
            }
            const float size = kTileSize * goal_.GetSpriteScale() * goalScale;
            goalSprite_.SetScale(size, size);
            if (disappearRatio >= 1.0f) {
                goalSprite_.SetActive(false);
            }
        }

        // 消えきったら音と粒
        if (disappearRatio >= 1.0f) {
            if (!goalSePlayed_) {
                if (soundController_ !is null) {
                    soundController_.PlayGoal();
                }
                goalSePlayed_ = true;
            }
            if (!goalCloseParticlesSpawned_) {
                closeParticles_.Spawn(goalPos);
                goalCloseParticlesSpawned_ = true;
                goalCloseParticleTimer_ = 0.0f;
            }
        }

        if (!goalCloseParticlesSpawned_) {
            return;
        }
        goalCloseParticleTimer_ += dt;

        // CLEAR を出す
        if (goalCloseParticleTimer_ >= 0.2f && !clearBannerStarted_) {
            clearBannerSprite_.SetActive(true);
            clearBannerStarted_ = true;
            clearBannerTimer_ = 0.0f;
        }
        if (clearBannerStarted_ && !clearBannerFinished_) {
            clearBannerTimer_ += dt;
            const float baseScaleY = 96.0f / 288.0f;
            if (clearBannerTimer_ < 0.45f) {
                const float scale = Td3EaseApply(clearBannerTimer_ / 0.45f, Td3Ease::EaseOutBack);
                clearBannerSprite_.SetScale(scale, scale * baseScaleY);
            } else {
                clearBannerSprite_.SetScale(1.0f, baseScaleY);
            }
            if (clearBannerTimer_ >= 0.45f + 1.2f) {
                clearBannerFinished_ = true;
            }
        }

        // ブロックに割れて落ちる
        if (clearBannerFinished_ && !pixelFallStarted_) {
            ShaderMaterial@ fade = PrepareFadeFall();
            if (fade !is null) {
                fade.SetFloat("progress", 0.0f);
            }
            PostEffect::SetEnabled("Fade", true);
            pixelFallStarted_ = true;
            pixelFallTimer_ = 0.0f;
        }
        if (pixelFallStarted_) {
            const float kCompleteFactor = 1.4f;
            pixelFallTimer_ += dt;
            const float fadeAlpha = Clamp(pixelFallTimer_ / 1.0f, 0.0f, kCompleteFactor);
            ShaderMaterial@ fade = PostEffect::GetMaterial("Fade");
            if (fade !is null) {
                fade.SetFloat("progress", fadeAlpha / kCompleteFactor);
            }
            if (pixelFallTimer_ >= 1.0f * kCompleteFactor && !goalTransitionRequested_) {
                goalTransitionRequested_ = true;
            }
        }
    }

    private void ApplySuction(Td3Sprite@ sprite, const Vector2 &in startPos, float startRadius, float startAngle,
        const Vector2 &in startScale, float suctionDisappearRatio, float suctionScale, float suctionAlpha,
        const Vector2 &in goalPos)
    {
        if (sprite is null) {
            return;
        }
        const float kSuctionPhase1End = 0.25f;
        float posX, posY, rotateZ;
        if (suctionDisappearRatio < kSuctionPhase1End) {
            // ゴールの外周へ寄せる（回さない）
            const float t1 = Td3EaseApply(suctionDisappearRatio / kSuctionPhase1End, Td3Ease::EaseInOutCubic);
            const float targetX = goalPos.x + startRadius * cos(startAngle);
            const float targetY = goalPos.y + startRadius * sin(startAngle);
            posX = Lerp(startPos.x, targetX, t1);
            posY = Lerp(startPos.y, targetY, t1);
            rotateZ = sprite.GetRotate().z;
        } else {
            // 外周から中心へ 2 回転しながら巻き込む
            const float t2 = (suctionDisappearRatio - kSuctionPhase1End) / (1.0f - kSuctionPhase1End);
            const float spiralT = Td3EaseApply(t2, Td3Ease::EaseInCubic);
            const float rotationT = Td3EaseApply(t2, Td3Ease::EaseInQuint);
            const float currentRadius = startRadius * (1.0f - spiralT);
            const float currentAngle = startAngle + rotationT * 4.0f * kPi;
            posX = goalPos.x + currentRadius * cos(currentAngle);
            posY = goalPos.y + currentRadius * sin(currentAngle);
            rotateZ = currentAngle;
        }
        sprite.SetTranslate(posX, posY);
        sprite.SetRotateZ(rotateZ);
        sprite.SetScale(startScale.x * suctionScale, startScale.y * suctionScale);
        if (suctionDisappearRatio >= 1.0f) {
            sprite.SetActive(false);
            if (suctionMaterial_ !is null) {
                suctionMaterial_.SetFloat("alpha", 0.0f);
            }
        }
    }

    private void ApplyPlayerLegSuction(Td3Sprite@ legSprite, float xOffset, const Vector2 &in goalPos)
    {
        if (legSprite is null) {
            return;
        }
        const float disappearRatioLocal = Clamp(goalDisappearTimer_ / 1.8f, 0.0f, 1.0f);
        const float suctionDisappearRatioLocal = Clamp(disappearRatioLocal / 0.70f, 0.0f, 1.0f);
        const float kSuctionPhase1EndLocal = 0.25f;
        const float kLegOffsetY = -18.0f;
        const float suctionAlphaLocal = 1.0f - Td3EaseApply(suctionDisappearRatioLocal, Td3Ease::EaseInExpo);
        float posX, posY, rotateZ;
        const Vector2 current = legSprite.GetTranslate();
        if (suctionDisappearRatioLocal < kSuctionPhase1EndLocal) {
            const float t1 = Td3EaseApply(suctionDisappearRatioLocal / kSuctionPhase1EndLocal, Td3Ease::EaseInOutCubic);
            const float targetX = goalPos.x + playerSuctionStartRadius_ * cos(playerSuctionStartAngle_) + xOffset;
            const float targetY = goalPos.y + playerSuctionStartRadius_ * sin(playerSuctionStartAngle_) + kLegOffsetY;
            posX = Lerp(current.x, targetX, t1);
            posY = Lerp(current.y, targetY, t1);
            rotateZ = legSprite.GetRotate().z;
        } else {
            const float t2 = (suctionDisappearRatioLocal - kSuctionPhase1EndLocal) / (1.0f - kSuctionPhase1EndLocal);
            const float spiralT = Td3EaseApply(t2, Td3Ease::EaseInCubic);
            const float rotationT = Td3EaseApply(t2, Td3Ease::EaseInQuint);
            const float currentRadius = playerSuctionStartRadius_ * (1.0f - spiralT);
            const float currentAngle = playerSuctionStartAngle_ + rotationT * (6.0f * kPi);
            posX = goalPos.x + currentRadius * cos(currentAngle) + xOffset;
            posY = goalPos.y + currentRadius * sin(currentAngle) + kLegOffsetY;
            rotateZ = currentAngle + 0.12f;
        }
        legSprite.SetTranslate(posX, posY);
        legSprite.SetRotateZ(rotateZ);
        legSprite.SetColor(Vector4(suctionAlphaLocal, suctionAlphaLocal, suctionAlphaLocal, suctionAlphaLocal));
        if (suctionDisappearRatioLocal >= 1.0f) {
            legSprite.SetActive(false);
        }
    }
}
