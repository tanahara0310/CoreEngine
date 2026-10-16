// リザルトの背景の配置
namespace ResultLayout
{
    const float kMonkeyY = 7.8f;
    const float kGroundTileScale = 0.625f;      // 1 マスの地面（ブロック 1.6 を 1.0 へ）
    const float kGroundTileY = 6.8f;            // kMonkeyY - 1.6 * kGroundTileScale
    const float kMonkeyMinCenterDistance = 1.9f;
    const float kMonkeyLayoutEdgePadding = 0.9f;
    const float kDecorationMinCenterDistance = 2.1f;
    const float kDecorationLayoutMargin = 3.0f;
    const int kRandomPlacementAttempts = 512;
    const float kGridCellSize = 2.1f;          // 重なりの判定の格子（最も大きい間隔以上）
    const int kChecksPerCall = 2500;           // 1 回の呼び出しで試す候補の上限（残りは次のフレームへ）
}

// 揺れの 1 軸ぶんの状態
class ResultShakeAxis
{
    float intervalTimer = 0.0f;
    float burstTimer = -1.0f;
    float phase = 0.0f;
}

// 背景のサル 1 匹の小刻みな揺れ（X と Z を別々の間隔で短く震わせる）
class ResultMonkeyShake
{
    Transform@ transform;
    Vector3 basePosition;
    ResultShakeAxis x;
    ResultShakeAxis z;
}

// リザルトの背景。空の上に地面のマスを敷き、連れてきたサルの数だけサルを、壊した岩と取ったバナナの 4 つごとに
// 岩とバナナの木を、重ならないよう円の中へ並べる。並びは結果から決まる種の乱数で決める（同じ結果なら同じ並び）
class ResultStage
{
    float monkeyRingRadius = 4.5f;
    float shakeXAmplitude = 0.06f;
    float shakeZAmplitude = 0.05f;
    float shakeXInterval = 1.15f;
    float shakeZInterval = 1.65f;
    float shakeXDuration = 0.28f;
    float shakeZDuration = 0.34f;
    float shakeXFrequency = 34.0f;
    float shakeZFrequency = 27.0f;

    private GameObject@ owner_;
    private int monkeyCount_ = 1;
    private array<Vector3> occupied_;
    private array<ResultMonkeyShake@> monkeys_;

    // 置き済みの位置を引く格子（マスごとに occupied_ の番号を持つ）
    private array<array<int>@> grid_;
    private int gridCells_ = 1;
    private float gridHalf_ = 1.0f;

    // 並べる途中の状態。段 0 = サル ／ 1 = 岩 ／ 2 = バナナの木 ／ 3 = 済み
    private RandomStream@ placement_;
    private int stage_ = 0;
    private int itemIndex_ = 0;
    private int rockCount_ = 0;
    private int treeCount_ = 0;
    private int searchPhase_ = 0;   // 0 = 探し始め ／ 1 = 乱数で探す ／ 2 = 黄金角の渦で探す
    private int attempt_ = 0;
    private float randomPhase_ = 0.0f;
    private int budget_ = 0;

    void Build(GameObject@ owner, int monkeyCount, int brokenRockCount, int bananaHarvestCount, int progressBlocks)
    {
        @owner_ = owner;
        monkeyCount_ = Max(1, monkeyCount);
        const int rockCount = brokenRockCount / 4;
        const int treeCount = bananaHarvestCount / 4;
        const uint seed = uint(0x9E3779B9) ^ (uint(monkeyCount_) * uint(0x85EBCA6B)) ^ (uint(rockCount) * uint(0xC2B2AE35))
            ^ (uint(treeCount) * uint(0x27D4EB2F)) ^ uint(progressBlocks);

        // 地面のマスを敷き、マスごとに色を少しずつ変える
        const int tileRadius = Max(1, int(ceil(SceneRadius())) + 1);
        RandomStream groundRandom(seed ^ uint(0xD1B54A32));
        for (int x = -tileRadius; x <= tileRadius; ++x) {
            for (int z = -tileRadius; z <= tileRadius; ++z) {
                const float amount = groundRandom.Uniform(-1.0f, 1.0f);
                const float luminance = 1.0f + 0.20f * amount;
                const float blue = luminance * (1.0f + 0.12f * amount);
                GameObject@ tile = owner_.InstantiatePrefab("Application/Assets/Prefabs/Game/Ground.prefab", "Result_ground_tile_" + x + "_" + z);
                if (tile is null) {
                    continue;
                }
                tile.transform.position = Vector3(float(x), ResultLayout::kGroundTileY, float(z));
                tile.transform.scale = Vector3(ResultLayout::kGroundTileScale, ResultLayout::kGroundTileScale, ResultLayout::kGroundTileScale);
                tile.material.color = Vector4(luminance, luminance, blue, 1.0f);
            }
        }

        @placement_ = RandomStream(seed);
        rockCount_ = rockCount;
        treeCount_ = treeCount;
        InitGrid();
        // 中央のサルも重なりの判定に入れる（XZ の距離だけを見る）
        AddOccupied(Vector3(0.0f, 0.0f, 0.0f));
        SpawnMonkey("Result_monkey", Vector3(0.0f, ResultLayout::kMonkeyY, 0.0f), 0, placement_.Uniform(0.0f, kTau));
        stage_ = 0;
        itemIndex_ = 1;
        searchPhase_ = 0;
        Advance(ResultLayout::kChecksPerCall);
    }

