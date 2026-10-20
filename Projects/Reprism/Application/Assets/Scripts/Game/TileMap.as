using namespace CoreEngine;

// タイルの番号（ステージの JSON の値と同じ）
const int kTileBlank = 0;
const int kTileBlock = 1;
const int kTileUnbreakable = 2;
const int kTileWall = 3;
const int kTileGoal = 4;
const int kTilePlayer = 5;
const int kTileHat = 6;

const float kTileSize = 64.0f;

// 元の TileMap（1 レイヤー）。行 0 がマップの上端で、ワールドは中央が原点・上が +Y
class TileMap
{
    int cols = 0;
    int rows = 0;
    float tileSize = kTileSize;
    Vector2 origin;
    array<int> tiles;

    void Initialize(int newCols, int newRows, float newTileSize)
    {
        cols = newCols;
        rows = newRows;
        tileSize = newTileSize;
        const float mapW = float(cols) * tileSize;
        const float mapH = float(rows) * tileSize;
        origin = Vector2(-mapW * 0.5f, mapH * 0.5f);
        tiles.resize(uint(cols * rows));
        for (uint i = 0; i < tiles.length(); ++i) {
            tiles[i] = 0;
        }
    }

    bool IsInRange(int col, int row) const
    {
        return col >= 0 && col < cols && row >= 0 && row < rows;
    }

    int Key(int col, int row) const
    {
        return row * cols + col;
    }

    int GetTile(int col, int row) const
    {
        if (!IsInRange(col, row)) {
            return 0;
        }
        return tiles[uint(row * cols + col)];
    }

    void SetTile(int col, int row, int id)
    {
        if (!IsInRange(col, row)) {
            return;
        }
        tiles[uint(row * cols + col)] = id;
    }

    // 壊せるブロック・壊せないブロック・壁がぶつかる
    bool IsSolid(int col, int row) const
    {
        const int id = GetTile(col, row);
        return id == kTileBlock || id == kTileUnbreakable || id == kTileWall;
    }

    bool WorldToTile(const Vector2 &in worldPos, int &out col, int &out row) const
    {
        col = 0;
        row = 0;
        if (tileSize <= 0.0f) {
            return false;
        }
        const float localX = worldPos.x - origin.x;
        const float localY = origin.y - worldPos.y;
        col = int(floor(localX / tileSize));
        row = int(floor(localY / tileSize));
        return IsInRange(col, row);
    }

    // タイルの中心
    Vector2 TileToWorld(int col, int row) const
    {
        return Vector2(origin.x + (float(col) + 0.5f) * tileSize, origin.y - (float(row) + 0.5f) * tileSize);
    }

    Vector2 TileToWorldTopLeft(int col, int row) const
    {
        return Vector2(origin.x + float(col) * tileSize, origin.y - float(row) * tileSize);
    }
}

// 元の TileCollider::Resolve の結果
class TileCollisionResult
{
    Vector2 resolvedPosition;
    bool isOnGround = false;
    bool hitCeiling = false;
    bool hitWallLeft = false;
    bool hitWallRight = false;
}

// 元の TileCollider：AABB を X → Y の順に動かしてタイルから押し出す
class TileCollider
{
    TileMap@ tileMap;
    float halfW = 16.0f;
    float halfH = 16.0f;

