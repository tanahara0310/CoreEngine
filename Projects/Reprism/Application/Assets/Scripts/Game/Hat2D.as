using namespace CoreEngine;

// ハット（元の Hat2D・HatOverlapFlash・HatStackEffect）

const float kHatHalfW = 30.0f;
const float kHatHalfH = 30.0f;

enum HatDirection
{
    Up,
    Down,
    Left,
    Right
}

// 置けないとき：自機の範囲と重なった板を赤く点滅させ、ハットと板を揺らす（その間は操作を受けない）
class HatOverlapFlash
{
    private TileMap@ tileMap_;
    NormalBlockAnimator@ blockAnimator;
    private array<Td3Sprite@> flashSprites_;
    private array<Td3Sprite@> shakeSprites_;
    private array<Vector2> shakeOrigins_;
    private Td3Timer flashTimer_;
    private Td3Timer flashInterval_;
    private bool flashOn_ = false;

    void Initialize(TileMap@ tileMap)
    {
        @tileMap_ = tileMap;
    }

    bool IsActive() const { return flashTimer_.IsActive(); }

    void Start(const Vector2 &in hatPos, const Vector2 &in playerPos, Td3Sprite@ hatSprite,
        const array<Td3Sprite@> &in copiedSprites, const array<Vector2> &in blockPositions)
    {
        if (tileMap_ is null) {
            return;
        }
        ResetFlash();
        int hcx, hcy, pcx, pcy;
        if (!tileMap_.WorldToTile(hatPos, hcx, hcy) || !tileMap_.WorldToTile(playerPos, pcx, pcy)) {
            return;
        }
        const int cols = tileMap_.cols;
        const int rows = tileMap_.rows;

        array<int> blockCols;
        array<int> blockRows;
        for (uint i = 0; i < blockPositions.length(); ++i) {
            int c, r;
            if (tileMap_.WorldToTile(blockPositions[i], c, r)) {
                blockCols.insertLast(c);
                blockRows.insertLast(r);
            } else {
                blockCols.insertLast(-1);
                blockRows.insertLast(-1);
            }
        }

        for (int dy = -kCopyRadius; dy <= kCopyRadius; ++dy) {
            for (int dx = -kCopyRadius; dx <= kCopyRadius; ++dx) {
                const int col = hcx + dx;
                const int row = hcy + dy;
                if (col < 0 || col >= cols || row < 0 || row >= rows) {
                    continue;
                }
                if (Td3Abs(col - pcx) > kCopyRadius || Td3Abs(row - pcy) > kCopyRadius) {
                    continue;
                }
                for (uint i = 0; i < blockCols.length(); ++i) {
                    if (blockCols[i] == col && blockRows[i] == row) {
                        flashSprites_.insertLast(copiedSprites[i]);
                        break;
                    }
                }
            }
        }

        if (hatSprite !is null) {
            shakeSprites_.insertLast(hatSprite);
            shakeOrigins_.insertLast(hatPos);
        }
        for (uint i = 0; i < copiedSprites.length(); ++i) {
            if (copiedSprites[i] !is null) {
                shakeSprites_.insertLast(copiedSprites[i]);
                shakeOrigins_.insertLast(blockPositions[i]);
            }
        }

        flashTimer_.Start(0.5f);
        flashInterval_.Start(0.05f, true);
        flashOn_ = true;
        ApplyFlashColor(Vector4(1.0f, 0.0f, 0.0f, 1.0f));
    }

    void Update(float dt)
    {
        if (!flashTimer_.IsActive()) {
            return;
        }
        flashTimer_.Update(dt);
        flashInterval_.Update(dt);
        if (flashInterval_.HasLooped()) {
            flashOn_ = !flashOn_;
            ApplyFlashColor(flashOn_ ? Vector4(1.0f, 0.0f, 0.0f, 1.0f) : Vector4(0.0f, 0.0f, 0.0f, 1.0f));
        }
        const float damping = 1.0f - flashTimer_.GetProgress();
        const float elapsed = flashTimer_.GetElapsedTime();
        const float shiftX = sin(elapsed * 40.0f) * 6.0f * damping;
        for (uint i = 0; i < shakeSprites_.length(); ++i) {
            if (shakeSprites_[i] is null) {
                continue;
            }
            shakeSprites_[i].SetTranslate(shakeOrigins_[i].x + shiftX, shakeOrigins_[i].y);
        }
        if (flashTimer_.IsFinished()) {
            ResetFlash();
        }
    }

