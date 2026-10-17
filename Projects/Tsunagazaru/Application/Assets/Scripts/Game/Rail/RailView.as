using namespace CoreEngine;

// レールの形
enum RailKind
{
    None = -1,
    Straight = 0,
    Left = 1,
    Right = 2
}

// 経路の 1 本ぶんに置いている物
class RailSlot
{
    int kind = RailKind::None;
    PooledModel@ model;
    PooledModel@ bridge;
}

// レールを描く。確定・未確定を 1 本の経路として、前後の向きから直線・左曲がり・右曲がりを選ぶ。
// 置いた直後は跳ね、バナナの木の隣なら 1 回転する。トロッコが通って確定したら音を鳴らす
[DisplayName("レールの表示")]
class RailView : ScriptComponent
{
    [DisplayName("マスの大きさ"), Range(0.01, 20)]
    float gridSize = 1.0f;

    [DisplayName("描く範囲（左右の列数）"), Range(1, 200)]
    int viewDistanceX = 20;

    [DisplayName("置いたときに跳ねる高さ"), Range(0, 10)]
    float jumpHeight = 0.8f;

    [DisplayName("置いたときに跳ねる時間"), Range(0.01, 10)]
    float jumpDuration = 0.35f;

    [DisplayName("バナナの木の隣で回る回数"), Range(0, 4)]
    float bananaBuildRotationTurns = 1.0f;

    [DisplayName("確定の音の時間差"), Range(0, 5)]
    float staggerInterval = 0.06f;

    [DisplayName("確定の音の音量"), Range(0, 1)]
    float seVolume = 0.45f;

    [DisplayName("確定の音の基準の音程"), Range(0.01, 4)]
    float seBasePitch = 0.9f;

    [DisplayName("確定の音の音程の増分"), Range(0, 4)]
    float sePitchStep = 0.05f;

    [DisplayName("確定の音の最大の音程"), Range(0.01, 4)]
    float seMaxPitch = 1.35f;

    [DisplayName("確定の音")]
    string railBuildSePath = "Application/Assets/Sounds/SE/rail_build.mp3";

    [DisplayName("駅の前のレールの確定の音")]
    string stationRailBuildSePath = "Application/Assets/Sounds/SE/build_station.mp3";

    private RailPath@ path_;
    private MapGenerator@ map_;
    private ModelPool@ railPool_;
    private ModelPool@ railLeftPool_;
    private ModelPool@ railRightPool_;
    private ModelPool@ bridgePool_;
    private array<RailSlot@> slots_;
    private array<float> jumpTimes_;
    private array<float> soundTimes_;
    private array<float> soundPitches_;
    private array<int> stationRailKeys_;
    private array<PooledModel@> stationRailModels_;

    // GameDirector が最初に呼ぶ
    void Setup(RailPath@ path, MapGenerator@ map)
    {
        @path_ = path;
        @map_ = map;
        const string root = "Application/Assets/Prefabs/Game/";
        @railPool_ = ModelPool(owner, root + "Rail.prefab", "Rail_");
        @railLeftPool_ = ModelPool(owner, root + "RailLeft.prefab", "RailLeft_");
        @railRightPool_ = ModelPool(owner, root + "RailRight.prefab", "RailRight_");
        @bridgePool_ = ModelPool(owner, root + "Bridge.prefab", "Bridge_");

        // 始めからあるレールは跳ねさせず、音も鳴らさない
        jumpTimes_.resize(path_.GetRailCount());
        for (uint i = 0; i < jumpTimes_.length(); ++i) {
            jumpTimes_[i] = jumpDuration;
        }
        soundTimes_.resize(path_.GetConfirmedCount());
        soundPitches_.resize(path_.GetConfirmedCount());
        for (uint i = 0; i < soundTimes_.length(); ++i) {
            soundTimes_[i] = jumpDuration;
            soundPitches_[i] = seBasePitch;
        }
    }

    // GameDirector が Update から呼ぶ
    void Tick(float deltaTime)
    {
        if (path_ is null || map_ is null) {
            return;
        }
        UpdateJumpTimes(deltaTime);
        UpdateConfirmationSounds(deltaTime);
        DrawRails();
    }

    private void UpdateJumpTimes(float deltaTime)
    {
        // 置いた順の添字はトロッコが通って確定しても変わらない。増えた分は 0 から、減った分は末尾ごと捨てる
        const uint count = uint(path_.GetRailCount());
        const uint previous = jumpTimes_.length();
        jumpTimes_.resize(count);
        for (uint i = previous; i < count; ++i) {
            jumpTimes_[i] = 0.0f;
        }
        const float step = Max(deltaTime, 0.0f);
        for (uint i = 0; i < count; ++i) {
            jumpTimes_[i] = Min(jumpTimes_[i] + step, jumpDuration);
        }
    }

