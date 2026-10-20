using namespace CoreEngine;

// 自機とハットの 5×5 の範囲の枠と塗り（元の RangeFrame2D）。操作している側の枠に光が回る
const string kPlayerFrameMaterial = "Application/Assets/Materials/PlayerFrame.mat";
const string kHatFrameMaterial = "Application/Assets/Materials/HatFrame.mat";

class RangeFrame2D
{
    private Td3Sprite@ playerSprite_;
    private Td3Sprite@ hatSprite_;
    private Td3Sprite@ playerFillSprite_;
    private Td3Sprite@ hatFillSprite_;
    private ShaderMaterial@ playerMat_;
    private ShaderMaterial@ hatMat_;

    private float playerTime_ = 0.0f;
    private float hatTime_ = 0.0f;
    private Vector2 playerGlowCenter_;
    private Vector2 hatGlowCenter_;
    private bool hasPlayerGlowCenter_ = false;
    private bool hasHatGlowCenter_ = false;
    private float playerPunchTimer_ = 0.0f;
    private float hatPunchTimer_ = 0.0f;
    private float hatShakeTime_ = 0.0f;
    private float hatShakeDuration_ = 0.0f;
    private float hatShakeStrength_ = 0.0f;
    private int prevMode_ = 0;
    private bool hasPrevMode_ = false;

    void Initialize(Td3SpriteFactory@ factory)
    {
        @playerSprite_ = MakeFrame(factory, kPlayerFrameMaterial);
        @playerMat_ = ShaderMaterial(kPlayerFrameMaterial);
        playerMat_.SetVector("baseColor", kNeonPlayerDark);
        playerMat_.SetVector("glowColor", kNeonPlayerGlow);
        playerMat_.SetFloat("glowSharpness", 600.0f);
        playerMat_.SetFloat("glowCount", 2.0f);

        @hatSprite_ = MakeFrame(factory, kHatFrameMaterial);
        @hatMat_ = ShaderMaterial(kHatFrameMaterial);
        hatMat_.SetVector("baseColor", kNeonHatDark);
        hatMat_.SetVector("glowColor", kNeonHatGlow);
        hatMat_.SetFloat("glowSharpness", 200.0f);
        hatMat_.SetFloat("glowCount", 2.0f);

        // 塗り（枠より奥の層 10）
        const float span = float(kCopyRadius * 2 + 1) * kTileSize;
        @playerFillSprite_ = MakeFill(factory, Vector4(kNeonPlayerColor.x, kNeonPlayerColor.y, kNeonPlayerColor.z, 0.08f), span);
        @hatFillSprite_ = MakeFill(factory, Vector4(kNeonHatColor.x, kNeonHatColor.y, kNeonHatColor.z, 0.08f), span);
    }

    private Td3Sprite@ MakeFrame(Td3SpriteFactory@ factory, const string &in material)
    {
        const float span = float(kCopyRadius * 2 + 1) * kTileSize;
        const float half = span * 0.5f;
        Td3Sprite@ sprite = factory.Create("white1x1.png", "Frame2D");
        sprite.SetColor(Vector4(1.0f, 1.0f, 1.0f, 1.0f));
        sprite.SetSortingLayer(11);
        sprite.SetAnchor(Vector2(0.5f, 0.5f));
        sprite.SetScale(span, span);
        sprite.SetTranslate(0.0f, 0.0f);
        sprite.SetMaterial(material);
        ShaderMaterial@ mat = ShaderMaterial(material);
        mat.SetFloat("thickness", 6.0f);
        mat.SetFloat("cornerRadius", 24.0f);
        mat.SetFloat("halfSpan", half);
        mat.SetFloat("drawHalfSpan", half);
        mat.SetFloat("edgeStart", 0.0f);
        return sprite;
    }

    private Td3Sprite@ MakeFill(Td3SpriteFactory@ factory, const Vector4 &in color, float span)
    {
        Td3Sprite@ sprite = factory.Create("white1x1.png", "RangeFill");
        sprite.SetColor(color);
        sprite.SetSortingLayer(10);
        sprite.SetAnchor(Vector2(0.5f, 0.5f));
        sprite.SetScale(span, span);
        sprite.SetTranslate(0.0f, 0.0f);
        return sprite;
    }

