// 1 マスに置いている物
class MapCellView
{
    int chip = -1;
    PooledModel@ ground;
    PooledModel@ skirt;
    PooledModel@ decor;
    ModelPool@ decorPool;
    PooledModel@ water;
    PooledModel@ fallNear;
    PooledModel@ fallFar;
    PooledModel@ stationMonkey;
}

// 1 列ぶんのマス
class MapColumnView
{
    int x;
    array<MapCellView@> cells;
}

// バナナの木の姿勢
class TreePose
{
    Vector3 rotation;
    Vector3 scale;
}

// 駅が弾む・使い終わった・木がしなる、の経過
class MapCellTimer
{
    int x;
    int z;
    float elapsed = 0.0f;
    float towardX = 0.0f;
    float towardZ = 0.0f;
}

// マップを描く。カメラの周り ±viewDistanceX 列のマスへ、地面・柱・駅・岩・木・草・水を置く。
// 置いた物は列が描く範囲から出るまで同じマスに付けたままにし、動くもの（色ムラ・駅・木）だけを毎フレーム書き換える
[DisplayName("マップの表示")]
class MapView : ScriptComponent
{
    [DisplayName("マスの大きさ"), Range(0.01, 20)]
    float gridSize = 1.0f;

    [DisplayName("描く範囲（左右の列数）"), Range(1, 200)]
    int viewDistanceX = 20;

    [DisplayName("地面の柱の長さ"), Range(0, 20)]
    float groundSkirtHeight = 4.5f;

    [DisplayName("色ムラの明度のふり幅"), Range(0, 1)]
    float groundTintStrength = 0.2f;

    [DisplayName("色ムラの色味のふり幅"), Range(0, 1)]
    float groundTintHueSwing = 0.12f;

    [DisplayName("色ムラを弱め始める距離"), Range(0, 300)]
    float groundTintFadeStart = 16.0f;

    [DisplayName("色ムラが消えるまでの距離"), Range(0, 300)]
    float groundTintFadeRange = 22.0f;

    [DisplayName("駅の反動の時間"), Range(0.01, 5)]
    float stationPopDuration = 0.45f;

    [DisplayName("駅の沈み込み"), Range(0, 1)]
    float stationPopSquash = 0.22f;

    [DisplayName("駅の待機の呼吸"), Range(0, 0.5)]
    float stationIdleBreath = 0.02f;

    [DisplayName("駅の接近時の呼吸"), Range(0, 0.5)]
    float stationWakeBreath = 0.07f;

    [DisplayName("駅の待機の速さ"), Range(0, 5)]
    float stationIdleSpeed = 0.45f;

    [DisplayName("駅の接近時の速さ"), Range(0, 8)]
    float stationWakeSpeed = 1.5f;

    [DisplayName("駅が起き出す距離"), Range(0, 60)]
    float stationWakeRange = 7.0f;

    [DisplayName("済んだ駅の暗さ"), Range(0.2, 1)]
    float stationUsedTint = 0.78f;

    [DisplayName("駅で待つサルの大きさ"), Range(0, 3)]
    float stationMonkeyScale = 0.85f;

    [DisplayName("駅で待つサルの跳ねる高さ"), Range(0, 1)]
    float stationMonkeyHop = 0.16f;

    [DisplayName("駅で待つサルの飛び降りる距離"), Range(0, 3)]
    float stationMonkeyLeap = 0.7f;

    [DisplayName("木のしなりの時間"), Range(0.01, 5)]
    float bananaTreeShakeDuration = 0.55f;

    [DisplayName("木のしなりの角度"), Range(0, 1.5)]
    float bananaTreeShakeLean = 0.17f;

    [DisplayName("木のしなりの縮み"), Range(0, 1)]
    float bananaTreeShakeSquash = 0.12f;

    [DisplayName("木の揺れの角度"), Range(0, 0.5)]
    float bananaTreeSwayAngle = 0.07f;

