using namespace CoreEngine;

// 自機（元の Player2D・PlayerPhysics・PlayerVisualController・PlayerLegAnimator・BlinkAnimationController）

const float kPlayerHalfW = 30.0f;
const float kPlayerHalfH = 30.0f;
const float kHatBlockHalf = 32.0f;

enum AirborneStatus
{
    None,     // 接地中
    Jumping,  // 上昇中
    Floating, // 頂点で止まっている
    Falling   // 落下中
}

enum FallOutState
{
    Normal,     // 通常
    FallingOut, // 下へ落ちた（死んだ顔を見せている）
    Returning   // 安全な位置へ戻っている
}

// ハットが運んでいるブロック（AABB）との押し出し（元の PlayerPhysics）
class HatBlockResolveResult
{
    Vector2 resolvedPos;
    bool isOnGround = false;
    bool hitCeiling = false;
    bool hitWallLeft = false;
    bool hitWallRight = false;
}

HatBlockResolveResult@ ResolveHatBlocks(const Vector2 &in playerPos, const Vector2 &in delta,
    const array<Vector2> &in blocks, float halfW, float halfH)
{
    HatBlockResolveResult r;
    r.resolvedPos = Vector2(playerPos.x + delta.x, playerPos.y + delta.y);

    // X（Y は動かす前の位置で見る）
    for (uint i = 0; i < blocks.length(); ++i) {
        const Vector2 b = blocks[i];
        const float overlapX = (halfW + kHatBlockHalf) - abs(r.resolvedPos.x - b.x);
        const float overlapY = (halfH + kHatBlockHalf) - abs(playerPos.y - b.y);
        if (overlapX > 0.0f && overlapY > 0.0f && overlapX < overlapY) {
            if (r.resolvedPos.x < b.x) {
                r.hitWallRight = true;
                r.resolvedPos.x = b.x - (halfW + kHatBlockHalf);
            } else {
                r.hitWallLeft = true;
                r.resolvedPos.x = b.x + (halfW + kHatBlockHalf);
            }
        }
    }
    // Y
    for (uint i = 0; i < blocks.length(); ++i) {
        const Vector2 b = blocks[i];
        const float overlapX = (halfW + kHatBlockHalf) - abs(r.resolvedPos.x - b.x);
        const float overlapY = (halfH + kHatBlockHalf) - abs(r.resolvedPos.y - b.y);
        if (overlapX > 0.0f && overlapY > 0.0f && overlapY <= overlapX) {
            if (r.resolvedPos.y < b.y) {
                r.hitCeiling = true;
                r.resolvedPos.y = b.y - (halfH + kHatBlockHalf);
            } else {
                r.isOnGround = true;
                r.resolvedPos.y = b.y + (halfH + kHatBlockHalf);
            }
        }
    }
    return r;
}

// 瞬き（アイドルと瞬きのクリップを切り替える）
class BlinkAnimationController
{
    private Td3Sprite@ sprite_;
    private string clipIdle_;
    private string clipBlink_;
    private Td3Timer blinkIntervalTimer_;
    private Td3Timer blinkWaitTimer_;
    private bool isBlinking_ = false;
    private int blinkCountRemaining_ = 0;

    void Initialize(Td3Sprite@ sprite, const string &in clipIdle, const string &in clipBlink)
    {
        @sprite_ = sprite;
        clipIdle_ = clipIdle;
        clipBlink_ = clipBlink;
        isBlinking_ = false;
        blinkCountRemaining_ = 0;
        if (sprite_ is null) {
            return;
        }
        blinkIntervalTimer_.Start(Random::Range(2.0f, 4.0f));
        @sprite_.GetAnimator().onFinished = Td3AnimFinished(this.OnAnimationFinished);
    }

    private void OnAnimationFinished()
    {
        sprite_.GetAnimator().Play(clipIdle_);
        isBlinking_ = false;
        if (blinkCountRemaining_ > 0) {
            blinkWaitTimer_.Start(0.12f);
        }
    }

