using namespace CoreEngine;

// ハットが運ぶブロックの写し取り・持ち運び・置く・Undo（元の HatBlockManager）
class HatBlockManager
{
    private TileMap@ tileMap_;
    private SpriteGrid@ stageSpriteMap_;
    private bool hasPlayerWorldPos_ = false;
    private Vector2 playerWorldPos_;
    private bool hasLastCopiedHatPos_ = false;
    private Vector2 lastCopiedHatPos_;
    private array<int> prevCommittedKeys_;

    HatSpritePool spritePool;
    private HatSpawnAnimator spawnAnimator_;
    HatTileStateManager tileState;

    private array<Td3Sprite@> copiedSprites_;
    private array<string> copiedSpriteTextures_;
    private array<Vector2> copiedOffsets_;
    private array<Vector2> blockWorldPositions_;
    private array<int> copiedTileIds_;
    private array<Vector2> unbreakableBlockPositions_;

    void Initialize(TileMap@ tileMap, Td3SpriteFactory@ factory)
    {
        @tileMap_ = tileMap;
        @spritePool.factory = factory;
        @tileState.tileMap = tileMap;
    }

    void SetStageSpriteMap(SpriteGrid@ map)
    {
        @stageSpriteMap_ = map;
        @tileState.stageSpriteMap = map;
    }

    void SetPlayerWorldPos(bool hasPosition, const Vector2 &in position)
    {
        hasPlayerWorldPos_ = hasPosition;
        playerWorldPos_ = position;
    }

    const array<Vector2>@ GetBlockWorldPositions() const { return blockWorldPositions_; }
    const array<Td3Sprite@>@ GetCopiedSprites() const { return copiedSprites_; }
    const array<Vector2>@ GetUnbreakableBlockPositions() const { return unbreakableBlockPositions_; }
    bool HasCopiedBlocks() const { return copiedSprites_.length() > 0; }
    bool IsSpawnAnimationFinished() const { return spawnAnimator_.IsFinished(); }

    private int Key(int col, int row) const { return row * tileMap_.cols + col; }

    private bool IsInBounds(int col, int row) const
    {
        return col >= 0 && col < tileMap_.cols && row >= 0 && row < tileMap_.rows;
    }

    private bool IsSolidCopySource(int id) const
    {
        return id == kTileBlock || id == kTileUnbreakable || id == kTileWall;
    }

    private bool IsUnbreakable(int id) const
    {
        return id == kTileUnbreakable || id == kTileWall;
    }

    private bool IsInTileRange(int col, int row, int centerCol, int centerRow, int radius) const
    {
        return Td3Abs(col - centerCol) <= radius && Td3Abs(row - centerRow) <= radius;
    }

    array<HatUndoCommittedCell@>@ CaptureUndoCommittedCells() const
    {
        array<HatUndoCommittedCell@> cells;
        for (uint i = 0; i < tileState.committedCells.length(); ++i) {
            HatCommittedCell@ cc = tileState.committedCells[i];
            HatUndoCommittedCell undoCell;
            undoCell.col = cc.col;
            undoCell.row = cc.row;
            undoCell.originalId = cc.originalId;
            undoCell.currentId = tileMap_ !is null ? tileMap_.GetTile(cc.col, cc.row) : 0;
            undoCell.hasHatSprite = cc.hatSprite !is null;
            undoCell.hatSpriteTex = cc.hatSpriteTex;
            cells.insertLast(undoCell);
        }
        return cells;
    }