    [DisplayName("木の揺れの速さ"), Range(0, 5)]
    float bananaTreeSwaySpeed = 0.6f;

    [DisplayName("木の揺れの 2 本目の速さ"), Range(0, 5)]
    float bananaTreeSwaySubSpeed = 0.27f;

    [DisplayName("木の揺れの 2 本目の割合"), Range(0, 1)]
    float bananaTreeSwaySubRate = 0.45f;

    [DisplayName("木の風下への傾き"), Range(0, 0.5)]
    float bananaTreeSwayLean = 0.03f;

    [DisplayName("木のねじれの角度"), Range(0, 1.5)]
    float bananaTreeSwayYaw = 0.18f;

    [DisplayName("木の葉の開閉"), Range(0, 0.5)]
    float bananaTreeSwayBreath = 0.045f;

    [DisplayName("風向き X")]
    float bananaTreeWindDirX = 1.0f;

    [DisplayName("風向き Z")]
    float bananaTreeWindDirZ = 0.0f;

    [DisplayName("木を地面へ埋める深さ"), Range(0, 0.5)]
    float bananaTreeSinkDepth = 0.025f;

    [DisplayName("水面の高さ（地面ブロックの何割）"), Range(0, 1)]
    float waterLevelRatio = 0.65f;

    [DisplayName("滝を出す")]
    bool fallEnabled = true;

    [DisplayName("滝の長さ（マス）"), Range(0, 50)]
    float fallLengthRatio = 6.0f;

    [DisplayName("距離の目盛りの間隔（m）"), Range(1, 100)]
    int distanceMarkerInterval = 5;

    private MapGenerator@ map_;
    private ModelPool@ groundPool_;
    private ModelPool@ skirtPool_;
    private ModelPool@ stationPool_;
    private ModelPool@ rockPool_;
    private ModelPool@ hardRockPool_;
    private ModelPool@ treePool_;
    private ModelPool@ grassPool_;
    private ModelPool@ waterPool_;
    private ModelPool@ fallPool_;
    private ModelPool@ stationMonkeyPool_;
    private ModelPool@ markerPool_;
    private array<MapColumnView@> columns_;
    private array<PooledModel@> markers_;
    private array<MapCellTimer@> stationPops_;
    private array<MapCellTimer@> usedStations_;
    private array<MapCellTimer@> treeShakes_;
    private int viewStart_ = 0;
    private int viewEnd_ = 0;
    private Vector3 cameraPosition_;
    private float cameraGridX_ = 0.0f;

    // GameDirector が最初に呼ぶ
    void Setup(MapGenerator@ map)
    {
        @map_ = map;
        const string root = "Application/Assets/Prefabs/Game/";
        @groundPool_ = ModelPool(owner, root + "Ground.prefab", "Ground_");
        @skirtPool_ = ModelPool(owner, root + "Ground.prefab", "GroundSkirt_");
        @stationPool_ = ModelPool(owner, root + "Station.prefab", "Station_");
        @rockPool_ = ModelPool(owner, root + "Rock.prefab", "Rock_");
        @hardRockPool_ = ModelPool(owner, root + "HardRock.prefab", "HardRock_");
        @treePool_ = ModelPool(owner, root + "BananaTree.prefab", "BananaTree_");
        @grassPool_ = ModelPool(owner, root + "Grass.prefab", "Grass_");
        @waterPool_ = ModelPool(owner, root + "Water.prefab", "Water_");
        @fallPool_ = ModelPool(owner, root + "Water.prefab", "WaterFall_");
        @stationMonkeyPool_ = ModelPool(owner, root + "Monkey.prefab", "StationMonkey_");
        @markerPool_ = ModelPool(owner, root + "DistanceMarker.prefab", "DistanceMarker_");
    }

    // ---------------------------------------------------------------- 演出の口