    // activeMode は 0 = 自機を操作中 ／ 1 = ハットを操作中
    void Update(const Vector2 &in playerPos, const Vector2 &in hatPos, float dt, int activeMode)
    {
        playerSprite_.SetTranslate(playerPos);
        playerFillSprite_.SetTranslate(playerPos);

        Vector2 shakeOffset(0.0f, 0.0f);
        if (hatShakeTime_ > 0.0f) {
            hatShakeTime_ -= dt;
            if (hatShakeTime_ < 0.0f) {
                hatShakeTime_ = 0.0f;
            }
            const float decay = (hatShakeDuration_ > 0.0f) ? (hatShakeTime_ / hatShakeDuration_) : 0.0f;
            const float amp = hatShakeStrength_ * decay;
            shakeOffset.x = Td3RandSigned() * amp;
            shakeOffset.y = Td3RandSigned() * amp;
        }
        hatSprite_.SetTranslate(hatPos.x + shakeOffset.x, hatPos.y + shakeOffset.y);
        hatFillSprite_.SetTranslate(hatPos.x + shakeOffset.x, hatPos.y + shakeOffset.y);

        if (!hasPlayerGlowCenter_) {
            playerGlowCenter_ = playerPos;
            hasPlayerGlowCenter_ = true;
        }
        if (!hasHatGlowCenter_) {
            hatGlowCenter_ = hatPos;
            hasHatGlowCenter_ = true;
        }
        if (dt > 0.0f) {
            // 光の尾は中心を遅れて追う
            playerGlowCenter_ = UpdateGlowLagCenter(playerGlowCenter_, playerPos, dt);
            hatGlowCenter_ = UpdateGlowLagCenter(hatGlowCenter_, hatPos, dt);
            const Vector2 playerLag = playerGlowCenter_ - playerPos;
            const Vector2 hatLag = hatGlowCenter_ - hatPos;
            playerMat_.SetVector("glowLagOffset", Vector4(playerLag.x, playerLag.y, 0.0f, 0.0f));
            hatMat_.SetVector("glowLagOffset", Vector4(hatLag.x, hatLag.y, 0.0f, 0.0f));
        }
        if (dt <= 0.0f) {
            return;
        }

        const bool playerActive = activeMode == 0;
        const bool hatActive = activeMode == 1;
        // 操作が切り替わったら、操作する側の枠を膨らませる
        if (hasPrevMode_ && prevMode_ != activeMode) {
            if (playerActive) {
                playerPunchTimer_ = 0.25f;
            }
            if (hatActive) {
                hatPunchTimer_ = 0.25f;
            }
        }
        prevMode_ = activeMode;
        hasPrevMode_ = true;
        playerPunchTimer_ = Max(playerPunchTimer_ - dt, 0.0f);
        hatPunchTimer_ = Max(hatPunchTimer_ - dt, 0.0f);

        playerTime_ += dt;
        hatTime_ += dt;
        UpdateFrameActive(playerMat_, playerTime_, playerActive, kNeonPlayerDark, kNeonPlayerGlow,
            kNeonPlayerInactive, kNeonPlayerGlowInactive, 2.0f);
        UpdateFrameActive(hatMat_, hatTime_, hatActive, kNeonHatDark, kNeonHatGlow,
            kNeonHatInactive, kNeonHatGlowInactive, 2.0f);

        UpdateFrameScale(playerSprite_, CalcPunchScale(playerPunchTimer_));
        UpdateFrameScale(hatSprite_, CalcPunchScale(hatPunchTimer_));
        UpdateFrameScale(playerFillSprite_, CalcPunchScale(playerPunchTimer_));
        UpdateFrameScale(hatFillSprite_, CalcPunchScale(hatPunchTimer_));
    }

    void SetPlayerVisible(bool visible)
    {
        playerSprite_.SetActive(visible);
        playerFillSprite_.SetActive(visible);
    }

    void SetHatVisible(bool visible)
    {
        hatSprite_.SetActive(visible);
        hatFillSprite_.SetActive(visible);
    }

    void StartHatShake(float duration, float strength)
    {
        hatShakeDuration_ = duration;
        hatShakeTime_ = duration;
        hatShakeStrength_ = strength;
    }

    private Vector2 UpdateGlowLagCenter(const Vector2 &in current, const Vector2 &in targetCenter, float dt) const
    {
        const float followRate = Clamp(dt * 3.0f, 0.0f, 1.0f);
        Vector2 lagCenter = current + (targetCenter - current) * followRate;
        const Vector2 offset = lagCenter - targetCenter;
        const float distance = Length(offset);
        if (distance > 96.0f) {
            lagCenter = targetCenter + offset * (96.0f / distance);
        }
        return lagCenter;
    }

    private void UpdateFrameActive(ShaderMaterial@ mat, float time, bool active,
        const Vector4 &in activeBase, const Vector4 &in activeGlow, const Vector4 &in inactiveBase,
        const Vector4 &in inactiveGlow, float activeGlowCount)
    {
        mat.SetFloat("time", time);
        mat.SetFloat("speed", active ? 0.3f : 0.15f);
        mat.SetVector("baseColor", active ? activeBase : inactiveBase);
        mat.SetVector("glowColor", active ? activeGlow : inactiveGlow);
        // 操作していない側の光は消す
        mat.SetFloat("glowCount", active ? activeGlowCount : 0.0f);
    }

    private float CalcPunchScale(float timer) const
    {
        if (timer <= 0.0f) {
            return 1.0f;
        }
        const float t = timer / 0.25f;
        return 1.0f + (1.25f - 1.0f) * sin(t * kPi);
    }

    private void UpdateFrameScale(Td3Sprite@ sprite, float punchScale)
    {
        const float span = float(kCopyRadius * 2 + 1) * kTileSize * punchScale;
        sprite.SetScale(span, span);
    }
}