    // 自機のまわり 5×5 を写し取り、ハットのまわりに白い板を並べる
    void CopyBlocksFrom(const Vector2 &in playerWorldPos, const Vector2 &in hatPos)
    {
        if (tileMap_ is null) {
            return;
        }
        int cx, cy;
        if (!tileMap_.WorldToTile(playerWorldPos, cx, cy)) {
            return;
        }

        // 置いた物を戻すより前にタイルを読む
        array<int> entryDx;
        array<int> entryDy;
        array<int> entryId;
        for (int dy = -kCopyRadius; dy <= kCopyRadius; ++dy) {
            for (int dx = -kCopyRadius; dx <= kCopyRadius; ++dx) {
                const int srcCol = cx + dx;
                const int srcRow = cy + dy;
                if (!IsInBounds(srcCol, srcRow)) {
                    continue;
                }
                const int id = tileMap_.GetTile(srcCol, srcRow);
                if (IsSolidCopySource(id)) {
                    entryDx.insertLast(dx);
                    entryDy.insertLast(dy);
                    entryId.insertLast(id);
                }
            }
        }

        // 前に置いた物のうち、今回写す範囲に入るセルは残す
        array<int> copiedCommittedKeys;
        for (uint i = 0; i < tileState.committedCells.length(); ++i) {
            HatCommittedCell@ cc = tileState.committedCells[i];
            if (IsInTileRange(cc.col, cc.row, cx, cy, kCopyRadius)) {
                copiedCommittedKeys.insertLast(Key(cc.col, cc.row));
            }
        }
        array<int> copiedSourceKeys;
        for (uint i = 0; i < entryDx.length(); ++i) {
            copiedSourceKeys.insertLast(Key(cx + entryDx[i], cy + entryDy[i]));
        }
        prevCommittedKeys_.resize(0);
        for (uint i = 0; i < tileState.committedCells.length(); ++i) {
            HatCommittedCell@ cc = tileState.committedCells[i];
            const int key = Key(cc.col, cc.row);
            if (copiedCommittedKeys.find(key) < 0) {
                prevCommittedKeys_.insertLast(key);
            }
        }

        tileState.RestorePlayerAreaSuppressed();
        tileState.ClearCommittedExcept(copiedCommittedKeys, spritePool);
        // 前の出現が途中でも大きさを確定してから片付ける
        spawnAnimator_.ForceComplete(copiedSprites_);
        ClearCopied();

        // 自機の 5×5 の中で戻ったブロックをもう一度抑える
        for (int dy = -kCopyRadius; dy <= kCopyRadius; ++dy) {
            for (int dx = -kCopyRadius; dx <= kCopyRadius; ++dx) {
                const int col = cx + dx;
                const int row = cy + dy;
                if (!IsInBounds(col, row)) {
                    continue;
                }
                const int currentId = tileMap_.GetTile(col, row);
                if (currentId == 0 || IsUnbreakable(currentId)) {
                    continue;
                }
                if (copiedSourceKeys.find(Key(col, row)) >= 0) {
                    continue;
                }
                tileState.SuppressPlayerAreaTile(col, row, currentId, stageSpriteMap_.Find(col, row));
            }
        }

        hasLastCopiedHatPos_ = true;
        lastCopiedHatPos_ = hatPos;

        for (uint i = 0; i < entryDx.length(); ++i) {
            const int id = entryId[i];
            if (!IsSolidCopySource(id)) {
                continue;
            }
            const Vector2 offset(float(entryDx[i]) * kTileSize, float(-entryDy[i]) * kTileSize);
            Td3Sprite@ sprite = spritePool.Acquire();
            if (sprite is null) {
                continue;
            }
            const Vector2 worldPos(hatPos.x + offset.x, hatPos.y + offset.y);
            sprite.SetAnchor(Vector2(0.5f, 0.5f));
            sprite.SetSortingLayer(1);
            sprite.SetColor(Vector4(1.0f, 1.0f, 1.0f, 1.0f));
            spawnAnimator_.Register(sprite, copiedSprites_.length(), kTileSize);
            sprite.SetTranslate(worldPos);

            copiedSprites_.insertLast(sprite);
            copiedSpriteTextures_.insertLast(kCopySpriteTex);
            copiedOffsets_.insertLast(offset);
            blockWorldPositions_.insertLast(worldPos);
            copiedTileIds_.insertLast(id);
            if (id == kTileUnbreakable || id == kTileWall) {
                unbreakableBlockPositions_.insertLast(worldPos);
            }
        }

        UpdateClearedArea(hatPos);
    }