    void Update(float dt)
    {
        if (sprite_ is null || dt <= 0.0f) {
            return;
        }
        // 2 回目の瞬きまでの短い待ち
        if (blinkWaitTimer_.IsActive()) {
            blinkWaitTimer_.Update(dt);
            if (blinkWaitTimer_.IsFinished()) {
                blinkWaitTimer_.Reset();
                --blinkCountRemaining_;
                isBlinking_ = true;
                sprite_.GetAnimator().Play(clipBlink_);
            }
        }
        if (!isBlinking_ && !blinkWaitTimer_.IsActive()) {
            blinkIntervalTimer_.Update(dt);
            if (blinkIntervalTimer_.IsFinished()) {
                blinkCountRemaining_ = Random::Chance(0.35f) ? 1 : 0;
                isBlinking_ = true;
                sprite_.GetAnimator().Play(clipBlink_);
                blinkIntervalTimer_.Start(Random::Range(2.0f, 4.0f));
            }
        }
        sprite_.GetAnimator().Update(dt, sprite_);
    }
}

// 足の動き（歩くと弧を描き、空中では離れて伸びる）
class PlayerLegAnimator
{
    private Td3Sprite@ legL_;
    private Td3Sprite@ legR_;
    Vector2 offsetL = Vector2(-18.0f, -22.0f);
    Vector2 offsetR = Vector2(18.0f, -22.0f);
    private Td3Timer animTimer_;
    private float walkBlend_ = 0.0f;
    private float airborneBlend_ = 0.0f;
    private float takeoffBlend_ = 0.0f;
    private float landingBlend_ = 0.0f;
    private bool wasAirborne_ = false;

    void Initialize(Td3Sprite@ legL, Td3Sprite@ legR)
    {
        @legL_ = legL;
        @legR_ = legR;
        animTimer_.Reset();
        walkBlend_ = 0.0f;
        airborneBlend_ = 0.0f;
        takeoffBlend_ = 0.0f;
        landingBlend_ = 0.0f;
        wasAirborne_ = false;
    }

    void Update(float dt, bool isMoving, bool isAirborne)
    {
        const float walkT = Saturate(dt * 10.0f);
        const float airborneT = Saturate(dt * 8.0f);
        const float impulseT = Saturate(dt * 9.0f);

        walkBlend_ = Lerp(walkBlend_, isMoving ? 1.0f : 0.0f, walkT);
        airborneBlend_ = Lerp(airborneBlend_, isAirborne ? 1.0f : 0.0f, airborneT);

        if (!wasAirborne_ && isAirborne) {
            takeoffBlend_ = 1.0f;
        }
        if (wasAirborne_ && !isAirborne) {
            landingBlend_ = 1.0f;
        }
        takeoffBlend_ = Lerp(takeoffBlend_, 0.0f, impulseT);
        landingBlend_ = Lerp(landingBlend_, 0.0f, impulseT);

        if (isMoving) {
            if (!animTimer_.IsActive()) {
                animTimer_.Start(0.22f, true);
            }
            animTimer_.Update(dt);
        } else if (walkBlend_ <= 0.01f) {
            animTimer_.Reset();
        }
        wasAirborne_ = isAirborne;
    }

    void Apply(const Vector2 &in bodyPos, float currentScaleX)
    {
        const bool facingRight = currentScaleX > 0.0f;
        const float phase = animTimer_.IsActive() ? animTimer_.GetProgress() * 2.0f * kPi : 0.0f;
        const float phaseR = phase + kPi;

        const float kStepAmplitudeX = 14.0f;
        const float kLiftAmplitudeY = 9.0f;
        const float kLegScale = 1.4f;
        const float strideL = sin(phase) * kStepAmplitudeX * walkBlend_ * (1.0f - airborneBlend_);
        const float strideR = sin(phaseR) * kStepAmplitudeX * walkBlend_ * (1.0f - airborneBlend_);
        const float liftL = Max(0.0f, cos(phase)) * kLiftAmplitudeY * walkBlend_ * (1.0f - airborneBlend_);
        const float liftR = Max(0.0f, cos(phaseR)) * kLiftAmplitudeY * walkBlend_ * (1.0f - airborneBlend_);

        const float airborneOffsetY = airborneBlend_ * -8.0f + takeoffBlend_ * -6.0f + landingBlend_ * 4.0f;
        const float legScaleX = kLegScale * (1.0f - airborneBlend_ * 0.10f - takeoffBlend_ * 0.05f + landingBlend_ * 0.12f);
        const float legScaleY = kLegScale * (1.0f + airborneBlend_ * 0.18f + takeoffBlend_ * 0.10f - landingBlend_ * 0.16f);

        // 右向きは左足が奥・右足が手前。左向きは逆
        const int layerL = facingRight ? 4 : 6;
        const int layerR = facingRight ? 6 : 4;

        ApplyLeg(legL_, offsetL, strideL, liftL + airborneOffsetY, legScaleX, legScaleY, currentScaleX, facingRight, bodyPos, layerL);
        ApplyLeg(legR_, offsetR, strideR, liftR + airborneOffsetY, legScaleX, legScaleY, currentScaleX, facingRight, bodyPos, layerR);
    }