    private void ApplyFlashColor(const Vector4 &in color)
    {
        if (blockAnimator is null) {
            return;
        }
        for (uint i = 0; i < flashSprites_.length(); ++i) {
            if (flashSprites_[i] !is null) {
                blockAnimator.SetCopiedSpriteFlashColor(flashSprites_[i], color);
            }
        }
    }

    private void ResetFlash()
    {
        if (blockAnimator !is null) {
            for (uint i = 0; i < flashSprites_.length(); ++i) {
                if (flashSprites_[i] !is null) {
                    blockAnimator.ClearCopiedSpriteFlashColor(flashSprites_[i]);
                }
            }
            blockAnimator.ClearAllFlashColors();
        }
        for (uint i = 0; i < shakeSprites_.length(); ++i) {
            if (shakeSprites_[i] is null) {
                continue;
            }
            shakeSprites_[i].SetTranslate(shakeOrigins_[i]);
        }
        flashSprites_.resize(0);
        shakeSprites_.resize(0);
        shakeOrigins_.resize(0);
        flashTimer_.Reset();
        flashInterval_.Reset();
    }
}

// 詰まって動けないとき：「動けない」の表示を出して揺らす
class HatStackEffect
{
    private Td3Sprite@ uiSprite_;
    private RangeFrame2D@ rangeFrame_;
    private Td3Timer uiTimer_;
    private Td3Timer shakeTimer_;
    float displayTime = 1.0f;
    float shakeDuration = 1.0f;
    float shakeStrength = 20.0f;

    void Initialize(Td3Sprite@ uiSprite, RangeFrame2D@ rangeFrame)
    {
        @uiSprite_ = uiSprite;
        @rangeFrame_ = rangeFrame;
    }

    void TriggerStuck()
    {
        uiTimer_.Start(displayTime);
        shakeTimer_.Start(shakeDuration);
        if (rangeFrame_ !is null) {
            rangeFrame_.StartHatShake(shakeDuration, shakeStrength);
        }
    }

    bool IsActive() const { return uiTimer_.IsActive(); }

    void Update(float dt, const Vector2 &in hatPos, bool isStuck)
    {
        if (uiSprite_ is null || dt <= 0.0f) {
            return;
        }
        if (shakeTimer_.IsActive()) {
            shakeTimer_.Update(dt);
            if (shakeTimer_.IsFinished()) {
                shakeTimer_.Reset();
            }
        }
        if (!isStuck || !uiTimer_.IsActive()) {
            uiSprite_.SetTranslate(hatPos);
            uiSprite_.SetColor(Vector4(1.0f, 1.0f, 1.0f, 0.0f));
            return;
        }
        uiTimer_.Update(dt);
        const float elapsed = uiTimer_.GetElapsedTime();
        const float alpha = (elapsed < displayTime) ? 1.0f : 0.0f;
        Vector2 shakeOffset(0.0f, 0.0f);
        if (shakeTimer_.IsActive()) {
            const float decay = 1.0f - (shakeTimer_.GetElapsedTime() / shakeDuration);
            const float amp = shakeStrength * decay;
            shakeOffset.x = Td3RandSigned() * amp;
            shakeOffset.y = Td3RandSigned() * amp;
        }
        uiSprite_.SetTranslate(hatPos.x + shakeOffset.x, hatPos.y + shakeOffset.y);
        uiSprite_.SetColor(Vector4(1.0f, 1.0f, 1.0f, alpha));
        if (uiTimer_.IsFinished()) {
            uiTimer_.Reset();
        }
    }
}

class Hat2D
{
    private Td3Sprite@ sprite_;
    private TileMap@ tileMap_;
    private TileCollider collider_;
    private bool isActionMode_ = false;
    private bool isStuck_ = false;

    private Vector2 position_;
    private Vector2 lastSafePosition_;
    private Vector2 inputVelocity_;
    private bool wasMoving_ = false;
    private float bobAnimationTimer_ = 0.0f;
    private float targetFacingScaleX_ = -1.0f;
    private float currentFacingScaleX_ = -1.0f;

    private bool hasPlayerWorldPos_ = false;
    private Vector2 playerWorldPos_;
    private bool hasSnapTarget_ = false;
    private Vector2 snapTarget_;

    private float moveSpeed_ = 384.0f;
    private float snapLerpSpeed_ = 20.0f;