    // 駅を弾ませる（減速した駅か連結した駅＝使い終わった駅）
    void PlayStationPop(int gridX, int gridZ)
    {
        MarkStationUsed(gridX, gridZ);
        for (uint i = 0; i < stationPops_.length(); ++i) {
            if (stationPops_[i].x == gridX && stationPops_[i].z == gridZ) {
                stationPops_[i].elapsed = 0.0f;
                return;
            }
        }
        MapCellTimer@ pop = MapCellTimer();
        pop.x = gridX;
        pop.z = gridZ;
        stationPops_.insertLast(pop);
    }

    // バナナを取られた木を、取ったサルの向きへしならせる
    void PlayBananaTreeShake(int gridX, int gridZ, float towardX, float towardZ)
    {
        for (uint i = 0; i < treeShakes_.length(); ++i) {
            if (treeShakes_[i].x == gridX && treeShakes_[i].z == gridZ) {
                treeShakes_[i].towardX = towardX;
                treeShakes_[i].towardZ = towardZ;
                treeShakes_[i].elapsed = 0.0f;
                return;
            }
        }
        MapCellTimer@ shake = MapCellTimer();
        shake.x = gridX;
        shake.z = gridZ;
        shake.towardX = towardX;
        shake.towardZ = towardZ;
        treeShakes_.insertLast(shake);
    }

    // ---------------------------------------------------------------- 毎フレーム

    // GameDirector が Update から呼ぶ
    void Tick(float deltaTime)
    {
        if (map_ is null) {
            return;
        }
        AdvanceTimers(stationPops_, deltaTime, stationPopDuration);
        AdvanceTimers(treeShakes_, deltaTime, bananaTreeShakeDuration);
        for (uint i = 0; i < usedStations_.length(); ++i) {
            usedStations_[i].elapsed += Max(deltaTime, 0.0f);
        }

        GameCamera@ camera = Scene::GetGameCamera();
        cameraPosition_ = camera.position;
        cameraGridX_ = cameraPosition_.x / gridSize;
        const float roundedX = floor(cameraGridX_ + 0.5f);
        const int center = roundedX > 0.0f ? int(roundedX) : 0;
        const int startX = Max(center - viewDistanceX, 0);
        const int endX = center + viewDistanceX;
        ForgetUsedStationsBefore(startX);

        map_.CreateToX(endX);
        const int drawEnd = Min(endX, map_.GetSizeX());
        UpdateDistanceMarkers(startX, drawEnd);
        SyncColumns(startX, drawEnd);

        for (uint i = 0; i < columns_.length(); ++i) {
            MapColumnView@ column = columns_[i];
            for (int z = 0; z < map_.mapSizeZ; ++z) {
                UpdateCell(column.x, z, column.cells[z]);
            }
        }
    }

    void OnDestroy() override
    {
        @map_ = null;
    }

    // ---------------------------------------------------------------- 列の出し入れ

    // 描く範囲が変わったら、出た列の物を返し、入った列を作る
    private void SyncColumns(int startX, int endX)
    {
        if (startX == viewStart_ && endX == viewEnd_ && columns_.length() == uint(Max(endX - startX, 0))) {
            return;
        }
        array<MapColumnView@> next;
        for (int x = startX; x < endX; ++x) {
            MapColumnView@ column;
            for (uint i = 0; i < columns_.length(); ++i) {
                if (columns_[i] !is null && columns_[i].x == x) {
                    @column = columns_[i];
                    @columns_[i] = null;
                    break;
                }
            }
            if (column is null) {
                @column = MapColumnView();
                column.x = x;
                column.cells.resize(map_.mapSizeZ);
                for (int z = 0; z < map_.mapSizeZ; ++z) {
                    @column.cells[z] = MapCellView();
                }
            }
            next.insertLast(column);
        }
        for (uint i = 0; i < columns_.length(); ++i) {
            if (columns_[i] !is null) {
                for (uint z = 0; z < columns_[i].cells.length(); ++z) {
                    ReleaseCell(columns_[i].cells[z]);
                }
            }
        }
        columns_ = next;
        viewStart_ = startX;
        viewEnd_ = endX;
    }

