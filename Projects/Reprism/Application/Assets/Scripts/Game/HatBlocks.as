using namespace CoreEngine;

// ハットが持ち運ぶブロックの下まわり（元の HatSpritePool・HatSpawnAnimator・HatTileStateManager）

const int kCopyRadius = 2;
const string kCopySpriteTex = "white1x1.png";

// タイルごとのステージのスプライト（元の stageSpriteMap。キーは row × cols + col）
class SpriteGrid
{
    int cols = 0;
    int rows = 0;
    array<Td3Sprite@> sprites;

    void Initialize(int newCols, int newRows)
    {
        cols = newCols;
        rows = newRows;
        sprites.resize(uint(cols * rows));
    }

    int Key(int col, int row) const { return row * cols + col; }

    Td3Sprite@ Find(int key) const
    {
        if (key < 0 || key >= int(sprites.length())) {
            return null;
        }
        return sprites[uint(key)];
    }

    Td3Sprite@ Find(int col, int row) const
    {
        if (col < 0 || col >= cols || row < 0 || row >= rows) {
            return null;
        }
        return sprites[uint(row * cols + col)];
    }

    void Set(int key, Td3Sprite@ sprite)
    {
        if (key < 0 || key >= int(sprites.length())) {
            return;
        }
        @sprites[uint(key)] = sprite;
    }

    void Erase(int key)
    {
        Set(key, null);
    }
}

// 白い板の使い回し
class HatSpritePool
{
    Td3SpriteFactory@ factory;
    private array<Td3Sprite@> pool_;

    Td3Sprite@ Acquire()
    {
        if (pool_.length() > 0) {
            Td3Sprite@ sprite = pool_[pool_.length() - 1];
            pool_.removeLast();
            sprite.SetActive(true);
            return sprite;
        }
        return factory.Create(kCopySpriteTex, "Hat_Block");
    }

    void Release(Td3Sprite@ sprite)
    {
        if (sprite is null) {
            return;
        }
        sprite.SetActive(false);
        pool_.insertLast(sprite);
    }
}

// 写したブロックの出現（大きさ 0 → 目標を EaseOutBack で、1 個ずつ少し遅らせる）
class HatSpawnState
{
    Td3Timer timer;
    float targetScale = 1.0f;
}

class HatSpawnAnimator
{
    private array<HatSpawnState@> states_;

    void Register(Td3Sprite@ sprite, uint index, float targetScale)
    {
        if (sprite !is null) {
            sprite.SetScale(0.0f, 0.0f);
        }
        const float delay = float(index) * 0.03f;
        HatSpawnState state;
        state.targetScale = targetScale;
        state.timer.Start(0.25f + delay);
        states_.insertLast(state);
    }

    void Update(float dt, const array<Td3Sprite@> &in sprites)
    {
        for (uint i = 0; i < states_.length(); ++i) {
            states_[i].timer.Update(dt);
            if (i >= sprites.length() || sprites[i] is null) {
                continue;
            }
            const float targetScale = states_[i].targetScale;
            if (states_[i].timer.IsFinished()) {
                sprites[i].SetScale(targetScale, targetScale);
                continue;
            }
            const float elapsed = states_[i].timer.GetElapsedTime();
            const float delay = float(i) * 0.03f;
            const float animTime = elapsed - delay;
            if (animTime <= 0.0f) {
                sprites[i].SetScale(0.0f, 0.0f);
            } else {
                const float t = (animTime < 0.25f) ? (animTime / 0.25f) : 1.0f;
                const float scale = Td3EaseApply(t, Td3Ease::EaseOutBack);
                sprites[i].SetScale(scale * targetScale, scale * targetScale);
            }
        }
    }

    bool IsFinished() const
    {
        for (uint i = 0; i < states_.length(); ++i) {
            if (!states_[i].timer.IsFinished()) {
                return false;
            }
        }
        return true;
    }

    void ForceComplete(const array<Td3Sprite@> &in sprites)
    {
        for (uint i = 0; i < sprites.length() && i < states_.length(); ++i) {
            if (sprites[i] !is null) {
                sprites[i].SetScale(states_[i].targetScale, states_[i].targetScale);
            }
        }
        states_.resize(0);
    }

    void Clear()
    {
        states_.resize(0);
    }
}

class HatClearedTile
{
    int col;
    int row;
    int originalId;
}

class HatSuppressedTile
{
    int col;
    int row;
    int originalId;
    Td3Sprite@ sprite;
}

class HatCommittedCell
{
    int col;
    int row;
    int originalId;
    Td3Sprite@ originalSprite;
    Td3Sprite@ hatSprite;
    string hatSpriteTex;
}

// Undo に覚える置いたセル
class HatUndoCommittedCell
{
    int col = 0;
    int row = 0;
    int originalId = 0;
    int currentId = 0;
    bool hasHatSprite = false;
    string hatSpriteTex;
}