    private bool hasBounds_ = false;
    private Vector2 mapBoundsMin_;
    private Vector2 mapBoundsMax_;

    HatBlockManager blockManager;
    private HatOverlapFlash overlapFlash_;
    private BlinkAnimationController blinkController_;
    private HatStackEffect stackEffect_;
    private RangeFrame2D@ rangeFrame_;

    private bool stuckMoveAttempted_ = false;
    private int stuckConsecutiveFrames_ = 0;

    void Initialize(Td3Sprite@ sprite, const Vector2 &in worldPos, TileMap@ tileMap, Td3SpriteFactory@ factory)
    {
        @sprite_ = sprite;
        @tileMap_ = tileMap;
        position_ = worldPos;
        lastSafePosition_ = worldPos;
        inputVelocity_ = Vector2(0.0f, 0.0f);
        bobAnimationTimer_ = 0.0f;
        targetFacingScaleX_ = -1.0f;
        currentFacingScaleX_ = -1.0f;

        @collider_.tileMap = tileMap;
        collider_.halfW = kHatHalfW;
        collider_.halfH = kHatHalfH;

        blockManager.Initialize(tileMap, factory);
        overlapFlash_.Initialize(tileMap);

        // 写したブロック（層 1）より手前
        if (sprite_ !is null) {
            sprite_.SetSortingLayer(2);
            const float texH = sprite_.GetTextureSize().y;
            Td3SpriteAnimator@ animator = sprite_.GetAnimator();
            animator.AddUniformClip("idle", 64.0f, texH, 1, 6.0f, true);
            animator.AddUniformClip("blink", 64.0f, texH, 1, 6.0f, false, 64.0f, 0.0f);
            animator.Play("idle");
        }
        blinkController_.Initialize(sprite_, "idle", "blink");

        // hat2d_params.json の値
        moveSpeed_ = 384.0f;
        snapLerpSpeed_ = 20.0f;

        // 詰まったときの表示（右肩上がり 45 度）
        Td3Sprite@ stackUiSprite = factory.Create("UI/UI_Stuck.png", "HatStackUI");
        if (stackUiSprite !is null) {
            stackUiSprite.SetAnchor(Vector2(0.5f, 0.5f));
            stackUiSprite.SetSortingLayer(5);
            stackUiSprite.SetRotate(Vector3(0.0f, 0.0f, -kPi / 4.0f));
            stackUiSprite.SetScale(1.0f, 1.0f);
            stackUiSprite.SetTranslate(position_);
            stackUiSprite.SetColor(Vector4(1.0f, 1.0f, 1.0f, 0.0f));
        }
        // 範囲の枠はまだ渡されていないので、詰まっても枠は揺れない
        stackEffect_.Initialize(stackUiSprite, rangeFrame_);

        ApplySpriteTransform();
    }

    Vector2 GetPosition() const { return position_; }
    bool IsActionMode() const { return isActionMode_; }
    bool IsStuck() const { return isStuck_; }
    bool IsBlocked() const { return overlapFlash_.IsActive(); }
    bool IsSpawnAnimationPlaying() const { return !blockManager.IsSpawnAnimationFinished(); }
    const array<Vector2>@ GetBlockWorldPositions() const { return blockManager.GetBlockWorldPositions(); }
    const array<Td3Sprite@>@ GetCopiedSprites() const { return blockManager.GetCopiedSprites(); }

    array<Td3Sprite@>@ GetCommittedHatSprites() const
    {
        array<Td3Sprite@> result;
        for (uint i = 0; i < blockManager.tileState.committedCells.length(); ++i) {
            Td3Sprite@ sprite = blockManager.tileState.committedCells[i].hatSprite;
            if (sprite !is null) {
                result.insertLast(sprite);
            }
        }
        return result;
    }

    array<HatUndoCommittedCell@>@ CaptureUndoCommittedCells() const
    {
        return blockManager.CaptureUndoCommittedCells();
    }

    bool PopStuckMoveAttempt()
    {
        const bool value = stuckMoveAttempted_;
        stuckMoveAttempted_ = false;
        return value;
    }

    void SetStageSpriteMap(SpriteGrid@ map) { blockManager.SetStageSpriteMap(map); }
    void SetRangeFrame(RangeFrame2D@ rangeFrame) { @rangeFrame_ = rangeFrame; }
    void SetBlockAnimator(NormalBlockAnimator@ animator) { @overlapFlash_.blockAnimator = animator; }