    private void ReleaseCell(MapCellView@ cell)
    {
        groundPool_.Release(cell.ground);
        skirtPool_.Release(cell.skirt);
        if (cell.decorPool !is null) {
            cell.decorPool.Release(cell.decor);
        }
        waterPool_.Release(cell.water);
        fallPool_.Release(cell.fallNear);
        fallPool_.Release(cell.fallFar);
        stationMonkeyPool_.Release(cell.stationMonkey);
        @cell.ground = null;
        @cell.skirt = null;
        @cell.decor = null;
        @cell.decorPool = null;
        @cell.water = null;
        @cell.fallNear = null;
        @cell.fallFar = null;
        @cell.stationMonkey = null;
        cell.chip = -1;
    }

    // マスの種類が変わったら（岩を壊したなど）置く物を作り直す
    private void RebuildCell(int x, int z, MapCellView@ cell, int chip)
    {
        ReleaseCell(cell);
        cell.chip = chip;

        const float modelScale = BlockLayout::GetScale(gridSize);
        const Vector3 scale(modelScale, modelScale, modelScale);
        const float groundHeight = BlockLayout::GetGroundHeight(gridSize);
        const float surfaceHeight = BlockLayout::GetSurfaceHeight(gridSize);
        const Vector3 groundPosition(x * gridSize, groundHeight, z * gridSize);

        // 水以外は地面を敷く。空白も「敷けない床」として地面の上に硬い岩を立てる
        if (chip != MapChip::Water) {
            @cell.ground = groundPool_.Acquire();
            cell.ground.Place(groundPosition, Vector3(0, 0, 0), scale);
            if (groundSkirtHeight > 0.0f) {
                // 同じ地面ブロックを上下逆さにして、底面から下へ吊るして柱にする
                @cell.skirt = skirtPool_.Acquire();
                cell.skirt.Place(groundPosition, Vector3(kPi, 0, 0),
                    Vector3(modelScale, BlockLayout::GetGroundSkirtYScale(gridSize, groundSkirtHeight), modelScale));
            }
        } else {
            const float waterHeight = GetWaterSurfaceHeight();
            @cell.water = waterPool_.Acquire();
            cell.water.Place(Vector3(x * gridSize, waterHeight, z * gridSize), Vector3(0, 0, 0),
                Vector3(gridSize * 0.1f, 1.0f, gridSize * 0.1f));
            // Z の両端の外は何も無いので、そこに面した水から滝を垂らす
            if (fallEnabled) {
                const float fallLength = fallLengthRatio * gridSize;
                if (z == 0) {
                    @cell.fallNear = fallPool_.Acquire();
                    cell.fallNear.Place(Vector3(x * gridSize, waterHeight - fallLength * 0.5f, -0.5f * gridSize),
                        Vector3(-kPi * 0.5f, 0, 0), Vector3(gridSize * 0.1f, 1.0f, fallLength * 0.1f));
                }
                if (z + 1 == map_.mapSizeZ) {
                    @cell.fallFar = fallPool_.Acquire();
                    cell.fallFar.Place(Vector3(x * gridSize, waterHeight - fallLength * 0.5f, (z + 0.5f) * gridSize),
                        Vector3(kPi * 0.5f, 0, 0), Vector3(gridSize * 0.1f, 1.0f, fallLength * 0.1f));
                }
            }
        }

        const Vector3 onSurface(x * gridSize, surfaceHeight, z * gridSize);
        if (chip == MapChip::Station) {
            @cell.decorPool = stationPool_;
        } else if (chip == MapChip::Resource) {
            @cell.decorPool = rockPool_;
        } else if (chip == MapChip::Void) {
            @cell.decorPool = hardRockPool_;
        } else if (chip == MapChip::BananaTree) {
            @cell.decorPool = treePool_;
        } else if (chip == MapChip::Grass) {
            @cell.decorPool = grassPool_;
        }
        if (cell.decorPool !is null) {
            @cell.decor = cell.decorPool.Acquire();
            cell.decor.Place(onSurface, Vector3(0, 0, 0), scale);
        }
        if (chip == MapChip::Station && stationMonkeyScale > 0.0f) {
            @cell.stationMonkey = stationMonkeyPool_.Acquire();
        }
    }

