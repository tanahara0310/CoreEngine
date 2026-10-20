using namespace CoreEngine;

// 壊せるブロックの見た目（元の NormalBlockAnimator）。踏まれたら弾んで光り、ハットと自機の範囲を描き分ける

const string kStageBlockMaterial = "Application/Assets/Materials/StageBlock.mat";
const string kHatBlockMaterial = "Application/Assets/Materials/HatBlock.mat";
const string kCopyBlockMaterial = "Application/Assets/Materials/CopyBlock.mat";

class NormalBlockState
{
    int key = 0;
    Td3Sprite@ sprite;
    float bounceTime = -1.0f;
    float treadBlend = 0.0f;
    bool isTrodden = false;
    float rippleTime = -1.0f;
    float baseScale = 64.0f;
    bool isHatBlock = false;
    // 部品ごとに渡した値（変わったときだけ渡し直す）
    float appliedTread = -100.0f;
    float appliedRipple = -100.0f;
}

class NormalBlockAnimator
{
    private array<NormalBlockState@> states_;
    private array<int> unbreakableKeys_;
    private array<Td3Sprite@> flashSprites_;
    private array<Vector4> flashColors_;
    private array<Td3Sprite@> copiedSprites_;
    private float globalTime_ = 0.0f;

    private bool hatDiscardEnabled_ = false;
    private Vector2 hatDiscardCenter_;
    private float hatDiscardHalfSize_ = 0.0f;
    private bool hasPlayerDiscardCenter_ = false;
    private Vector2 playerDiscardCenter_;
    private float playerDiscardHalfSize_ = 0.0f;

    private ShaderMaterial@ stageMaterial_;
    private ShaderMaterial@ hatMaterial_;
    private ShaderMaterial@ copyMaterial_;

    void SetUnbreakableKeys(const array<int> &in keys)
    {
        unbreakableKeys_ = keys;
    }

    void Initialize(SpriteGrid@ blockSprites)
    {
        @stageMaterial_ = ShaderMaterial(kStageBlockMaterial);
        @hatMaterial_ = ShaderMaterial(kHatBlockMaterial);
        @copyMaterial_ = ShaderMaterial(kCopyBlockMaterial);
        states_.resize(0);
        for (uint key = 0; key < blockSprites.sprites.length(); ++key) {
            Td3Sprite@ sprite = blockSprites.sprites[key];
            if (sprite is null) {
                continue;
            }
            // 壊せないブロックはこの見た目にしない
            if (unbreakableKeys_.find(int(key)) >= 0) {
                continue;
            }
            NormalBlockState state;
            state.key = int(key);
            @state.sprite = sprite;
            state.baseScale = sprite.GetScale().x;
            sprite.SetMaterial(kStageBlockMaterial);
            states_.insertLast(state);
        }
    }

    // ハットが持ち運んでいる写し。フラッシュ中は赤い色・それ以外は自機の範囲を描かない
    void ReapplyCopiedSprites(const array<Td3Sprite@>@ sprites, bool hasPlayerCenter, const Vector2 &in playerCenter,
        float playerHalfSize)
    {
        copyMaterial_.SetFloat("time", globalTime_);
        if (hasPlayerCenter && playerHalfSize > 0.0f) {
            copyMaterial_.SetVector("playerRect", Vector4(playerCenter.x, playerCenter.y, playerHalfSize, playerHalfSize));
            copyMaterial_.SetFloat("ghostMode", 1.0f);
        } else {
            copyMaterial_.SetVector("playerRect", Vector4(0.0f, 0.0f, 0.0f, 0.0f));
            copyMaterial_.SetFloat("ghostMode", 0.0f);
        }
        copyMaterial_.SetVector("hatRect", Vector4(0.0f, 0.0f, 0.0f, 0.0f));

        for (uint i = 0; i < sprites.length(); ++i) {
            Td3Sprite@ sprite = sprites[i];
            if (sprite is null) {
                continue;
            }
            if (copiedSprites_.findByRef(sprite) < 0) {
                copiedSprites_.insertLast(sprite);
                sprite.ClearMaterialOverrides();
            }
            sprite.SetMaterial(kCopyBlockMaterial);
            const int flashIndex = flashSprites_.findByRef(sprite);
            if (flashIndex >= 0) {
                sprite.SetMaterialVector("hatRect", Vector4(0.0f, 0.0f, 0.0f, 0.0f));
                sprite.SetMaterialVector("playerRect", Vector4(0.0f, 0.0f, 0.0f, 0.0f));
                sprite.SetMaterialFloat("ghostMode", 0.0f);
                sprite.SetMaterialVector("color", flashColors_[uint(flashIndex)]);
                sprite.SetMaterialVector("accentColor", Vector4(0.0f, 0.0f, 0.0f, 0.0f));
            }
        }
        // 写しでなくなった物を外す
        for (int i = int(copiedSprites_.length()) - 1; i >= 0; --i) {
            if (sprites.findByRef(copiedSprites_[uint(i)]) < 0) {
                copiedSprites_.removeAt(uint(i));
            }
        }
    }