    private void ApplyLeg(Td3Sprite@ sprite, const Vector2 &in baseOffset, float strideOffset, float liftOffset,
        float scaleX, float scaleY, float currentScaleX, bool facingRight, const Vector2 &in bodyPos, int sortingLayer)
    {
        if (sprite is null) {
            return;
        }
        sprite.SetScale(scaleX, scaleY);
        sprite.SetTranslate(bodyPos.x + (baseOffset.x + strideOffset) * currentScaleX, bodyPos.y + baseOffset.y + liftOffset);
        sprite.SetFlipX(facingRight);
        sprite.SetSortingLayer(sortingLayer);
    }
}

// 体の見た目（向きの補間・上下の揺れ・傾き・空中での伸び縮み）
class PlayerVisualController
{
    private Td3Sprite@ sprite_;
    PlayerLegAnimator legAnimator;
    float faceLerpSpeed = 12.0f;
    private float targetScaleX_ = 1.0f;
    private float currentScaleX_ = 1.0f;
    private float targetBodyTiltZ_ = 0.0f;
    private float currentBodyTiltZ_ = 0.0f;
    private AirborneStatus airborne_ = AirborneStatus::Falling;
    private float jumpProgress_ = 0.0f;
    private Td3Timer bodyAnimTimer_;

    void Initialize(Td3Sprite@ sprite, Td3Sprite@ legL, Td3Sprite@ legR)
    {
        @sprite_ = sprite;
        targetScaleX_ = 1.0f;
        currentScaleX_ = 1.0f;
        targetBodyTiltZ_ = 0.0f;
        currentBodyTiltZ_ = 0.0f;
        legAnimator.Initialize(legL, legR);
        bodyAnimTimer_.Start(0.6f, true);
    }

    void SetFacingInput(float scaleX, float tiltZ)
    {
        targetScaleX_ = scaleX;
        targetBodyTiltZ_ = tiltZ;
    }

    void Update(float dt, bool isMoving, AirborneStatus airborne, float jumpProgress)
    {
        bodyAnimTimer_.Update(dt);
        if (!isMoving) {
            targetBodyTiltZ_ = 0.0f;
        }
        const float raw = dt * faceLerpSpeed;
        const float t = raw < 1.0f ? raw : 1.0f;
        currentScaleX_ += (targetScaleX_ - currentScaleX_) * t;
        currentBodyTiltZ_ += (targetBodyTiltZ_ - currentBodyTiltZ_) * t;
        if (abs(currentScaleX_ - targetScaleX_) < 0.01f) {
            currentScaleX_ = targetScaleX_;
        }
        if (abs(currentBodyTiltZ_ - targetBodyTiltZ_) < 0.001f) {
            currentBodyTiltZ_ = targetBodyTiltZ_;
        }
        legAnimator.Update(dt, isMoving, airborne != AirborneStatus::None);
        airborne_ = airborne;
        jumpProgress_ = jumpProgress;
    }

    void ApplyTransform(const Vector2 &in position)
    {
        if (sprite_ is null) {
            return;
        }
        const float bodyPhase = bodyAnimTimer_.GetProgress() * 2.0f * kPi;
        const float bodyBobY = sin(bodyPhase) * 3.0f;
        float bodyScaleY = 1.0f + sin(bodyPhase) * 0.025f;
        float bodyScaleX = 1.0f - sin(bodyPhase) * (0.025f * 0.5f);

        const bool isAirborne = airborne_ != AirborneStatus::None;
        const float airborneBlend = isAirborne ? 1.0f : 0.0f;
        const float takeoffBlend = (airborne_ == AirborneStatus::Jumping) ? (1.0f - jumpProgress_) : 0.0f;
        const float landingBlend = (airborne_ == AirborneStatus::None)
            ? bodyAnimTimer_.GetEasedProgress(Td3Ease::EaseOutQuad) : 0.0f;

        bodyScaleX *= 1.0f - airborneBlend * 0.06f - takeoffBlend * 0.04f + landingBlend * 0.10f * 0.15f;
        bodyScaleY *= 1.0f + airborneBlend * 0.10f + takeoffBlend * 0.08f - landingBlend * 0.12f * 0.15f;

        const float bodyJumpOffsetY = airborneBlend * 5.0f;
        const float texW = sprite_.GetTextureSize().x;
        const float uvScaleX = (texW > 0.0f) ? 64.0f / texW : 1.0f;
        sprite_.SetScale(abs(currentScaleX_) * bodyScaleX * uvScaleX, bodyScaleY);
        sprite_.SetRotate(Vector3(0.0f, 0.0f, currentBodyTiltZ_));
        sprite_.SetTranslate(position.x, position.y + 8.0f + bodyBobY + bodyJumpOffsetY);
        sprite_.SetFlipX(currentScaleX_ > 0.0f);

        legAnimator.Apply(position, currentScaleX_);
    }
}