    // ---------------------------------------------------------------- マスごと

    private void UpdateCell(int x, int z, MapCellView@ cell)
    {
        const int chip = map_.GetChip(x, z);
        if (chip != cell.chip) {
            RebuildCell(x, z, cell, chip);
        }

        if (cell.ground !is null) {
            // マスごとに色をわずかに散らす。柱もブロックと同じ色にして 1 本に見せる
            const Vector3 groundPosition(x * gridSize, BlockLayout::GetGroundHeight(gridSize), z * gridSize);
            const Vector4 tint = CalcGroundTint(x, z, Distance(groundPosition, cameraPosition_));
            cell.ground.SetColor(tint);
            if (cell.skirt !is null) {
                cell.skirt.SetColor(tint);
            }
        }

        const float modelScale = BlockLayout::GetScale(gridSize);
        const float surfaceHeight = BlockLayout::GetSurfaceHeight(gridSize);
        if (chip == MapChip::Station && cell.decor !is null) {
            const Vector3 popScale = GetStationPopScale(x, z);
            const Vector3 stationScale = ApplyStationIdle(x, z,
                Vector3(modelScale * popScale.x, modelScale * popScale.y, modelScale * popScale.z));
            cell.decor.Place(Vector3(x * gridSize, surfaceHeight, z * gridSize), Vector3(0, 0, 0), stationScale);
            cell.decor.SetColor(GetStationTint(x, z));
            UpdateStationMonkey(x, z, cell,
                surfaceHeight + BlockLayout::kStationModelHeight * stationScale.y, modelScale);
        } else if (chip == MapChip::BananaTree && cell.decor !is null) {
            TreePose@ pose = TreePose();
            pose.scale = Vector3(modelScale, modelScale, modelScale);
            ApplyBananaTreeSway(x, z, pose);
            ApplyBananaTreeShake(x, z, pose);
            // 傾けると根元のフチが浮くので、その分だけ沈めて隠す
            cell.decor.Place(Vector3(x * gridSize, surfaceHeight - bananaTreeSinkDepth * gridSize, z * gridSize),
                pose.rotation, pose.scale);
        }
    }

    // 遠いマスほど色ムラを弱め、地平線側は元の一色へ戻す
    private Vector4 CalcGroundTint(int x, int z, float cameraDistance) const
    {
        float fade = 1.0f;
        if (groundTintFadeRange > 0.0f) {
            fade = Clamp(1.0f - (cameraDistance - groundTintFadeStart) / groundTintFadeRange, 0.0f, 1.0f);
        }
        const float amount = (HashCell01(x, z) * 2.0f - 1.0f) * fade;
        const float luminance = 1.0f + groundTintStrength * amount;
        // 青だけ逆位相に振って、明るいマスは色が薄く・暗いマスは色が濃い芝のムラにする
        const float blue = luminance * (1.0f + groundTintHueSwing * amount);
        return Vector4(luminance, luminance, blue, 1.0f);
    }

    private float GetWaterSurfaceHeight() const
    {
        return BlockLayout::GetGroundHeight(gridSize) + waterLevelRatio * gridSize;
    }

    // ---------------------------------------------------------------- 駅

