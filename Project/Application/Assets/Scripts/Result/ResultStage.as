const float kResultStageMonkeyY = 7.8f;
const float kResultStageGridSize = 1.0f;
const float kResultStageModelBlockSize = 1.6f;
const float kResultStageMonkeyMinCenterDistance = 1.9f;
const float kResultStageLayoutEdgePadding = 0.9f;
const float kResultStageDecorationMinCenterDistance = 2.1f;
const float kResultStageDecorationLayoutMargin = 3.0f;
const int kResultStageRandomPlacementAttempts = 512;
const float kResultStagePi = 3.14159265f;

// リザルトの地面を敷き、サルと岩とバナナの木を置いて、ゲーム視点のカメラをサルの周りで回す。
// 置き方はスコアから決めた種の乱数で決まるので、同じスコアなら同じ並びになる
[DisplayName("リザルトの地面とサル")]
class ResultStage : ScriptComponent
{
    [DisplayName("サルを置く円の半径（m。サルが多いと広げる）"), Range(0, 30)]
    float monkeyRingRadius = 4.5f;

    [DisplayName("カメラが回る速さ（ラジアン/秒）"), Range(-2, 2)]
    float cameraOrbitSpeed = 0.32f;

    [DisplayName("床タイルのプレハブ")]
    string groundTilePrefab = "Application/Assets/Prefabs/Result/ResultGroundTile.prefab";

    [DisplayName("サルのプレハブ")]
    string monkeyPrefab = "Application/Assets/Prefabs/Result/ResultMonkey.prefab";

    [DisplayName("岩のプレハブ")]
    string rockPrefab = "Application/Assets/Prefabs/Result/ResultRock.prefab";

    [DisplayName("バナナの木のプレハブ")]
    string bananaTreePrefab = "Application/Assets/Prefabs/Result/ResultBananaTree.prefab";

    private int monkeyCount_ = 1;
    private float cameraOrbitAngle_ = 0.0f;

    void Awake() override
    {
        monkeyCount_ = Max(1, Session::GetInt("GameResult.MonkeyCount", 0));
        const int rockCount = Session::GetInt("GameResult.BrokenRockCount", 0) / 4;
        const int bananaTreeCount = Session::GetInt("GameResult.BananaHarvestCount", 0) / 4;
        const uint seed = uint(0x9E3779B9)
            ^ uint(monkeyCount_) * uint(0x85EBCA6B)
            ^ uint(rockCount) * uint(0xC2B2AE35)
            ^ uint(bananaTreeCount) * uint(0x27D4EB2F)
            ^ uint(Session::GetInt("GameResult.HorizontalProgressBlocks", 0));

        PlaceGround(seed);

        RandomStream@ random = RandomStream(seed);
        array<Vector3> occupied = { Vector3(0.0f, 0.0f, 0.0f) };
        PlaceMonkeys(random, occupied);
        PlaceDecorations(random, occupied, rockPrefab, "Result_rock_", rockCount);
        PlaceDecorations(random, occupied, bananaTreePrefab, "Result_banana_tree_", bananaTreeCount);
    }

    void LateUpdate() override
    {
        GameCamera@ camera = Scene::GetGameCamera();
        if (!camera.exists) {
            return;
        }

        const float cameraDistance = Max(18.0f, SceneLayoutRadius() + 14.0f);
        cameraOrbitAngle_ += cameraOrbitSpeed * Max(0.0f, Time::UnscaledDeltaTime());
        cameraOrbitAngle_ = fmod(cameraOrbitAngle_, 2.0f * kResultStagePi);
        camera.position = Vector3(
            sin(cameraOrbitAngle_) * cameraDistance,
            kResultStageMonkeyY + 5.5f,
            -cos(cameraOrbitAngle_) * cameraDistance);
        camera.LookAt(Vector3(0.0f, kResultStageMonkeyY, 0.0f));
        camera.UpdateMatrix();
    }

    // サルの数から、サルを置く円の半径を決める（多いときは広げる）
    private float MonkeyLayoutRadius() const
    {
        const float configuredRadius = Max(0.0f, monkeyRingRadius);
        const float count = float(Max(1, monkeyCount_));
        const float autoExpandedRadius = kResultStageLayoutEdgePadding
            + kResultStageMonkeyMinCenterDistance * 0.75f * sqrt(count);
        return Max(configuredRadius, autoExpandedRadius);
    }

    // 飾りまで含めて置く円の半径
    private float SceneLayoutRadius() const
    {
        return MonkeyLayoutRadius() + kResultStageDecorationLayoutMargin;
    }

    // 置く範囲を覆う正方形に床タイルを敷き、タイルごとに明るさと青みを揺らす
    private void PlaceGround(uint seed)
    {
        const float tileScale = kResultStageGridSize / kResultStageModelBlockSize;
        const float tileY = kResultStageMonkeyY - kResultStageModelBlockSize * tileScale;
        const int tileRadius = Max(1, int(ceil(SceneLayoutRadius())) + 1);
        RandomStream@ random = RandomStream(seed ^ uint(0xD1B54A32));

        for (int gridX = -tileRadius; gridX <= tileRadius; ++gridX) {
            for (int gridZ = -tileRadius; gridZ <= tileRadius; ++gridZ) {
                const float amount = random.Uniform(-1.0f, 1.0f);
                const float luminance = 1.0f + 0.20f * amount;
                const float blue = luminance * (1.0f + 0.12f * amount);

                GameObject@ tile = owner.InstantiatePrefab(groundTilePrefab, "Result_ground_tile_" + gridX + "_" + gridZ);
                if (tile is null) {
                    continue;
                }
                tile.transform.position = Vector3(float(gridX) * kResultStageGridSize, tileY, float(gridZ) * kResultStageGridSize);
                tile.transform.scale = Vector3(tileScale, tileScale, tileScale);
                tile.material.color = Vector4(luminance, luminance, blue, 1.0f);
            }
        }
    }