    void SetMapBounds(float minX, float maxX, float minY, float maxY)
    {
        mapBoundsMin_ = Vector2(minX, minY);
        mapBoundsMax_ = Vector2(maxX, maxY);
        hasBounds_ = true;
    }

    void SetPlayerWorldPos(bool hasPosition, const Vector2 &in position)
    {
        hasPlayerWorldPos_ = hasPosition;
        playerWorldPos_ = position;
        blockManager.SetPlayerWorldPos(hasPosition, position);
    }

    void SetPosition(const Vector2 &in position)
    {
        position_ = position;
        lastSafePosition_ = position;
        ApplySpriteTransform();
    }

    void Move(HatDirection direction)
    {
        switch (direction) {
        case HatDirection::Up:    inputVelocity_.y += 1.0f; break;
        case HatDirection::Down:  inputVelocity_.y -= 1.0f; break;
        case HatDirection::Left:  inputVelocity_.x -= 1.0f; break;
        case HatDirection::Right: inputVelocity_.x += 1.0f; break;
        }
    }

    void Update(float dt)
    {
        UpdateMovement(dt);
        UpdateVisualAnimation(dt);
        if (isActionMode_) {
            blockManager.UpdateClearedArea(position_);
        }
        UpdateCopiedBlocks(dt);
        overlapFlash_.Update(dt);
        blinkController_.Update(dt);
        // 揺れている間は点滅の側が位置を持つ
        if (!overlapFlash_.IsActive()) {
            ApplySpriteTransform();
        }
        stackEffect_.Update(dt, position_, isStuck_);
    }

    private void UpdateMovement(float dt)
    {
        // 詰まっている間は入力を捨てて動かない
        if (isStuck_) {
            const float len = inputVelocity_.x * inputVelocity_.x + inputVelocity_.y * inputVelocity_.y;
            if (len > 0.0f && stuckConsecutiveFrames_ >= 2 && !stackEffect_.IsActive()) {
                stuckMoveAttempted_ = true;
                stackEffect_.TriggerStuck();
            }
            inputVelocity_ = Vector2(0.0f, 0.0f);
            hasSnapTarget_ = false;
            return;
        }

        lastSafePosition_ = position_;

        const float len = sqrt(inputVelocity_.x * inputVelocity_.x + inputVelocity_.y * inputVelocity_.y);
        const bool isMoving = len > 0.001f;
        if (inputVelocity_.x < -0.001f) {
            targetFacingScaleX_ = -1.0f;
        } else if (inputVelocity_.x > 0.001f) {
            targetFacingScaleX_ = 1.0f;
        }

        if (isMoving) {
            hasSnapTarget_ = false;
            const Vector2 norm(inputVelocity_.x / len, inputVelocity_.y / len);
            const Vector2 delta(norm.x * moveSpeed_ * dt, norm.y * moveSpeed_ * dt);
            // 斜めは X と Y を分けて解決し、角に引っかからないようにする
            const bool isDiagonal = abs(delta.x) > 0.001f && abs(delta.y) > 0.001f;
            if (isDiagonal) {
                const Vector2 afterX = collider_.Resolve(position_, Vector2(delta.x, 0.0f)).resolvedPosition;
                position_ = collider_.Resolve(afterX, Vector2(0.0f, delta.y)).resolvedPosition;
            } else {
                position_ = collider_.Resolve(position_, delta).resolvedPosition;
            }
        } else if (wasMoving_) {
            // 離したらいちばん近いタイルの中心へ寄せる
            if (tileMap_ !is null) {
                int col, row;
                if (tileMap_.WorldToTile(position_, col, row)) {
                    snapTarget_ = tileMap_.TileToWorld(col, row);
                    hasSnapTarget_ = true;
                }
            }
        }
        wasMoving_ = isMoving;
        inputVelocity_ = Vector2(0.0f, 0.0f);

        UpdateSnap(dt);

        if (hasBounds_) {
            position_.x = Max(mapBoundsMin_.x + kHatHalfW, Min(mapBoundsMax_.x - kHatHalfW, position_.x));
            position_.y = Max(mapBoundsMin_.y + kHatHalfH, Min(mapBoundsMax_.y - kHatHalfH, position_.y));
        }
    }