    private Vector3 GetStationPopScale(int x, int z) const
    {
        for (uint i = 0; i < stationPops_.length(); ++i) {
            const MapCellTimer@ pop = stationPops_[i];
            if (pop.x != x || pop.z != z) {
                continue;
            }
            // 反動でいったん沈み、跳ね返って収まる（減衰する正弦波 1 周期）
            const float progress = Clamp(pop.elapsed / stationPopDuration, 0.0f, 1.0f);
            const float recoil = sin(progress * kTau) * (1.0f - progress) * stationPopSquash;
            return Vector3(1.0f + recoil * 0.6f, 1.0f - recoil, 1.0f + recoil * 0.6f);
        }
        return Vector3(1.0f, 1.0f, 1.0f);
    }

    private void MarkStationUsed(int gridX, int gridZ)
    {
        if (FindUsedStation(gridX, gridZ) !is null) {
            return;
        }
        MapCellTimer@ used = MapCellTimer();
        used.x = gridX;
        used.z = gridZ;
        usedStations_.insertLast(used);
    }

    private const MapCellTimer@ FindUsedStation(int x, int z) const
    {
        for (uint i = 0; i < usedStations_.length(); ++i) {
            if (usedStations_[i].x == x && usedStations_[i].z == z) {
                return usedStations_[i];
            }
        }
        return null;
    }

    // 描く範囲から十分に下がった駅は忘れる
    private void ForgetUsedStationsBefore(int startX)
    {
        for (int i = int(usedStations_.length()) - 1; i >= 0; --i) {
            if (usedStations_[i].x < 0 || usedStations_[i].x + viewDistanceX < startX) {
                usedStations_.removeAt(i);
            }
        }
    }

    // カメラが近いほど 1（起き出す）
    private float GetStationApproach(int x) const
    {
        if (stationWakeRange <= 0.0f) {
            return 0.0f;
        }
        return Clamp(1.0f - abs(float(x) - cameraGridX_) / stationWakeRange, 0.0f, 1.0f);
    }

    // 近づくほど速い波へ重心を移す（どちらの波も鳴らしっぱなしで、混ぜる量だけを動かす）
    private float GetStationWave(int x, int z, float approach, float phaseOffset) const
    {
        const float cellPhase = HashCell01(x, z) * kTau;
        const float time = Time::TimeSinceStartup();
        const float slowWeight = 1.0f - approach * 0.5f;
        const float fastWeight = approach;
        return (sin(time * stationIdleSpeed * kTau + cellPhase + phaseOffset) * slowWeight
            + sin(time * stationWakeSpeed * kTau + cellPhase * 1.7f + phaseOffset) * fastWeight)
            / (slowWeight + fastWeight);
    }

    // まだ使っていない駅は呼吸する（縦に伸びたら横は縮める）
    private Vector3 ApplyStationIdle(int x, int z, const Vector3 &in baseScale) const
    {
        Vector3 scale = baseScale;
        if (stationIdleBreath <= 0.0f && stationWakeBreath <= 0.0f) {
            return scale;
        }
        if (FindUsedStation(x, z) !is null) {
            return scale;
        }
        const float approach = GetStationApproach(x);
        const float wave = GetStationWave(x, z, approach, 0.0f);
        const float breath = wave * (stationIdleBreath + (stationWakeBreath - stationIdleBreath) * approach);
        scale.y *= 1.0f + breath;
        scale.x *= 1.0f - breath * 0.6f;
        scale.z *= 1.0f - breath * 0.6f;
        return scale;
    }

    // 使い終わった駅は、ポップと同じ長さで少し暗く冷めた色へ落とす
    private Vector4 GetStationTint(int x, int z) const
    {
        const MapCellTimer@ used = FindUsedStation(x, z);
        if (used is null || stationUsedTint >= 1.0f) {
            return Vector4(1.0f, 1.0f, 1.0f, 1.0f);
        }
        const float progress = Clamp(used.elapsed / stationPopDuration, 0.0f, 1.0f);
        const float tint = 1.0f + (stationUsedTint - 1.0f) * progress;
        return Vector4(tint, tint, Min(1.0f, tint * 1.08f), 1.0f);
    }