    TileCollisionResult@ Resolve(const Vector2 &in oldPosition, const Vector2 &in velocity) const
    {
        TileCollisionResult result;
        result.resolvedPosition = oldPosition;
        if (tileMap is null) {
            return result;
        }
        const float ts = tileMap.tileSize;
        if (ts <= 0.0f) {
            return result;
        }

        // X だけ動かして横を解決する
        result.resolvedPosition.x = oldPosition.x + velocity.x;
        {
            int colMin, colMax, rowMin, rowMax;
            GetOverlappingTileRange(result.resolvedPosition, colMin, colMax, rowMin, rowMax);
            for (int row = rowMin; row <= rowMax; ++row) {
                for (int col = colMin; col <= colMax; ++col) {
                    if (!tileMap.IsSolid(col, row)) {
                        continue;
                    }
                    const Vector2 tileTopLeft = tileMap.TileToWorldTopLeft(col, row);
                    const float tileLeft = tileTopLeft.x;
                    const float tileRight = tileTopLeft.x + ts;
                    const float tileTop = tileTopLeft.y;
                    const float tileBottom = tileTopLeft.y - ts;
                    const float charLeft = result.resolvedPosition.x - halfW;
                    const float charRight = result.resolvedPosition.x + halfW;
                    const float charTop = result.resolvedPosition.y + halfH;
                    const float charBottom = result.resolvedPosition.y - halfH;
                    const float epsilon = 1.0f;
                    if (charTop <= tileBottom + epsilon || charBottom >= tileTop - epsilon) {
                        continue;
                    }
                    if (charRight <= tileLeft || charLeft >= tileRight) {
                        continue;
                    }
                    const float overlapRight = charRight - tileLeft;
                    const float overlapLeft = tileRight - charLeft;
                    if (overlapRight < overlapLeft) {
                        result.resolvedPosition.x -= overlapRight;
                        result.hitWallRight = true;
                    } else {
                        result.resolvedPosition.x += overlapLeft;
                        result.hitWallLeft = true;
                    }
                }
            }
        }

        // Y だけ動かして縦を解決する
        result.resolvedPosition.y = oldPosition.y + velocity.y;
        {
            int colMin, colMax, rowMin, rowMax;
            GetOverlappingTileRange(result.resolvedPosition, colMin, colMax, rowMin, rowMax);
            for (int row = rowMin; row <= rowMax; ++row) {
                for (int col = colMin; col <= colMax; ++col) {
                    if (!tileMap.IsSolid(col, row)) {
                        continue;
                    }
                    const Vector2 tileTopLeft = tileMap.TileToWorldTopLeft(col, row);
                    const float tileLeft = tileTopLeft.x;
                    const float tileRight = tileTopLeft.x + ts;
                    const float tileTop = tileTopLeft.y;
                    const float tileBottom = tileTopLeft.y - ts;
                    const float charLeft = result.resolvedPosition.x - halfW;
                    const float charRight = result.resolvedPosition.x + halfW;
                    const float charTop = result.resolvedPosition.y + halfH;
                    const float charBottom = result.resolvedPosition.y - halfH;
                    const float epsilon = 1.0f;
                    if (charRight <= tileLeft + epsilon || charLeft >= tileRight - epsilon) {
                        continue;
                    }
                    if (charTop <= tileBottom || charBottom >= tileTop) {
                        continue;
                    }
                    const float overlapUp = charTop - tileBottom;
                    const float overlapDown = tileTop - charBottom;
                    if (overlapDown < overlapUp) {
                        result.resolvedPosition.y += overlapDown;
                        result.isOnGround = true;
                    } else {
                        result.resolvedPosition.y -= overlapUp;
                        result.hitCeiling = true;
                    }
                }
            }
        }
        return result;
    }

    private void GetOverlappingTileRange(const Vector2 &in center, int &out colMin, int &out colMax,
        int &out rowMin, int &out rowMax) const
    {
        const float ts = tileMap.tileSize;
        const Vector2 origin = tileMap.origin;
        const float left = center.x - halfW;
        const float right = center.x + halfW;
        const float top = center.y + halfH;
        const float bottom = center.y - halfH;
        colMin = Max(int(floor((left - origin.x) / ts)), 0);
        colMax = Min(int(floor((right - origin.x) / ts)), tileMap.cols - 1);
        rowMin = Max(int(floor((origin.y - top) / ts)), 0);
        rowMax = Min(int(floor((origin.y - bottom) / ts)), tileMap.rows - 1);
    }
}

// 元の StageLoader：ステージの JSON（行優先の 1 本の配列）を読む。壁(3) は壊せないブロック(2) にそろえる
TileMap@ LoadStage(int stageNumber)
{
    DataFile data("Application/Assets/Data/Stages/stage" + stageNumber + ".json");
    TileMap tileMap;
    if (!data.exists) {
        tileMap.Initialize(20, 20, kTileSize);
        return tileMap;
    }
    const int cols = data.GetInt("cols", 20);
    const int rows = data.GetInt("rows", 20);
    tileMap.Initialize(cols, rows, kTileSize);
    int[]@ ids = data.GetIntArray("tiles");
    if (ids !is null) {
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                const uint index = uint(r * cols + c);
                if (index >= ids.length()) {
                    continue;
                }
                int id = ids[index];
                if (id == kTileWall) {
                    id = kTileUnbreakable;
                }
                tileMap.SetTile(c, r, id);
            }
        }
    }
    return tileMap;
}