    private float GetJumpOffset(int index) const
    {
        if (index >= int(jumpTimes_.length()) || jumpTimes_[index] >= jumpDuration) {
            return 0.0f;
        }
        const float progress = Clamp(jumpTimes_[index] / jumpDuration, 0.0f, 1.0f);
        return sin(progress * kPi) * jumpHeight;
    }

    // ジャンプと一緒に回り始め、最後は元の向きへ滑らかに戻す
    private float GetBananaBuildRotation(int index, int gridX, int gridZ) const
    {
        if (index >= int(jumpTimes_.length()) || bananaBuildRotationTurns <= 0.0f
            || jumpTimes_[index] >= jumpDuration || !IsAdjacentToBananaTree(map_, gridX, gridZ)) {
            return 0.0f;
        }
        const float progress = Clamp(jumpTimes_[index] / jumpDuration, 0.0f, 1.0f);
        const float eased = progress * progress * (3.0f - 2.0f * progress);
        return eased * bananaBuildRotationTurns * kTau;
    }

    // 同じフレームに何本も確定したら（駅に着いたとき）、少しずつずらして音程を上げながら鳴らす
    private void UpdateConfirmationSounds(float deltaTime)
    {
        const uint confirmed = uint(path_.GetConfirmedCount());
        if (soundTimes_.length() > confirmed) {
            soundTimes_.resize(confirmed);
            soundPitches_.resize(confirmed);
        }
        const uint firstNew = soundTimes_.length();
        for (uint i = firstNew; i < confirmed; ++i) {
            soundTimes_.insertLast(-staggerInterval * float(i - firstNew));
            soundPitches_.insertLast(Min(seBasePitch + sePitchStep * float(i - firstNew), seMaxPitch));
        }

        const float step = Max(deltaTime, 0.0f);
        for (uint i = 0; i < soundTimes_.length(); ++i) {
            if (soundTimes_[i] >= jumpDuration) {
                continue;
            }
            const float previousTime = soundTimes_[i];
            soundTimes_[i] = Min(soundTimes_[i] + step, jumpDuration);
            if (previousTime <= 0.0f && soundTimes_[i] > 0.0f) {
                const int key = path_.GetConfirmed(int(i));
                const bool isStationRail = map_.IsStationRailCell(CellX(key), CellZ(key));
                PlayParams params;
                params.bus = AudioBus::SE;
                params.volume = seVolume;
                params.pitch = soundPitches_[i];
                Audio::PlayOneShot(isStationRail ? stationRailBuildSePath : railBuildSePath, params);
            }
        }
    }

    private void DrawRails()
    {
        const float centerGridX = Scene::GetGameCamera().position.x / gridSize;
        const int minVisibleX = Max(0, int(floor(centerGridX)) - viewDistanceX);
        const int maxVisibleX = int(ceil(centerGridX)) + viewDistanceX;

        const float modelScale = BlockLayout::GetScale(gridSize);
        const Vector3 scale(modelScale, modelScale, modelScale);
        const float railHeight = BlockLayout::GetSurfaceHeight(gridSize);
        const float bridgeHeight = BlockLayout::GetBridgeHeight(gridSize);

        DrawStationRails(minVisibleX, maxVisibleX, railHeight, scale);

        // 取り消して減った分を返す
        const int count = path_.GetRailCount();
        while (int(slots_.length()) > count) {
            ReleaseSlot(slots_[slots_.length() - 1]);
            slots_.removeLast();
        }
        while (int(slots_.length()) < count) {
            slots_.insertLast(RailSlot());
        }

        for (int i = 0; i < count; ++i) {
            RailSlot@ slot = slots_[i];
            const int current = path_.GetRail(i);
            const int x = CellX(current);
            const int z = CellZ(current);
            if (x < minVisibleX || x > maxVisibleX) {
                ReleaseSlot(slot);
                continue;
            }

            const bool hasPrevious = i > 0;
            const bool hasNext = i + 1 < count;
            // 始点のレールで接続先がまだ無いときは +X を向ける
            int inX = 1;
            int inZ = 0;
            int outX = 1;
            int outZ = 0;
            if (hasPrevious) {
                const int previous = path_.GetRail(i - 1);
                inX = x - CellX(previous);
                inZ = z - CellZ(previous);
            }
            if (hasNext) {
                const int next = path_.GetRail(i + 1);
                outX = CellX(next) - x;
                outZ = CellZ(next) - z;
            }
            if (!hasPrevious && hasNext) {
                inX = outX;
                inZ = outZ;
            } else if (hasPrevious && !hasNext) {
                outX = inX;
                outZ = inZ;
            }

            const float bananaRotation = GetBananaBuildRotation(i, x, z);
            const Vector3 position(x * gridSize, railHeight + GetJumpOffset(i), z * gridSize);

            // 水の上なら、地面と同じ高さへ橋を足す
            if (map_.GetChip(x, z) == MapChip::Water) {
                if (slot.bridge is null) {
                    @slot.bridge = bridgePool_.Acquire();
                }
                slot.bridge.Place(Vector3(x * gridSize, bridgeHeight, z * gridSize),
                    Vector3(0, YawFromDirection(outX, outZ), 0), scale);
            } else if (slot.bridge !is null) {
                bridgePool_.Release(slot.bridge);
                @slot.bridge = null;
            }

            // XZ 平面の外積。正なら左折、負なら右折
            const int turn = inX * outZ - inZ * outX;
            int kind = RailKind::Straight;
            float yaw = YawFromDirection(outX, outZ);
            if (hasPrevious && hasNext && turn > 0) {
                kind = RailKind::Left;
                yaw = YawFromDirection(inX, inZ);
            } else if (hasPrevious && hasNext && turn < 0) {
                kind = RailKind::Right;
                yaw = YawFromDirection(inX, inZ);
            }
            if (slot.kind != kind || slot.model is null) {
                if (slot.model !is null) {
                    PoolOf(slot.kind).Release(slot.model);
                }
                @slot.model = PoolOf(kind).Acquire();
                slot.kind = kind;
            }
            slot.model.Place(position, Vector3(0, yaw + bananaRotation, 0), scale);
        }
    }