    private void UpdateSnap(float dt)
    {
        if (!hasSnapTarget_) {
            return;
        }
        const Vector2 target = snapTarget_;
        const float dx = target.x - position_.x;
        const float dy = target.y - position_.y;
        const float dist2 = dx * dx + dy * dy;
        if (dist2 < 0.5f * 0.5f) {
            position_ = target;
            hasSnapTarget_ = false;
            if (blockManager.HasCopiedBlocks() && !overlapFlash_.IsActive()) {
                blockManager.UpdateSpritePositions(position_);
            }
        } else {
            const float t = snapLerpSpeed_ * dt;
            const float alpha = t < 1.0f ? t : 1.0f;
            const Vector2 newPos(position_.x + dx * alpha, position_.y + dy * alpha);
            position_ = collider_.Resolve(position_, Vector2(newPos.x - position_.x, newPos.y - position_.y)).resolvedPosition;
        }
    }

    private void UpdateCopiedBlocks(float dt)
    {
        if (blockManager.HasCopiedBlocks() && !overlapFlash_.IsActive()) {
            blockManager.UpdateSpawnAnimation(dt);
            blockManager.UpdateSpritePositions(position_);
            ResolveUnbreakableBlockCollision();
        }
    }

    private void UpdateVisualAnimation(float dt)
    {
        bobAnimationTimer_ += dt;
        if (bobAnimationTimer_ >= 1.2f) {
            bobAnimationTimer_ = fmod(bobAnimationTimer_, 1.2f);
        }
        const float raw = dt * 14.0f;
        const float t = raw < 1.0f ? raw : 1.0f;
        currentFacingScaleX_ += (targetFacingScaleX_ - currentFacingScaleX_) * t;
        if (abs(currentFacingScaleX_ - targetFacingScaleX_) < 0.01f) {
            currentFacingScaleX_ = targetFacingScaleX_;
        }
    }

    // モードを切り替えたらタイルの中心へ寄せる
    void OnModeChange()
    {
        SnapToTileGrid();
        ApplySpriteTransform();
        blockManager.UpdateSpritePositions(position_);
    }

    // 自機のタイルを中心に 5×5 を写し取る（自機は動かさない）
    void CopyBlocksFrom(const Vector2 &in playerWorldPos)
    {
        isActionMode_ = true;
        Vector2 snappedPlayerPos = playerWorldPos;
        if (tileMap_ !is null) {
            int col, row;
            if (tileMap_.WorldToTile(playerWorldPos, col, row)) {
                snappedPlayerPos = tileMap_.TileToWorld(col, row);
            }
        }
        blockManager.CopyBlocksFrom(snappedPlayerPos, position_);
        ResolveUnbreakableBlockCollision();
    }

    void CommitPlacement()
    {
        blockManager.CommitPlacement();
        isActionMode_ = false;
        isStuck_ = false;
    }

    void ResetAfterUndo(const array<HatUndoCommittedCell@> &in undoCommittedCells)
    {
        blockManager.ResetAfterUndo(undoCommittedCells);
        isActionMode_ = false;
        isStuck_ = false;
        hasSnapTarget_ = false;
    }

    // ハットの 5×5 が自機の 5×5 と重なっていれば置けない
    bool CanCommit() const
    {
        if (tileMap_ !is null && hasPlayerWorldPos_) {
            int hcol, hrow, pcol, prow;
            if (tileMap_.WorldToTile(position_, hcol, hrow) && tileMap_.WorldToTile(playerWorldPos_, pcol, prow)) {
                const int overlapThreshold = kCopyRadius * 2;
                if (Td3Abs(hcol - pcol) <= overlapThreshold && Td3Abs(hrow - prow) <= overlapThreshold) {
                    return false;
                }
            }
        }
        return blockManager.CanCommit();
    }

    void StartOverlapFlash()
    {
        if (!hasPlayerWorldPos_) {
            return;
        }
        SnapToTileGrid();
        hasSnapTarget_ = false;
        blockManager.UpdateSpritePositions(position_);
        overlapFlash_.Start(position_, playerWorldPos_, sprite_, blockManager.GetCopiedSprites(),
            blockManager.GetBlockWorldPositions());
    }

    private void ApplySpriteTransform()
    {
        if (sprite_ is null) {
            return;
        }
        // コマを横に並べた画像なので、幅をコマの幅に直す
        const float texW = sprite_.GetTextureSize().x;
        const float uvScaleX = (texW > 0.0f) ? 64.0f / texW : 1.0f;
        const float bobPhase = (bobAnimationTimer_ / 1.2f) * (kPi * 2.0f);
        const float bobOffsetY = sin(bobPhase) * 4.0f;
        sprite_.SetScale(abs(currentFacingScaleX_) * uvScaleX, 1.0f);
        sprite_.SetTranslate(position_.x, position_.y + bobOffsetY);
        sprite_.SetFlipX(currentFacingScaleX_ > 0.0f);
    }