    // ハットの 5×5 の中の壊せるブロックを 0 にして元の番号を覚える（毎フレーム）
    void UpdateClearedArea(const Vector2 &in hatPos)
    {
        if (tileMap_ is null) {
            return;
        }
        tileState.RestoreCleared();

        int hx, hy;
        if (!tileMap_.WorldToTile(hatPos, hx, hy)) {
            return;
        }

        // 写したブロックが重なっているタイル
        array<int> hatCoveredKeys;
        for (uint i = 0; i < blockWorldPositions_.length(); ++i) {
            int c, r;
            if (tileMap_.WorldToTile(blockWorldPositions_[i], c, r)) {
                hatCoveredKeys.insertLast(Key(c, r));
            }
        }

        bool hasPlayerTile = false;
        int pcx = 0;
        int pcy = 0;
        if (hasPlayerWorldPos_) {
            hasPlayerTile = tileMap_.WorldToTile(playerWorldPos_, pcx, pcy);
        }

        for (int dy = -kCopyRadius; dy <= kCopyRadius; ++dy) {
            for (int dx = -kCopyRadius; dx <= kCopyRadius; ++dx) {
                const int col = hx + dx;
                const int row = hy + dy;
                if (!IsInBounds(col, row)) {
                    continue;
                }
                // ハットが px 単位で覆いきるまでブロックを残す
                const Vector2 tileCenter = tileMap_.TileToWorld(col, row);
                const float distX = abs(tileCenter.x - hatPos.x);
                const float distY = abs(tileCenter.y - hatPos.y);
                const float threshold = float(kCopyRadius) * kTileSize;
                if (distX > threshold || distY > threshold) {
                    continue;
                }
                const int key = Key(col, row);
                // 自機の 5×5 は守る（前に置いたセルは除く）
                if (hasPlayerTile && prevCommittedKeys_.find(key) < 0) {
                    if (IsInTileRange(col, row, pcx, pcy, kCopyRadius)) {
                        continue;
                    }
                }
                const int id = tileMap_.GetTile(col, row);
                if (id == 0) {
                    continue;
                }
                if (IsUnbreakable(id) && hatCoveredKeys.find(key) < 0) {
                    continue;
                }
                tileState.ClearTile(col, row, id);
            }
        }
    }

    // 写したブロックをハットに付いて動かす
    void UpdateSpritePositions(const Vector2 &in hatPos)
    {
        hasLastCopiedHatPos_ = true;
        lastCopiedHatPos_ = hatPos;
        uint unbreakIdx = 0;
        for (uint i = 0; i < copiedSprites_.length(); ++i) {
            const Vector2 worldPos(hatPos.x + copiedOffsets_[i].x, hatPos.y + copiedOffsets_[i].y);
            blockWorldPositions_[i] = worldPos;
            if (copiedSprites_[i] !is null) {
                copiedSprites_[i].SetTranslate(worldPos);
            }
            if (copiedTileIds_[i] == kTileUnbreakable || copiedTileIds_[i] == kTileWall) {
                if (unbreakIdx < unbreakableBlockPositions_.length()) {
                    unbreakableBlockPositions_[unbreakIdx++] = worldPos;
                }
            }
        }
    }

