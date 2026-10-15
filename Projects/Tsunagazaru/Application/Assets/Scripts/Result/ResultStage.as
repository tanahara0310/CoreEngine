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

        RandomStream placement(seed);
        // 中央のサルも重なりの判定に入れる（XZ の距離だけを見る）
        occupied_.insertLast(Vector3(0.0f, 0.0f, 0.0f));
        SpawnMonkey("Result_monkey", Vector3(0.0f, ResultLayout::kMonkeyY, 0.0f), 0, placement.Uniform(0.0f, kTau));

        if (monkeyCount_ > 1) {
            const float placementRadius = Max(0.0f, MonkeyLayoutRadius() - ResultLayout::kMonkeyLayoutEdgePadding);
            for (int index = 1; index < monkeyCount_; ++index) {
                Vector3 position;
                if (!FindPosition(placement, placementRadius, ResultLayout::kMonkeyMinCenterDistance, monkeyCount_, position)) {
                    break;
                }
                occupied_.insertLast(position);
                SpawnMonkey("Result_monkey_" + (index + 1), Vector3(position.x, ResultLayout::kMonkeyY, position.z), index,
                    placement.Uniform(0.0f, kTau));
            }
        }

        PlaceDecorations(placement, "Application/Assets/Prefabs/Game/Rock.prefab", "Result_rock_", rockCount);
        PlaceDecorations(placement, "Application/Assets/Prefabs/Game/BananaTree.prefab", "Result_banana_tree_", treeCount);
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

    private void PlaceDecorations(RandomStream@ placement, const string &in prefab, const string &in namePrefix, int count)
    {
        if (count <= 0) {
            return;
        }
        const float placementRadius = Max(0.0f, SceneRadius() - ResultLayout::kMonkeyLayoutEdgePadding);
        for (int index = 0; index < count; ++index) {
            Vector3 position;
            if (!FindPosition(placement, placementRadius, ResultLayout::kDecorationMinCenterDistance, count, position)) {
                break;
            }
            occupied_.insertLast(position);
            GameObject@ decoration = owner_.InstantiatePrefab(prefab, namePrefix + (index + 1));
            const float yRotation = placement.Uniform(0.0f, kTau);
            if (decoration is null) {
                continue;
            }
            decoration.transform.position = Vector3(position.x, ResultLayout::kMonkeyY, position.z);
            decoration.transform.rotation = Vector3(0.0f, yRotation, 0.0f);
        }
    }

    // 円の中へ乱数で候補を出し、どれとも近すぎない所を探す。詰まったら黄金角の渦で探す
    private bool FindPosition(RandomStream@ placement, float placementRadius, float minCenterDistance, int fallbackScale, Vector3 &out found)
    {
        const float radiusSquared = placementRadius * placementRadius;
        const float minDistanceSquared = minCenterDistance * minCenterDistance;
        for (int attempt = 0; attempt < ResultLayout::kRandomPlacementAttempts; ++attempt) {
            const float x = placement.Uniform(-placementRadius, placementRadius);
            const float z = placement.Uniform(-placementRadius, placementRadius);
            if (IsAvailable(x, z, minDistanceSquared, radiusSquared)) {
                found = Vector3(x, 0.0f, z);
                return true;
            }
        }
        const float goldenAngle = kPi * (3.0f - sqrt(5.0f));
        const int fallbackAttempts = Max(512, fallbackScale * 256);
        const float radiusSpan = Max(0.0f, placementRadius - minCenterDistance);
        const float randomPhase = placement.Uniform(0.0f, kTau);
        for (int attempt = 0; attempt < fallbackAttempts; ++attempt) {
            const float normalized = float(attempt + 1) / float(fallbackAttempts);
            const float radius = minCenterDistance + radiusSpan * sqrt(normalized);
            const float angle = randomPhase + float(attempt) * goldenAngle;
            const float x = cos(angle) * radius;
            const float z = sin(angle) * radius;
            if (IsAvailable(x, z, minDistanceSquared, radiusSquared)) {
                found = Vector3(x, 0.0f, z);
                return true;
            }
        }
        found = Vector3(0.0f, 0.0f, 0.0f);
        return false;
    }

    private bool IsAvailable(float x, float z, float minDistanceSquared, float radiusSquared) const
    {
        if (x * x + z * z > radiusSquared) {
            return false;
        }
        for (uint i = 0; i < occupied_.length(); ++i) {
            const float dx = x - occupied_[i].x;
            const float dz = z - occupied_[i].z;
            if (dx * dx + dz * dz < minDistanceSquared) {
                return false;
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