    void SetCopiedSpriteFlashColor(Td3Sprite@ sprite, const Vector4 &in color)
    {
        const int index = flashSprites_.findByRef(sprite);
        if (index >= 0) {
            flashColors_[uint(index)] = color;
            return;
        }
        flashSprites_.insertLast(sprite);
        flashColors_.insertLast(color);
    }

    void ClearCopiedSpriteFlashColor(Td3Sprite@ sprite)
    {
        const int index = flashSprites_.findByRef(sprite);
        if (index >= 0) {
            flashSprites_.removeAt(uint(index));
            flashColors_.removeAt(uint(index));
        }
        if (copiedSprites_.findByRef(sprite) >= 0) {
            sprite.ClearMaterialOverrides();
        }
    }

    void ClearAllFlashColors()
    {
        for (uint i = 0; i < flashSprites_.length(); ++i) {
            if (copiedSprites_.findByRef(flashSprites_[i]) >= 0) {
                flashSprites_[i].ClearMaterialOverrides();
            }
        }
        flashSprites_.resize(0);
        flashColors_.resize(0);
    }

    void Update(float dt, const Vector2 &in playerPos, float tileSize, float playerHalfH)
    {
        globalTime_ += dt;
        const float halfTile = tileSize * 0.5f;
        const float standThresh = 8.0f;

        for (uint i = 0; i < states_.length(); ++i) {
            NormalBlockState@ state = states_[i];
            if (state.sprite is null) {
                continue;
            }
            // 自機の足元がブロックの上面にあれば踏まれている
            const Vector2 blockPos = state.sprite.GetTranslate();
            const float dx = abs(playerPos.x - blockPos.x);
            const float feetY = playerPos.y - playerHalfH;
            const float blockTopY = blockPos.y + halfTile;
            const bool nowTrodden = (dx < halfTile) && (feetY <= blockTopY + standThresh)
                && (feetY >= blockTopY - standThresh - playerHalfH);
            if (nowTrodden && !state.isTrodden) {
                state.bounceTime = 0.0f;
                state.rippleTime = 0.0f;
            }
            state.isTrodden = nowTrodden;

            if (nowTrodden) {
                state.treadBlend += dt * 6.0f;
                if (state.treadBlend > 1.0f) {
                    state.treadBlend = 1.0f;
                }
            } else {
                state.treadBlend -= dt * 3.0f;
                if (state.treadBlend < 0.0f) {
                    state.treadBlend = 0.0f;
                }
            }

            // 弾む（縮む → 少し膨らむ → 戻る）
            float scale = state.baseScale;
            if (state.bounceTime >= 0.0f) {
                state.bounceTime += dt;
                if (state.bounceTime < 0.25f) {
                    const float t = state.bounceTime / 0.25f;
                    const float bounce = sin(t * 3.14159265f * 2.5f) * (1.0f - t);
                    scale = state.baseScale * (1.0f - 0.12f * bounce);
                } else {
                    state.bounceTime = -1.0f;
                }
            }
            state.sprite.SetScale(scale, scale);

            if (state.rippleTime >= 0.0f) {
                state.rippleTime += dt;
                if (state.rippleTime > 0.5f) {
                    state.rippleTime = -1.0f;
                }
            }

            if (state.appliedTread != state.treadBlend) {
                state.appliedTread = state.treadBlend;
                state.sprite.SetMaterialFloat("treadBlend", state.treadBlend);
            }
            if (state.appliedRipple != state.rippleTime) {
                state.appliedRipple = state.rippleTime;
                state.sprite.SetMaterialFloat("rippleTime", state.rippleTime);
            }
        }

        // ハットと自機の範囲・時間は全部のブロックで同じ
        const Vector4 hatRect = hatDiscardEnabled_
            ? Vector4(hatDiscardCenter_.x, hatDiscardCenter_.y, hatDiscardHalfSize_, hatDiscardHalfSize_)
            : Vector4(0.0f, 0.0f, 0.0f, 0.0f);
        const Vector4 playerRect = (hasPlayerDiscardCenter_ && playerDiscardHalfSize_ > 0.0f)
            ? Vector4(playerDiscardCenter_.x, playerDiscardCenter_.y, playerDiscardHalfSize_, playerDiscardHalfSize_)
            : Vector4(0.0f, 0.0f, 0.0f, 0.0f);
        stageMaterial_.SetVector("hatRect", hatRect);
        stageMaterial_.SetVector("playerRect", playerRect);
        stageMaterial_.SetFloat("time", globalTime_);
        hatMaterial_.SetVector("hatRect", hatRect);
        hatMaterial_.SetVector("playerRect", playerRect);
        hatMaterial_.SetFloat("time", globalTime_);
    }