class Player2D
{
    private Td3Sprite@ sprite_;
    PlayerVisualController visualController;
    private BlinkAnimationController blinkController_;
    private TileCollider collider_;
    private array<Vector2> hatBlockCenters_;

    private Vector2 position_;
    private Vector2 velocity_;
    private AirborneStatus airborneStatus_ = AirborneStatus::Falling;
    private Td3Timer jumpTimer_;
    private Td3Timer floatTimer_;
    private bool isMoving_ = false;

    private float moveSpeed_ = 384.0f;
    private float jumpSpeed_ = 700.0f;
    private float gravity_ = 1280.0f;
    private float jumpDuration_ = 0.2f;
    private float floatDuration_ = 0.2f;

    private bool hasBounds_ = false;
    private Vector2 mapBoundsMin_;
    private Vector2 mapBoundsMax_;

    private FallOutState fallOutState_ = FallOutState::Normal;
    private Vector2 safePosition_;
    private Vector2 returnStart_;
    private float returnTimer_ = 0.0f;
    private float deadDisplayTimer_ = 0.0f;

    void Initialize(Td3Sprite@ sprite, Td3Sprite@ legL, Td3Sprite@ legR, const Vector2 &in worldPos, TileMap@ tileMap)
    {
        @sprite_ = sprite;
        position_ = worldPos;
        velocity_ = Vector2(0.0f, 0.0f);
        @collider_.tileMap = tileMap;
        collider_.halfW = kPlayerHalfW;
        collider_.halfH = kPlayerHalfH;
        airborneStatus_ = AirborneStatus::Falling;
        isMoving_ = false;
        fallOutState_ = FallOutState::Normal;
        safePosition_ = worldPos;
        returnStart_ = worldPos;
        returnTimer_ = 0.0f;
        jumpTimer_.Reset();
        floatTimer_.Reset();

        visualController.Initialize(sprite, legL, legR);

        // 3 コマ（アイドル・瞬き・死んだ顔）
        if (sprite_ !is null) {
            const float texH = sprite_.GetTextureSize().y;
            Td3SpriteAnimator@ animator = sprite_.GetAnimator();
            animator.AddUniformClip("idle", 64.0f, texH, 1, 6.0f, true);
            animator.AddUniformClip("blink", 64.0f, texH, 1, 6.0f, false, 64.0f, 0.0f);
            animator.AddUniformClip("dead", 64.0f, texH, 1, 6.0f, true, 128.0f, 0.0f);
            animator.Play("idle");
        }
        blinkController_.Initialize(sprite_, "idle", "blink");

        // player2d_params.json の値
        moveSpeed_ = 384.0f;
        jumpSpeed_ = 700.0f;
        gravity_ = 1280.0f;
        jumpDuration_ = 0.20000000298023224f;
        floatDuration_ = 0.20000000298023224f;
        visualController.faceLerpSpeed = 12.0f;

        ApplySpriteTransform();
    }

    Vector2 GetPosition() const { return position_; }

    void SetPosition(const Vector2 &in position)
    {
        position_ = position;
        ApplySpriteTransform();
    }

    void SetMapBounds(float minX, float maxX, float minY, float maxY)
    {
        mapBoundsMin_ = Vector2(minX, minY);
        mapBoundsMax_ = Vector2(maxX, maxY);
        hasBounds_ = true;
    }

    bool OnGround() const { return airborneStatus_ == AirborneStatus::None; }
    FallOutState GetFallOutState() const { return fallOutState_; }

    void SetHatBlocks(const array<Vector2> &in blockCenters)
    {
        hatBlockCenters_ = blockCenters;
    }