    // 中央に 1 体、残りを円の中の空いた位置へ置く（置けなくなったらそこで止める）
    private void PlaceMonkeys(RandomStream@ random, array<Vector3>@ occupied)
    {
        const float fullTurn = 2.0f * kResultStagePi;
        SpawnMonkey("Result_monkey", Vector3(0.0f, kResultStageMonkeyY, 0.0f), 0, random.Uniform(0.0f, fullTurn));
        if (monkeyCount_ <= 1) {
            return;
        }

        const float placementRadius = Max(0.0f, MonkeyLayoutRadius() - kResultStageLayoutEdgePadding);
        for (int index = 1; index < monkeyCount_; ++index) {
            Vector3 position;
            if (!FindPosition(random, occupied, placementRadius, kResultStageMonkeyMinCenterDistance, monkeyCount_, position)) {
                break;
            }
            occupied.insertLast(position);
            SpawnMonkey("Result_monkey_" + (index + 1), Vector3(position.x, kResultStageMonkeyY, position.z), index,
                random.Uniform(0.0f, fullTurn));
        }
    }

    // 飾りをサルより広い円の中の空いた位置へ置く（置けなくなったらそこで止める）
    private void PlaceDecorations(RandomStream@ random, array<Vector3>@ occupied, const string &in prefabPath,
                                  const string &in namePrefix, int count)
    {
        if (count == 0) {
            return;
        }

        const float fullTurn = 2.0f * kResultStagePi;
        const float placementRadius = Max(0.0f, SceneLayoutRadius() - kResultStageLayoutEdgePadding);
        for (int index = 0; index < count; ++index) {
            Vector3 position;
            if (!FindPosition(random, occupied, placementRadius, kResultStageDecorationMinCenterDistance, count, position)) {
                break;
            }
            occupied.insertLast(position);
            SpawnDecoration(prefabPath, namePrefix + (index + 1), Vector3(position.x, kResultStageMonkeyY, position.z),
                random.Uniform(0.0f, fullTurn));
        }
    }

    // 円の中へ候補をランダムに作り、置いたものから離れた位置を探す。見つからなければ黄金角で並べた候補も試す
    private bool FindPosition(RandomStream@ random, const array<Vector3> &in occupied, float placementRadius,
                              float minCenterDistance, int fallbackCount, Vector3 &out position)
    {
        const float placementRadiusSquared = placementRadius * placementRadius;
        const float minCenterDistanceSquared = minCenterDistance * minCenterDistance;

        for (int attempt = 0; attempt < kResultStageRandomPlacementAttempts; ++attempt) {
            const float x = random.Uniform(-placementRadius, placementRadius);
            const float z = random.Uniform(-placementRadius, placementRadius);
            if (IsPositionAvailable(occupied, x, z, minCenterDistanceSquared, placementRadiusSquared)) {
                position = Vector3(x, 0.0f, z);
                return true;
            }
        }

        const float goldenAngle = kResultStagePi * (3.0f - sqrt(5.0f));
        const int fallbackAttempts = Max(512, fallbackCount * 256);
        const float radiusSpan = Max(0.0f, placementRadius - minCenterDistance);
        const float randomPhase = random.Uniform(0.0f, 2.0f * kResultStagePi);
        for (int attempt = 0; attempt < fallbackAttempts; ++attempt) {
            const float normalizedAttempt = float(attempt + 1) / float(fallbackAttempts);
            const float candidateRadius = minCenterDistance + radiusSpan * sqrt(normalizedAttempt);
            const float angle = randomPhase + float(attempt) * goldenAngle;
            const float x = cos(angle) * candidateRadius;
            const float z = sin(angle) * candidateRadius;
            if (IsPositionAvailable(occupied, x, z, minCenterDistanceSquared, placementRadiusSquared)) {
                position = Vector3(x, 0.0f, z);
                return true;
            }
        }

        position = Vector3();
        return false;
    }

    // 円の中にあり、置いたどれとも XZ で minCenterDistance 以上離れているか
    private bool IsPositionAvailable(const array<Vector3> &in occupied, float x, float z,
                                     float minCenterDistanceSquared, float placementRadiusSquared) const
    {
        const float distanceFromCenterSquared = x * x + z * z;
        if (distanceFromCenterSquared > placementRadiusSquared) {
            return false;
        }

        for (uint i = 0; i < occupied.length(); ++i) {
            const float deltaX = x - occupied[i].x;
            const float deltaZ = z - occupied[i].z;
            const float distanceSquared = deltaX * deltaX + deltaZ * deltaZ;
            if (distanceSquared < minCenterDistanceSquared) {
                return false;
            }
        }
        return true;
    }

    private void SpawnMonkey(const string &in name, const Vector3 &in position, int monkeyIndex, float yRotation)
    {
        GameObject@ monkey = owner.InstantiatePrefab(monkeyPrefab, name);
        if (monkey is null) {
            return;
        }
        monkey.transform.position = position;
        monkey.transform.rotation = Vector3(0.0f, yRotation, 0.0f);

        ResultMonkeyShake@ shake;
        if (monkey.GetComponent(@shake)) {
            shake.monkeyIndex = monkeyIndex;
        }
    }

    private void SpawnDecoration(const string &in prefabPath, const string &in name, const Vector3 &in position, float yRotation)
    {
        GameObject@ decoration = owner.InstantiatePrefab(prefabPath, name);
        if (decoration is null) {
            return;
        }
        decoration.transform.position = position;
        decoration.transform.rotation = Vector3(0.0f, yRotation, 0.0f);
    }
}