    // 屋根の上で跳ねて待つサル。連結したら列車側へ飛び降り、着く前に小さくなって消える
    private void UpdateStationMonkey(int x, int z, MapCellView@ cell, float roofHeight, float modelScale)
    {
        if (cell.stationMonkey is null) {
            return;
        }
        const MapCellTimer@ used = FindUsedStation(x, z);
        const float leap = (used !is null) ? Clamp(used.elapsed / stationPopDuration, 0.0f, 1.0f) : 0.0f;
        if (used !is null && leap >= 1.0f) {
            cell.stationMonkey.object.active = false;
            return;
        }
        cell.stationMonkey.object.active = true;

        const float approach = GetStationApproach(x);
        const float wave = GetStationWave(x, z, approach, kPi * 0.5f);
        const float bounce = abs(wave);
        const float hop = bounce * stationMonkeyHop * gridSize * (0.35f + 0.65f * approach);

        const float monkeyScale = modelScale * stationMonkeyScale;
        Vector3 scale(monkeyScale, monkeyScale, monkeyScale);
        const float squash = (1.0f - bounce) * 0.12f;
        scale.y *= 1.0f - squash;
        scale.x *= 1.0f + squash * 0.5f;
        scale.z *= 1.0f + squash * 0.5f;

        float offsetY = hop;
        float offsetZ = 0.0f;
        if (leap > 0.0f) {
            // 列車は -Z 側。弧を描いて前下へ降りる
            offsetZ = -stationMonkeyLeap * gridSize * leap;
            offsetY += (sin(leap * kPi) * 0.35f - leap * 1.1f) * gridSize;
            const float shrink = 1.0f - leap * leap;
            scale *= shrink;
        }
        cell.stationMonkey.Place(Vector3(x * gridSize, roofHeight + offsetY, z * gridSize + offsetZ),
            Vector3(0, 0, 0), scale);
    }

    // ---------------------------------------------------------------- バナナの木

    // 常に風で揺れる。位相はマスから作る（全部が同じ拍だと地面ごと揺れて見える）
    private void ApplyBananaTreeSway(int x, int z, TreePose@ pose) const
    {
        if (bananaTreeSwayAngle <= 0.0f && bananaTreeSwayLean <= 0.0f
            && bananaTreeSwayYaw <= 0.0f && bananaTreeSwayBreath <= 0.0f) {
            return;
        }
        const float cellPhase = HashCell01(x, z) * kTau;
        const float time = Time::TimeSinceStartup();

        const float windLength = sqrt(bananaTreeWindDirX * bananaTreeWindDirX + bananaTreeWindDirZ * bananaTreeWindDirZ);
        if (windLength > 1.0e-4f) {
            const float windX = bananaTreeWindDirX / windLength;
            const float windZ = bananaTreeWindDirZ / windLength;
            // 揺れの中心を風下へ倒しておく
            const float lean = SwayWave(time, cellPhase, 0.0f) * bananaTreeSwayAngle + bananaTreeSwayLean;
            pose.rotation.x += windZ * lean;
            pose.rotation.z += -windX * lean;
        }
        // 幹を軸に回してシルエットを動かす
        pose.rotation.y += SwayWave(time, cellPhase, kPi * 0.5f) * bananaTreeSwayYaw;
        // 葉の開閉（横へ広げたぶん少し縦を縮める）
        const float breath = SwayWave(time, cellPhase, kPi * 1.25f) * bananaTreeSwayBreath;
        pose.scale.x *= 1.0f + breath;
        pose.scale.z *= 1.0f + breath;
        pose.scale.y *= 1.0f - breath;
    }