    void Update(float dt)
    {
        UpdateFallOut(dt);
        if (fallOutState_ == FallOutState::FallingOut || fallOutState_ == FallOutState::Returning) {
            UpdateAnimation(dt);
            ApplySpriteTransform();
            return;
        }
        UpdateGroundState();
        UpdateAirborneState(dt);
        ResolveMovement(dt);
        UpdateAnimation(dt);
        ApplySpriteTransform();
        isMoving_ = false;
    }

    void MoveLeft() { Move(true); }
    void MoveRight() { Move(false); }

    private void Move(bool left)
    {
        velocity_.x = left ? -moveSpeed_ : moveSpeed_;
        isMoving_ = true;
        const float scaleX = left ? -1.0f : 1.0f;
        const float tiltZ = left ? -0.08f : 0.08f;
        visualController.SetFacingInput(scaleX, tiltZ);
    }

    bool Jump()
    {
        if (airborneStatus_ == AirborneStatus::None) {
            velocity_.y = jumpSpeed_;
            airborneStatus_ = AirborneStatus::Jumping;
            jumpTimer_.Start(jumpDuration_);
            return true;
        }
        return false;
    }

    private void UpdateGroundState()
    {
        if (airborneStatus_ != AirborneStatus::None) {
            return;
        }
        TileCollisionResult@ probe = collider_.Resolve(position_, Vector2(0.0f, -1.0f));
        bool onGround = probe.isOnGround;
        if (!onGround && hatBlockCenters_.length() > 0) {
            HatBlockResolveResult@ hatProbe = ResolveHatBlocks(position_, Vector2(0.0f, -1.0f), hatBlockCenters_,
                kPlayerHalfW, kPlayerHalfH);
            onGround = hatProbe.isOnGround;
        }
        if (!onGround) {
            airborneStatus_ = AirborneStatus::Falling;
            return;
        }

        // 接地中は、足元で中心に最も近い固いタイルの中心 X を安全な位置として覚える
        TileMap@ tm = collider_.tileMap;
        if (tm is null) {
            safePosition_ = position_;
            return;
        }
        const float ts = tm.tileSize;
        const Vector2 origin = tm.origin;
        const float scanY = position_.y - kPlayerHalfH - 1.0f;
        const float relY = scanY - origin.y;
        const int row = int(floor(-relY / ts));
        const int colLeft = int(floor((position_.x - kPlayerHalfW - origin.x) / ts));
        const int colRight = int(floor((position_.x + kPlayerHalfW - origin.x) / ts));
        float bestCenterX = 0.0f;
        float bestDist = 3.402823466e+38f;
        bool found = false;
        for (int c = colLeft; c <= colRight; ++c) {
            if (!tm.IsSolid(c, row)) {
                continue;
            }
            const float tileCenterX = origin.x + (float(c) + 0.5f) * ts;
            const float dist = abs(tileCenterX - position_.x);
            if (dist < bestDist) {
                bestDist = dist;
                bestCenterX = tileCenterX;
                found = true;
            }
        }
        safePosition_ = Vector2(found ? bestCenterX : position_.x, position_.y);
    }

    private void UpdateAirborneState(float dt)
    {
        Vector2 acceleration(0.0f, 0.0f);
        switch (airborneStatus_) {
        case AirborneStatus::Jumping:
            jumpTimer_.Update(dt);
            if (jumpTimer_.IsFinished()) {
                velocity_.y = 0.0f;
                jumpTimer_.Reset();
                airborneStatus_ = AirborneStatus::Floating;
                floatTimer_.Start(floatDuration_);
            }
            break;
        case AirborneStatus::Floating:
            floatTimer_.Update(dt);
            if (floatTimer_.IsFinished()) {
                floatTimer_.Reset();
                airborneStatus_ = AirborneStatus::Falling;
            }
            break;
        case AirborneStatus::Falling:
            acceleration.y = -gravity_;
            break;
        default:
            break;
        }
        velocity_ = velocity_ + acceleration * dt;
    }