    // まだ並べ終わっていないか（終わるまで毎フレーム Tick で続きを並べる）
    bool IsBuilding() const
    {
        return stage_ < 3;
    }

    // サルの数が多いほど広げる配置の半径
    float MonkeyLayoutRadius() const
    {
        const float autoExpanded = ResultLayout::kMonkeyLayoutEdgePadding
            + ResultLayout::kMonkeyMinCenterDistance * 0.75f * sqrt(float(Max(1, monkeyCount_)));
        return Max(Max(0.0f, monkeyRingRadius), autoExpanded);
    }

    float SceneRadius() const
    {
        return MonkeyLayoutRadius() + ResultLayout::kDecorationLayoutMargin;
    }

    void Tick(float deltaTime)
    {
        if (IsBuilding()) {
            Advance(ResultLayout::kChecksPerCall);
        }
        const float step = Max(0.0f, deltaTime);
        for (uint i = 0; i < monkeys_.length(); ++i) {
            ResultMonkeyShake@ monkey = monkeys_[i];
            Vector3 position = monkey.basePosition;
            position.x += UpdateAxis(monkey.x, step, shakeXAmplitude, shakeXInterval, shakeXDuration, shakeXFrequency);
            position.z += UpdateAxis(monkey.z, step, shakeZAmplitude, shakeZInterval, shakeZDuration, shakeZFrequency);
            monkey.transform.position = position;
        }
    }

    // ---------------------------------------------------------------- 並べる

    private void SpawnMonkey(const string &in name, const Vector3 &in position, int index, float yRotation)
    {
        GameObject@ monkey = owner_.InstantiatePrefab("Application/Assets/Prefabs/Result/ResultMonkey.prefab", name);
        if (monkey is null) {
            return;
        }
        monkey.transform.position = position;
        monkey.transform.rotation = Vector3(0.0f, yRotation, 0.0f);
        ResultMonkeyShake shake;
        @shake.transform = monkey.transform;
        shake.basePosition = position;
        shake.x.intervalTimer = Wrap(float(index) * 0.37f, Max(0.1f, shakeXInterval));
        shake.z.intervalTimer = Wrap(0.42f + float(index) * 0.53f, Max(0.1f, shakeZInterval));
        shake.x.phase = float(index) * 1.73f;
        shake.z.phase = 0.65f + float(index) * 2.11f;
        monkeys_.insertLast(shake);
    }

    // 決められた数だけ候補を試して並べ進める。サル → 岩 → バナナの木 の順で、置けなかったらその種類は打ち切る
    private void Advance(int checks)
    {
        budget_ = checks;
        while (IsBuilding() && budget_ > 0) {
            if (itemIndex_ >= StageCount()) {
                NextStage();
                continue;
            }
            Vector3 position;
            const int result = Search(position);
            if (result == 0) {
                return;
            }
            if (result == 2) {
                NextStage();
                continue;
            }
            AddOccupied(position);
            if (stage_ == 0) {
                SpawnMonkey("Result_monkey_" + (itemIndex_ + 1), Vector3(position.x, ResultLayout::kMonkeyY, position.z), itemIndex_,
                    placement_.Uniform(0.0f, kTau));
            } else {
                const string prefab = stage_ == 1 ? "Application/Assets/Prefabs/Game/Rock.prefab" : "Application/Assets/Prefabs/Game/BananaTree.prefab";
                const string namePrefix = stage_ == 1 ? "Result_rock_" : "Result_banana_tree_";
                GameObject@ decoration = owner_.InstantiatePrefab(prefab, namePrefix + (itemIndex_ + 1));
                const float yRotation = placement_.Uniform(0.0f, kTau);
                if (decoration !is null) {
                    decoration.transform.position = Vector3(position.x, ResultLayout::kMonkeyY, position.z);
                    decoration.transform.rotation = Vector3(0.0f, yRotation, 0.0f);
                }
            }
            ++itemIndex_;
            searchPhase_ = 0;
        }
    }

    private int StageCount() const
    {
        if (stage_ == 0) {
            return monkeyCount_;
        }
        return stage_ == 1 ? rockCount_ : treeCount_;
    }

    private void NextStage()
    {
        ++stage_;
        itemIndex_ = 0;
        searchPhase_ = 0;
    }