    private void SnapToTileGrid()
    {
        if (tileMap_ is null) {
            return;
        }
        int col, row;
        if (!tileMap_.WorldToTile(position_, col, row)) {
            return;
        }
        position_ = tileMap_.TileToWorld(col, row);
    }

    // 写した壊せないブロックが、ステージの壊せないブロックと重なったら押し戻す（どちらにも動けなければ詰まり）
    private void ResolveUnbreakableBlockCollision()
    {
        isStuck_ = false;
        if (tileMap_ is null) {
            return;
        }
        const array<Vector2>@ copiedPositions = blockManager.GetBlockWorldPositions();
        if (copiedPositions.length() == 0) {
            return;
        }

        bool hasPlayerTile = false;
        int ptc = 0;
        int ptr = 0;
        if (hasPlayerWorldPos_) {
            hasPlayerTile = tileMap_.WorldToTile(playerWorldPos_, ptc, ptr);
        }
        const Vector2 offset(position_.x - lastSafePosition_.x, position_.y - lastSafePosition_.y);

        if (HasCollisionAt(position_, copiedPositions, offset, hasPlayerTile, ptc, ptr)) {
            const Vector2 posXOnly(position_.x, lastSafePosition_.y);
            const Vector2 posYOnly(lastSafePosition_.x, position_.y);
            if (!HasCollisionAt(posYOnly, copiedPositions, offset, hasPlayerTile, ptc, ptr)) {
                position_ = posYOnly;
            } else if (!HasCollisionAt(posXOnly, copiedPositions, offset, hasPlayerTile, ptc, ptr)) {
                position_ = posXOnly;
            } else {
                isStuck_ = true;
                ++stuckConsecutiveFrames_;
                position_ = lastSafePosition_;
            }
            hasSnapTarget_ = false;
            blockManager.UpdateSpritePositions(position_);
            ApplySpriteTransform();
        }
        if (!isStuck_) {
            stuckConsecutiveFrames_ = 0;
        }
    }

    private bool HasCollisionAt(const Vector2 &in testPos, const array<Vector2>@ copiedPositions, const Vector2 &in offset,
        bool hasPlayerTile, int ptc, int ptr) const
    {
        const float kHalf = kTileSize * 0.5f;
        const int cols = tileMap_.cols;
        const int rows = tileMap_.rows;
        const Vector2 testOffset(testPos.x - lastSafePosition_.x, testPos.y - lastSafePosition_.y);
        for (uint i = 0; i < copiedPositions.length(); ++i) {
            const Vector2 copiedPosAtCurrent = copiedPositions[i];
            const Vector2 copiedPos(copiedPosAtCurrent.x - offset.x + testOffset.x,
                copiedPosAtCurrent.y - offset.y + testOffset.y);
            const float cLeft = copiedPos.x - kHalf;
            const float cRight = copiedPos.x + kHalf;
            const float cTop = copiedPos.y + kHalf;
            const float cBot = copiedPos.y - kHalf;
            int cx, cy;
            if (!tileMap_.WorldToTile(copiedPos, cx, cy)) {
                continue;
            }
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const int col = cx + dx;
                    const int row = cy + dy;
                    if (col < 0 || col >= cols || row < 0 || row >= rows) {
                        continue;
                    }
                    if (tileMap_.GetTile(col, row) != kTileUnbreakable) {
                        continue;
                    }
                    if (hasPlayerTile && Td3Abs(col - ptc) <= kCopyRadius && Td3Abs(row - ptr) <= kCopyRadius) {
                        continue;
                    }
                    const Vector2 stagePos = tileMap_.TileToWorld(col, row);
                    const float sLeft = stagePos.x - kHalf;
                    const float sRight = stagePos.x + kHalf;
                    const float sTop = stagePos.y + kHalf;
                    const float sBot = stagePos.y - kHalf;
                    if (cRight > sLeft && cLeft < sRight && cTop > sBot && cBot < sTop) {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    // 元の SpriteObject::Update（アニメの 2 回目の更新）
    void ObjectUpdate(float dt)
    {
        if (sprite_ !is null) {
            sprite_.ObjectUpdate(dt);
        }
    }
}