    private void ResolveMovement(float dt)
    {
        const Vector2 frameVelocity(velocity_.x * dt, velocity_.y * dt);
        TileCollisionResult@ result = collider_.Resolve(position_, frameVelocity);

        // ハットが運んでいるブロックとの押し出し
        if (hatBlockCenters_.length() > 0) {
            const Vector2 afterStage(result.resolvedPosition.x - position_.x, result.resolvedPosition.y - position_.y);
            HatBlockResolveResult@ hatResult = ResolveHatBlocks(position_, afterStage, hatBlockCenters_,
                kPlayerHalfW, kPlayerHalfH);
            if (airborneStatus_ == AirborneStatus::None) {
                // 接地中は縦の押し出しを使わない（上下に往復してしまう）
                result.resolvedPosition.x = hatResult.resolvedPos.x;
            } else {
                result.resolvedPosition = hatResult.resolvedPos;
                result.hitCeiling = result.hitCeiling || hatResult.hitCeiling;
            }
            result.isOnGround = result.isOnGround || hatResult.isOnGround;
            result.hitWallLeft = result.hitWallLeft || hatResult.hitWallLeft;
            result.hitWallRight = result.hitWallRight || hatResult.hitWallRight;
        }

        position_ = result.resolvedPosition;

        // 横だけ画面に収める（下は落ちたことを見るので収めない）
        if (hasBounds_) {
            const float clampedX = Max(mapBoundsMin_.x + kPlayerHalfW, Min(mapBoundsMax_.x - kPlayerHalfW, position_.x));
            const float clampedY = Max(position_.y, Min(mapBoundsMax_.y - kPlayerHalfH, position_.y));
            position_ = Vector2(clampedX, clampedY);
        }

        ApplyCollisionResult(result.isOnGround, result.hitCeiling);
    }

    private void UpdateFallOut(float dt)
    {
        switch (fallOutState_) {
        case FallOutState::Normal:
            // 画面の下端より下へ出たら落ちたことにする
            if (hasBounds_ && (position_.y - kPlayerHalfH) < mapBoundsMin_.y - kPlayerHalfH) {
                fallOutState_ = FallOutState::FallingOut;
                deadDisplayTimer_ = 0.0f;
                velocity_ = Vector2(0.0f, 0.0f);
                airborneStatus_ = AirborneStatus::Falling;
            }
            break;
        case FallOutState::FallingOut:
            deadDisplayTimer_ += dt;
            if (deadDisplayTimer_ >= 0.8f) {
                fallOutState_ = FallOutState::Returning;
                returnStart_ = position_;
                returnTimer_ = 0.0f;
            }
            break;
        case FallOutState::Returning:
        {
            returnTimer_ += dt;
            const float t = returnTimer_ / 0.5f;
            const float clamped = (t < 0.0f) ? 0.0f : (t > 1.0f) ? 1.0f : t;
            const float smooth = clamped * clamped * (3.0f - 2.0f * clamped);
            position_.x = returnStart_.x + (safePosition_.x - returnStart_.x) * smooth;
            position_.y = returnStart_.y + (safePosition_.y - returnStart_.y) * smooth;
            if (returnTimer_ >= 0.5f) {
                position_ = safePosition_;
                velocity_ = Vector2(0.0f, 0.0f);
                airborneStatus_ = AirborneStatus::Falling;
                fallOutState_ = FallOutState::Normal;
            }
            break;
        }
        default:
            break;
        }
    }

    private void ApplyCollisionResult(bool isOnGround, bool hitCeiling)
    {
        if (isOnGround && velocity_.y <= 0.0f) {
            velocity_.y = 0.0f;
            jumpTimer_.Reset();
            floatTimer_.Reset();
            airborneStatus_ = AirborneStatus::None;
        }
        if (hitCeiling) {
            velocity_.y = 0.0f;
            if (airborneStatus_ == AirborneStatus::Jumping || airborneStatus_ == AirborneStatus::Floating) {
                airborneStatus_ = AirborneStatus::Falling;
            }
        }
        // 壁に当たった後は横の速さを 0 に戻す（次のフレームの移動で入れ直す）
        velocity_.x = 0.0f;
    }

    private void UpdateAnimation(float dt)
    {
        if (fallOutState_ == FallOutState::FallingOut) {
            if (sprite_ !is null && sprite_.GetAnimator().GetCurrentClipName() != "dead") {
                sprite_.GetAnimator().Play("dead");
            }
            visualController.Update(dt, false, airborneStatus_, 0.0f);
            return;
        }
        if (fallOutState_ == FallOutState::Normal && sprite_ !is null
            && sprite_.GetAnimator().GetCurrentClipName() == "dead") {
            sprite_.GetAnimator().Play("idle");
        }
        visualController.Update(dt, isMoving_, airborneStatus_, jumpTimer_.GetProgress());
        blinkController_.Update(dt);
    }

    private void ApplySpriteTransform()
    {
        visualController.ApplyTransform(position_);
    }
}