    // まだ経路へつながっていない駅の前のレールは、横向きの直線で出しておく
    private void DrawStationRails(int minVisibleX, int maxVisibleX, float railHeight, const Vector3 &in scale)
    {
        array<int> wanted;
        const int endX = Min(map_.GetSizeX(), maxVisibleX + 1);
        for (int x = minVisibleX; x < endX; ++x) {
            for (int z = 0; z < map_.mapSizeZ; ++z) {
                if (map_.IsStationRailCell(x, z) && !path_.HasRail(x, z)) {
                    wanted.insertLast(CellKey(x, z));
                }
            }
        }
        for (int i = int(stationRailKeys_.length()) - 1; i >= 0; --i) {
            if (wanted.find(stationRailKeys_[i]) < 0) {
                railPool_.Release(stationRailModels_[i]);
                stationRailKeys_.removeAt(i);
                stationRailModels_.removeAt(i);
            }
        }
        for (uint i = 0; i < wanted.length(); ++i) {
            int index = stationRailKeys_.find(wanted[i]);
            if (index < 0) {
                stationRailKeys_.insertLast(wanted[i]);
                stationRailModels_.insertLast(railPool_.Acquire());
                index = int(stationRailKeys_.length()) - 1;
            }
            stationRailModels_[index].Place(
                Vector3(CellX(wanted[i]) * gridSize, railHeight, CellZ(wanted[i]) * gridSize),
                Vector3(0, kPi * 0.5f, 0), scale);
        }
    }

    private void ReleaseSlot(RailSlot@ slot)
    {
        if (slot.model !is null) {
            PoolOf(slot.kind).Release(slot.model);
            @slot.model = null;
        }
        if (slot.bridge !is null) {
            bridgePool_.Release(slot.bridge);
            @slot.bridge = null;
        }
        slot.kind = RailKind::None;
    }

    private ModelPool@ PoolOf(int kind)
    {
        if (kind == RailKind::Left) {
            return railLeftPool_;
        }
        if (kind == RailKind::Right) {
            return railRightPool_;
        }
        return railPool_;
    }

    // レールのモデルは +Z が 0 ラジアン
    private float YawFromDirection(int directionX, int directionZ) const
    {
        return atan2(float(directionX), float(directionZ));
    }
}

// 上下左右のどれかがバナナの木か
bool IsAdjacentToBananaTree(const MapGenerator@ map, int gridX, int gridZ)
{
    if (map is null) {
        return false;
    }
    return map.GetChip(gridX + 1, gridZ) == MapChip::BananaTree
        || (gridX > 0 && map.GetChip(gridX - 1, gridZ) == MapChip::BananaTree)
        || map.GetChip(gridX, gridZ + 1) == MapChip::BananaTree
        || (gridZ > 0 && map.GetChip(gridX, gridZ - 1) == MapChip::BananaTree);
}