    // 円の中へ乱数で候補を出し、どれとも近すぎない所を探す。詰まったら黄金角の渦で探す。
    // 0 = 試す数を使い切った（次の呼び出しで続ける）／ 1 = 見つけた ／ 2 = 置ける所が無い
    private int Search(Vector3 &out found)
    {
        found = Vector3(0.0f, 0.0f, 0.0f);
        const float edge = stage_ == 0 ? MonkeyLayoutRadius() : SceneRadius();
        const float placementRadius = Max(0.0f, edge - ResultLayout::kMonkeyLayoutEdgePadding);
        const float minCenterDistance = stage_ == 0 ? ResultLayout::kMonkeyMinCenterDistance : ResultLayout::kDecorationMinCenterDistance;
        const float radiusSquared = placementRadius * placementRadius;
        const float minDistanceSquared = minCenterDistance * minCenterDistance;
        if (searchPhase_ == 0) {
            searchPhase_ = 1;
            attempt_ = 0;
        }
        if (searchPhase_ == 1) {
            while (attempt_ < ResultLayout::kRandomPlacementAttempts) {
                if (budget_ <= 0) {
                    return 0;
                }
                --budget_;
                ++attempt_;
                const float x = placement_.Uniform(-placementRadius, placementRadius);
                const float z = placement_.Uniform(-placementRadius, placementRadius);
                if (IsAvailable(x, z, minDistanceSquared, radiusSquared)) {
                    found = Vector3(x, 0.0f, z);
                    return 1;
                }
            }
            searchPhase_ = 2;
            attempt_ = 0;
            randomPhase_ = placement_.Uniform(0.0f, kTau);
        }
        const float goldenAngle = kPi * (3.0f - sqrt(5.0f));
        const int fallbackAttempts = Max(512, StageCount() * 256);
        const float radiusSpan = Max(0.0f, placementRadius - minCenterDistance);
        while (attempt_ < fallbackAttempts) {
            if (budget_ <= 0) {
                return 0;
            }
            --budget_;
            const float normalized = float(attempt_ + 1) / float(fallbackAttempts);
            const float radius = minCenterDistance + radiusSpan * sqrt(normalized);
            const float angle = randomPhase_ + float(attempt_) * goldenAngle;
            ++attempt_;
            const float x = cos(angle) * radius;
            const float z = sin(angle) * radius;
            if (IsAvailable(x, z, minDistanceSquared, radiusSquared)) {
                found = Vector3(x, 0.0f, z);
                return 1;
            }
        }
        return 2;
    }

    private void InitGrid()
    {
        gridHalf_ = SceneRadius() + ResultLayout::kGridCellSize;
        gridCells_ = int(ceil(gridHalf_ * 2.0f / ResultLayout::kGridCellSize)) + 1;
        grid_.resize(0);
        for (int i = 0; i < gridCells_ * gridCells_; ++i) {
            grid_.insertLast(array<int>());
        }
    }

    private int GridCoord(float value) const
    {
        return Clamp(int(floor((value + gridHalf_) / ResultLayout::kGridCellSize)), 0, gridCells_ - 1);
    }

    private void AddOccupied(const Vector3 &in position)
    {
        occupied_.insertLast(position);
        grid_[GridCoord(position.z) * gridCells_ + GridCoord(position.x)].insertLast(int(occupied_.length()) - 1);
    }

    // 近すぎる置き済みは隣り合う 3x3 マスの中にしか無い（マスの幅が間隔以上なので）
    private bool IsAvailable(float x, float z, float minDistanceSquared, float radiusSquared) const
    {
        if (x * x + z * z > radiusSquared) {
            return false;
        }
        const int cellX = GridCoord(x);
        const int cellZ = GridCoord(z);
        for (int gz = Max(0, cellZ - 1); gz <= Min(gridCells_ - 1, cellZ + 1); ++gz) {
            for (int gx = Max(0, cellX - 1); gx <= Min(gridCells_ - 1, cellX + 1); ++gx) {
                const array<int>@ cell = grid_[gz * gridCells_ + gx];
                for (uint i = 0; i < cell.length(); ++i) {
                    const Vector3 placed = occupied_[cell[i]];
                    const float dx = x - placed.x;
                    const float dz = z - placed.z;
                    if (dx * dx + dz * dz < minDistanceSquared) {
                        return false;
                    }
                }
            }
        }
        return true;
    }

    // ---------------------------------------------------------------- 揺れ

    // 間隔ごとに duration 秒だけ、山なりの包絡で sin を掛けて震わせる
    private float UpdateAxis(ResultShakeAxis@ axis, float deltaTime, float amplitude, float interval, float duration, float frequency)
    {
        if (amplitude <= 0.0f || interval <= 0.0f || duration <= 0.0f || frequency <= 0.0f) {
            axis.burstTimer = -1.0f;
            return 0.0f;
        }
        axis.intervalTimer += deltaTime;
        if (axis.burstTimer < 0.0f && axis.intervalTimer >= interval) {
            axis.intervalTimer = Wrap(axis.intervalTimer, interval);
            axis.burstTimer = 0.0f;
        }
        if (axis.burstTimer < 0.0f) {
            return 0.0f;
        }
        axis.burstTimer += deltaTime;
        const float progress = Clamp(axis.burstTimer / duration, 0.0f, 1.0f);
        const float offset = sin(axis.phase + axis.burstTimer * frequency) * amplitude * sin(progress * kPi);
        if (axis.burstTimer >= duration) {
            axis.burstTimer = -1.0f;
            return 0.0f;
        }
        return offset;
    }
}

// 0 以上の値を [0, length) へ畳む
float Wrap(float value, float length)
{
    return value - length * floor(value / length);
}