    // 写した内容をタイルに書いて確定する
    void CommitPlacement()
    {
        if (tileMap_ is null) {
            return;
        }

        // (1) 一時的に消したタイルを、置いた物の記録へ移す（タイルは 0 のまま）
        array<int> cellKeys;
        array<uint> cellIndices;
        for (uint i = 0; i < tileState.committedCells.length(); ++i) {
            HatCommittedCell@ cc = tileState.committedCells[i];
            cellKeys.insertLast(Key(cc.col, cc.row));
            cellIndices.insertLast(i);
        }
        for (uint i = 0; i < tileState.clearedTiles.length(); ++i) {
            HatClearedTile@ t = tileState.clearedTiles[i];
            const int key = Key(t.col, t.row);
            if (cellKeys.find(key) >= 0) {
                continue;
            }
            Td3Sprite@ originalSprite = stageSpriteMap_ !is null ? stageSpriteMap_.Find(key) : null;
            cellKeys.insertLast(key);
            cellIndices.insertLast(tileState.committedCells.length());
            tileState.AddCommittedCell(MakeCommittedCell(t.col, t.row, t.originalId, originalSprite, null, ""));
        }
        tileState.ForgetCleared();

        // (2) 写したブロックをタイルに載せる
        for (uint i = 0; i < blockWorldPositions_.length(); ++i) {
            int col, row;
            if (!tileMap_.WorldToTile(blockWorldPositions_[i], col, row)) {
                continue;
            }
            const int key = Key(col, row);
            Td3Sprite@ hatSprite = copiedSprites_[i];
            const string hatTex = copiedSpriteTextures_[i];

            const int found = cellKeys.find(key);
            if (found < 0) {
                // 記録に無いセル（自機の範囲など）は、元の番号と元のスプライトを覚えておく
                const int currentId = tileMap_.GetTile(col, row);
                Td3Sprite@ originalSprite = stageSpriteMap_.Find(key);
                if (originalSprite is hatSprite) {
                    @originalSprite = null;
                }
                cellKeys.insertLast(key);
                cellIndices.insertLast(tileState.committedCells.length());
                tileState.AddCommittedCell(MakeCommittedCell(col, row, currentId, originalSprite, hatSprite, hatTex));
            } else {
                HatCommittedCell@ cell = tileState.committedCells[cellIndices[uint(found)]];
                if (cell.hatSprite !is null && cell.hatSprite !is hatSprite) {
                    spritePool.Release(cell.hatSprite);
                }
                @cell.hatSprite = hatSprite;
                cell.hatSpriteTex = hatTex;
            }

            tileMap_.SetTile(col, row, copiedTileIds_[i]);

            if (hatSprite !is null) {
                hatSprite.SetTranslate(tileMap_.TileToWorld(col, row));
                hatSprite.SetColor(Vector4(1.0f, 1.0f, 1.0f, 1.0f));
                hatSprite.SetActive(true);
                if (stageSpriteMap_ !is null) {
                    Td3Sprite@ existing = stageSpriteMap_.Find(key);
                    if (existing !is null && existing !is hatSprite) {
                        existing.SetActive(false);
                    }
                    stageSpriteMap_.Set(key, hatSprite);
                }
            }
        }

        copiedSprites_.resize(0);
        copiedSpriteTextures_.resize(0);
        copiedOffsets_.resize(0);
        blockWorldPositions_.resize(0);
        copiedTileIds_.resize(0);
        unbreakableBlockPositions_.resize(0);
        spawnAnimator_.Clear();

        // ハットの 5×5 でブロックが載らなかった壊せるブロックを隠す
        if (hasLastCopiedHatPos_ && stageSpriteMap_ !is null) {
            int hx, hy;
            if (tileMap_.WorldToTile(lastCopiedHatPos_, hx, hy)) {
                for (int dy = -kCopyRadius; dy <= kCopyRadius; ++dy) {
                    for (int dx = -kCopyRadius; dx <= kCopyRadius; ++dx) {
                        const int col = hx + dx;
                        const int row = hy + dy;
                        if (!IsInBounds(col, row)) {
                            continue;
                        }
                        if (tileState.HasCommittedCell(col, row)) {
                            continue;
                        }
                        const int id = tileMap_.GetTile(col, row);
                        if (id == 0 || IsUnbreakable(id)) {
                            continue;
                        }
                        Td3Sprite@ sprite = stageSpriteMap_.Find(col, row);
                        tileState.AddCommittedCell(MakeCommittedCell(col, row, id, sprite, null, ""));
                        tileMap_.SetTile(col, row, 0);
                        if (sprite !is null) {
                            sprite.SetActive(false);
                        }
                    }
                }
            }
        }

        tileState.RestorePlayerAreaSuppressed();
    }

    // 置く先が自機の 5×5 と重なっていなければ true
    bool CanCommit() const
    {
        if (tileMap_ is null || !hasPlayerWorldPos_) {
            return true;
        }
        int pcx, pcy;
        if (!tileMap_.WorldToTile(playerWorldPos_, pcx, pcy)) {
            return true;
        }
        for (uint i = 0; i < blockWorldPositions_.length(); ++i) {
            int c, r;
            if (!tileMap_.WorldToTile(blockWorldPositions_[i], c, r)) {
                continue;
            }
            if (IsInTileRange(c, r, pcx, pcy, kCopyRadius)) {
                return false;
            }
        }
        return true;
    }