    void SetHatDiscardRect(bool enabled, const Vector2 &in hatCenter, float hatHalfSize, bool hasPlayerCenter,
        const Vector2 &in playerCenter, float playerHalfSize)
    {
        hatDiscardEnabled_ = enabled;
        hatDiscardCenter_ = hatCenter;
        hatDiscardHalfSize_ = hatHalfSize;
        hasPlayerDiscardCenter_ = hasPlayerCenter;
        playerDiscardCenter_ = playerCenter;
        playerDiscardHalfSize_ = playerHalfSize;
    }

    // ステージのスプライトの並びが変わったら、見た目の状態を付け直す
    void Reapply(SpriteGrid@ blockSprites, const array<Td3Sprite@>@ hatSprites)
    {
        for (uint key = 0; key < blockSprites.sprites.length(); ++key) {
            Td3Sprite@ sprite = blockSprites.sprites[key];
            if (sprite is null) {
                continue;
            }
            if (unbreakableKeys_.find(int(key)) >= 0) {
                continue;
            }
            const bool isHat = hatSprites.findByRef(sprite) >= 0;
            NormalBlockState@ state = FindState(int(key));
            if (state is null) {
                NormalBlockState newState;
                newState.key = int(key);
                @newState.sprite = sprite;
                newState.baseScale = sprite.GetScale().x;
                newState.isHatBlock = isHat;
                states_.insertLast(newState);
                AssignMaterial(newState);
            } else {
                if (state.sprite !is sprite) {
                    @state.sprite = sprite;
                    state.baseScale = sprite.GetScale().x;
                    state.appliedTread = -100.0f;
                    state.appliedRipple = -100.0f;
                }
                state.isHatBlock = isHat;
                AssignMaterial(state);
            }
        }
        // 並びから消えたキーを外す
        for (int i = int(states_.length()) - 1; i >= 0; --i) {
            if (blockSprites.Find(states_[uint(i)].key) is null) {
                states_.removeAt(uint(i));
            }
        }
    }

    // 毎フレーム、状態を持つブロックの見た目を付け直す
    void ReapplyOverrides()
    {
        for (uint i = 0; i < states_.length(); ++i) {
            if (states_[i].sprite !is null) {
                AssignMaterial(states_[i]);
            }
        }
    }

    private void AssignMaterial(NormalBlockState@ state)
    {
        const string material = state.isHatBlock ? kHatBlockMaterial : kStageBlockMaterial;
        if (state.sprite.GetMaterial() != material) {
            state.sprite.ClearMaterialOverrides();
            state.appliedTread = -100.0f;
            state.appliedRipple = -100.0f;
        }
        state.sprite.SetMaterial(material);
    }

    private NormalBlockState@ FindState(int key)
    {
        for (uint i = 0; i < states_.length(); ++i) {
            if (states_[i].key == key) {
                return states_[i];
            }
        }
        return null;
    }
}