HatCommittedCell@ MakeCommittedCell(int col, int row, int originalId, Td3Sprite@ originalSprite,
    Td3Sprite@ hatSprite, const string &in hatSpriteTex)
{
    HatCommittedCell cell;
    cell.col = col;
    cell.row = row;
    cell.originalId = originalId;
    @cell.originalSprite = originalSprite;
    @cell.hatSprite = hatSprite;
    cell.hatSpriteTex = hatSpriteTex;
    return cell;
}

// タイルの一時的な消去・置いた物・自機の範囲で抑えたタイルの記録と戻し
class HatTileStateManager
{
    TileMap@ tileMap;
    SpriteGrid@ stageSpriteMap;
    array<HatClearedTile@> clearedTiles;
    array<HatSuppressedTile@> playerAreaSuppressed;
    array<HatCommittedCell@> committedCells;

    void ClearTile(int col, int row, int originalId)
    {
        HatClearedTile tile;
        tile.col = col;
        tile.row = row;
        tile.originalId = originalId;
        clearedTiles.insertLast(tile);
        if (tileMap !is null) {
            tileMap.SetTile(col, row, 0);
        }
        if (stageSpriteMap !is null) {
            Td3Sprite@ sprite = stageSpriteMap.Find(col, row);
            if (sprite !is null) {
                sprite.SetActive(false);
            }
        }
    }

    void RestoreCleared()
    {
        if (tileMap is null) {
            return;
        }
        for (uint i = 0; i < clearedTiles.length(); ++i) {
            HatClearedTile@ t = clearedTiles[i];
            tileMap.SetTile(t.col, t.row, t.originalId);
            if (stageSpriteMap !is null) {
                Td3Sprite@ sprite = stageSpriteMap.Find(t.col, t.row);
                if (sprite !is null) {
                    sprite.SetActive(true);
                }
            }
        }
        clearedTiles.resize(0);
    }

    void ForgetCleared() { clearedTiles.resize(0); }
    void ForgetCommitted() { committedCells.resize(0); }

    void SuppressPlayerAreaTile(int col, int row, int originalId, Td3Sprite@ sprite)
    {
        HatSuppressedTile tile;
        tile.col = col;
        tile.row = row;
        tile.originalId = originalId;
        @tile.sprite = sprite;
        playerAreaSuppressed.insertLast(tile);
        if (tileMap !is null) {
            tileMap.SetTile(col, row, 0);
        }
        if (sprite !is null) {
            sprite.SetActive(false);
        }
    }

    void RestorePlayerAreaSuppressed()
    {
        if (tileMap is null) {
            playerAreaSuppressed.resize(0);
            return;
        }
        for (uint i = 0; i < playerAreaSuppressed.length(); ++i) {
            HatSuppressedTile@ s = playerAreaSuppressed[i];
            tileMap.SetTile(s.col, s.row, s.originalId);
            if (s.sprite !is null) {
                s.sprite.SetActive(s.originalId != 0);
            }
        }
        playerAreaSuppressed.resize(0);
    }

    void ClearPlayerAreaSuppressed() { playerAreaSuppressed.resize(0); }

    void AddCommittedCell(HatCommittedCell@ cell)
    {
        committedCells.insertLast(cell);
    }

    bool HasCommittedCell(int col, int row) const
    {
        for (uint i = 0; i < committedCells.length(); ++i) {
            if (committedCells[i].col == col && committedCells[i].row == row) {
                return true;
            }
        }
        return false;
    }

    // preservedKeys に入っているセルを残して、置いた物を元へ戻す
    void ClearCommittedExcept(const array<int> &in preservedKeys, HatSpritePool@ pool)
    {
        if (tileMap is null) {
            committedCells.resize(0);
            return;
        }
        array<HatCommittedCell@> preservedCells;
        for (uint i = 0; i < committedCells.length(); ++i) {
            HatCommittedCell@ c = committedCells[i];
            const int key = stageSpriteMap.Key(c.col, c.row);
            if (preservedKeys.find(key) >= 0) {
                if (c.hatSprite !is null) {
                    c.hatSprite.SetActive(true);
                }
                if (c.hatSprite !is null) {
                    stageSpriteMap.Set(key, c.hatSprite);
                } else if (c.originalSprite !is null) {
                    stageSpriteMap.Set(key, c.originalSprite);
                    c.originalSprite.SetActive(tileMap.GetTile(c.col, c.row) != 0);
                }
                preservedCells.insertLast(c);
                continue;
            }
            if (c.hatSprite !is null) {
                pool.Release(c.hatSprite);
            }
            tileMap.SetTile(c.col, c.row, c.originalId);
            if (c.originalSprite !is null) {
                stageSpriteMap.Set(key, c.originalSprite);
                c.originalSprite.SetActive(c.originalId != 0);
            } else if (stageSpriteMap.Find(key) is c.hatSprite) {
                stageSpriteMap.Erase(key);
            }
        }
        committedCells = preservedCells;
    }
}