    private void ClearCopied()
    {
        for (uint i = 0; i < copiedSprites_.length(); ++i) {
            spritePool.Release(copiedSprites_[i]);
        }
        copiedSprites_.resize(0);
        copiedSpriteTextures_.resize(0);
        copiedOffsets_.resize(0);
        blockWorldPositions_.resize(0);
        copiedTileIds_.resize(0);
        unbreakableBlockPositions_.resize(0);
        spawnAnimator_.Clear();
        tileState.RestorePlayerAreaSuppressed();
    }

    // Undo の後に、置いた物の表示を記録から組み直す（タイルは呼ぶ側が戻してある）
    void ResetAfterUndo(const array<HatUndoCommittedCell@> &in undoCommittedCells)
    {
        // (1) 持ち運び中の板を返す
        spawnAnimator_.ForceComplete(copiedSprites_);
        for (uint i = 0; i < copiedSprites_.length(); ++i) {
            spritePool.Release(copiedSprites_[i]);
        }
        copiedSprites_.resize(0);
        copiedSpriteTextures_.resize(0);
        copiedOffsets_.resize(0);
        blockWorldPositions_.resize(0);
        copiedTileIds_.resize(0);
        unbreakableBlockPositions_.resize(0);
        spawnAnimator_.Clear();

        // (2) 置いた物の板を返し、ステージのスプライトを元へ戻す
        if (stageSpriteMap_ !is null) {
            for (uint i = 0; i < tileState.committedCells.length(); ++i) {
                HatCommittedCell@ cc = tileState.committedCells[i];
                const int key = Key(cc.col, cc.row);
                if (cc.hatSprite !is null) {
                    spritePool.Release(cc.hatSprite);
                }
                if (cc.originalSprite !is null) {
                    stageSpriteMap_.Set(key, cc.originalSprite);
                    cc.originalSprite.SetActive(true);
                } else if (stageSpriteMap_.Find(key) is cc.hatSprite) {
                    stageSpriteMap_.Erase(key);
                }
            }
        } else {
            for (uint i = 0; i < tileState.committedCells.length(); ++i) {
                if (tileState.committedCells[i].hatSprite !is null) {
                    spritePool.Release(tileState.committedCells[i].hatSprite);
                }
            }
        }
        tileState.ClearPlayerAreaSuppressed();
        tileState.ForgetCleared();
        tileState.ForgetCommitted();
        prevCommittedKeys_.resize(0);
        hasLastCopiedHatPos_ = false;

        // (3) 記録から置いた物を組み直す
        for (uint i = 0; i < undoCommittedCells.length(); ++i) {
            HatUndoCommittedCell@ undoCell = undoCommittedCells[i];
            Td3Sprite@ originalSprite = stageSpriteMap_ !is null ? stageSpriteMap_.Find(undoCell.col, undoCell.row) : null;
            Td3Sprite@ hatSprite = null;
            if (undoCell.hasHatSprite) {
                @hatSprite = spritePool.Acquire();
                if (hatSprite !is null) {
                    hatSprite.SetAnchor(Vector2(0.5f, 0.5f));
                    hatSprite.SetSortingLayer(1);
                    hatSprite.SetColor(Vector4(1.0f, 1.0f, 1.0f, 1.0f));
                    hatSprite.SetActive(true);
                    hatSprite.SetScale(kTileSize, kTileSize);
                    hatSprite.SetTranslate(tileMap_ !is null ? tileMap_.TileToWorld(undoCell.col, undoCell.row) : Vector2(0.0f, 0.0f));
                    if (originalSprite !is null && originalSprite !is hatSprite) {
                        originalSprite.SetActive(false);
                    }
                    if (stageSpriteMap_ !is null) {
                        stageSpriteMap_.Set(Key(undoCell.col, undoCell.row), hatSprite);
                    }
                }
            } else if (originalSprite !is null) {
                originalSprite.SetActive(undoCell.currentId != 0);
            }
            tileState.AddCommittedCell(MakeCommittedCell(undoCell.col, undoCell.row, undoCell.originalId,
                originalSprite is hatSprite ? null : originalSprite, hatSprite,
                undoCell.hasHatSprite ? undoCell.hatSpriteTex : ""));
        }
    }

    void UpdateSpawnAnimation(float dt)
    {
        spawnAnimator_.Update(dt, copiedSprites_);
    }
}