    // 割り切れない周期を 2 本重ねた -1〜1 の波
    private float SwayWave(float time, float cellPhase, float phaseOffset) const
    {
        const float wave = sin(time * bananaTreeSwaySpeed * kTau + cellPhase + phaseOffset)
            + sin(time * bananaTreeSwaySubSpeed * kTau + cellPhase * 1.7f + phaseOffset) * bananaTreeSwaySubRate;
        return wave / (1.0f + bananaTreeSwaySubRate);
    }

    // 取られた向きへ大きくしなり、跳ね返りながら収まる（減衰する正弦波 1.5 周期）
    private void ApplyBananaTreeShake(int x, int z, TreePose@ pose) const
    {
        for (uint i = 0; i < treeShakes_.length(); ++i) {
            const MapCellTimer@ shake = treeShakes_[i];
            if (shake.x != x || shake.z != z) {
                continue;
            }
            const float progress = Clamp(shake.elapsed / bananaTreeShakeDuration, 0.0f, 1.0f);
            const float decay = (1.0f - progress) * (1.0f - progress);
            const float wave = sin(progress * 3.0f * kPi) * decay;
            const float lean = wave * bananaTreeShakeLean;
            pose.rotation.x += shake.towardZ * lean;
            pose.rotation.z += -shake.towardX * lean;
            const float squash = abs(wave) * bananaTreeShakeSquash;
            pose.scale.y *= 1.0f - squash;
            pose.scale.x *= 1.0f + squash * 0.5f;
            pose.scale.z *= 1.0f + squash * 0.5f;
            return;
        }
    }

    // ---------------------------------------------------------------- 距離の目盛り

    // 5m ごとに、手前の地形の外へ寝かせた「|」と距離を置く
    private void UpdateDistanceMarkers(int startX, int endX)
    {
        const float startMeters = float(startX) * gridSize;
        const float endMeters = float(endX) * gridSize;
        const int firstMarker = Max(1, int(ceil(startMeters / float(distanceMarkerInterval))));
        uint used = 0;
        for (int index = firstMarker; float(index * distanceMarkerInterval) < endMeters; ++index) {
            if (used == markers_.length()) {
                PooledModel@ created = markerPool_.Acquire();
                if (created is null) {
                    break;
                }
                markers_.insertLast(created);
            }
            PooledModel@ marker = markers_[used];
            ++used;
            const int meters = index * distanceMarkerInterval;
            marker.object.active = true;
            Text3DRenderer@ text = marker.object.text3DRenderer;
            const string label = "|\n" + meters + "m";
            if (text.text != label) {
                text.text = label;
            }
            marker.Place(Vector3(float(meters), BlockLayout::GetSurfaceHeight(gridSize) + 0.015f * gridSize, -0.75f * gridSize),
                Vector3(kPi * 0.5f, 0, 0), Vector3(1, 1, 1));
        }
        for (uint i = used; i < markers_.length(); ++i) {
            markers_[i].object.active = false;
        }
    }

    // 距離目盛りの色を塗り分ける（指定した距離の目盛りだけ別の色・ほかは plainColor）
    void PaintDistanceMarkers(int highlightMeters, const Vector4 &in highlightColor, const Vector4 &in plainColor)
    {
        const string highlight = "|\n" + highlightMeters + "m";
        for (uint i = 0; i < markers_.length(); ++i) {
            if (!markers_[i].object.active) {
                continue;
            }
            Text3DRenderer@ text = markers_[i].object.text3DRenderer;
            text.color = text.text == highlight ? highlightColor : plainColor;
        }
    }

    // ---------------------------------------------------------------- 補助

    private void AdvanceTimers(array<MapCellTimer@>@ timers, float deltaTime, float duration)
    {
        const float step = Max(deltaTime, 0.0f);
        for (int i = int(timers.length()) - 1; i >= 0; --i) {
            timers[i].elapsed += step;
            if (timers[i].elapsed >= duration) {
                timers.removeAt(i);
            }
        }
    }
}
