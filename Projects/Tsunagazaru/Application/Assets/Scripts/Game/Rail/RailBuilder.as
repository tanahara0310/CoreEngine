using namespace CoreEngine;

// レールを敷くカーソル。上下左右で 1 マス動かすと、そのマスにレールを 1 本敷く。
// 1 つ前のレールへ戻ると取り消し、画面に映らないマス・敷けないマス・スタミナが足りないときは進まない。
// 岩のマスでは、トロッコを止めて石を投げ、当たったら地面にしてから再び走らせる
[DisplayName("レールを敷くカーソル")]
class RailBuilder : ScriptComponent
{
    [DisplayName("マスの大きさ"), Range(0.01, 20)]
    float gridSize = 1.0f;

    [DisplayName("開始のマス X"), Range(0, 500)]
    int initialGridX = 1;

    [DisplayName("開始のマス Z"), Range(0, 99)]
    int initialGridZ = 4;

    [DisplayName("横の入力を優先する")]
    bool horizontalPrioritize = true;

    [DisplayName("長押しで連続し始めるまで（秒）"), Range(0, 5)]
    float buildHoldTime = 0.3f;

    [DisplayName("長押しの連続の間隔（秒）"), Range(0.01, 2)]
    float buildInterval = 0.05f;

    [DisplayName("矢印の高さ"), Range(-20, 20)]
    float height = 1.0f;

    [DisplayName("矢印の脈打ちの基準の倍率"), Range(0, 5)]
    float pulseBaseScale = 0.8f;

    [DisplayName("矢印の脈打ちの振れ幅"), Range(0, 5)]
    float pulseAmplitude = 0.2f;

    [DisplayName("矢印の脈打ちの速さ"), Range(0, 30)]
    float pulseSpeed = 5.0f;

    [DisplayName("矢印の回る速さ"), Range(-30, 30)]
    float rotationSpeed = 2.0f;

    [DisplayName("岩を壊している間の矢印の高さ"), Range(0, 10)]
    float rockCursorHeightOffset = 1.0f;

    [DisplayName("石を投げ始める高さ"), Range(-10, 10)]
    float rockThrowStartHeight = 0.5f;

    [DisplayName("石が当たる高さ"), Range(-10, 10)]
    float rockImpactHeight = 0.7f;

    [DisplayName("画面の端で止める矢印の半径（マス）"), Range(0, 3)]
    float cursorEdgeRadiusRatio = 0.5f;

    [DisplayName("岩を壊したときの揺れの強さ"), Range(0, 3)]
    float rockBreakShakeStrength = 1.0f;

    [DisplayName("岩を壊したときの揺れの長さ（秒）"), Range(0.05, 1.5)]
    float rockBreakShakeDuration = 0.35f;

    [DisplayName("岩を壊したときの揺れの細かさ（Hz）"), Range(1, 60)]
    float rockBreakShakeFrequency = 22.0f;

    [DisplayName("敷いたときの音")]
    string buildSePath = "Application/Assets/Sounds/SE/build.mp3";

    [DisplayName("バナナの木の隣に敷いたときの音")]
    string bananaBuildSePath = "Application/Assets/Sounds/SE/banana_build.mp3";

    [DisplayName("取り消したときの音")]
    string undoSePath = "Application/Assets/Sounds/SE/build_return.mp3";

    [DisplayName("敷けなかったときの音")]
    string failureSePath = "Application/Assets/Sounds/SE/beep.mp3";

    [DisplayName("石を投げたときの音")]
    string rockThrowSePath = "Application/Assets/Sounds/SE/rock_throw.mp3";

    [DisplayName("岩が砕けたときの音")]
    string rockBreakSePath = "Application/Assets/Sounds/SE/rock_break.mp3";

    private RailPath@ path_;
    private MapGenerator@ map_;
    private TrainMovement@ train_;
    private Hunger@ hunger_;
    private RockThrow@ rockThrow_;
    private GameObject@ debris_;
    private Transform@ transform_;
    private int gridX_ = 0;
    private int gridZ_ = 0;
    private float timer_ = 0.0f;
    private float buildPushTimer_ = 0.0f;
    private float buildIntervalTimer_ = 0.0f;
    private bool inputLocked_ = false;
    private bool stopped_ = false;
    private bool isBreakingRock_ = false;
    private bool isCursorAboveRock_ = false;
    private array<int> rockBreakQueue_;
    private TweenCallback@ onStaminaInsufficient_;

    // GameDirector が最初に呼ぶ
    void Setup(RailPath@ path, MapGenerator@ map, TrainMovement@ train, Hunger@ hunger, RockThrow@ rockThrow, GameObject@ debris)
    {
        @path_ = path;
        @map_ = map;
        @train_ = train;
        @hunger_ = hunger;
        @rockThrow_ = rockThrow;
        @debris_ = debris;
        @transform_ = owner.transform;
        gridX_ = initialGridX;
        gridZ_ = initialGridZ;
        SyncTransformToGrid();
    }

    int GetGridX() const { return gridX_; }
    int GetGridZ() const { return gridZ_; }
    bool IsBreakingRock() const { return isBreakingRock_; }

    // 突入の演出の間などは入力だけを止める（矢印は回り続ける）
    void SetInputLocked(bool locked)
    {
        inputLocked_ = locked;
    }

    // 終わりが決まったら止める
    void Stop()
    {
        stopped_ = true;
    }

    // スタミナが足りなかったときに呼ぶ関数（ゲージを光らせるなど）
    void SetOnStaminaInsufficient(TweenCallback@ callback)
    {
        @onStaminaInsufficient_ = callback;
    }

    bool IsStopped() const { return stopped_; }

    // そのマスへ敷くのに要るスタミナ（実際に払う額と同じ式）
    float GetPlacementCost(int x, int z) const
    {
        return hunger_.CalculateActionCost(GetBaseCost(x, z));
    }

    // 優先する向きの 1 マス先へ敷く費用（ゲージの予告に使う）
    float GetNextPlacementCost() const
    {
        const int nextX = gridX_ + (horizontalPrioritize ? 1 : 0);
        const int nextZ = gridZ_ + (horizontalPrioritize ? 0 : 1);
        if (nextX < 0 || nextZ < 0) {
            return 0.0f;
        }
        return hunger_.CalculateActionCost(GetBaseCost(nextX, nextZ));
    }

    // ---------------------------------------------------------------- 毎フレーム

    // GameDirector が Update から呼ぶ
    void Tick(float deltaTime)
    {
        if (stopped_ || path_ is null) {
            return;
        }
        timer_ += deltaTime;
        const float modelScale = BlockLayout::GetScale(gridSize);
        transform_.scale = Vector3(modelScale,
            modelScale * (pulseBaseScale + sin(timer_ * pulseSpeed) * pulseAmplitude), modelScale);
        Vector3 rotation = transform_.rotation;
        rotation.y = timer_ * rotationSpeed;
        transform_.rotation = rotation;

        if (inputLocked_) {
            return;
        }

        // 押した瞬間に 1 回、押し続けたら一定時間の後に一定の間隔で繰り返す
        bool isContinuousBuild = false;
        const bool isMovePressed = Input::IsActionPressed(InputAction::MoveRight)
            || Input::IsActionPressed(InputAction::MoveLeft)
            || Input::IsActionPressed(InputAction::MoveForward)
            || Input::IsActionPressed(InputAction::MoveBack);
        if (isMovePressed) {
            buildPushTimer_ += deltaTime;
            if (buildPushTimer_ >= buildHoldTime) {
                if (buildIntervalTimer_ <= 0.0f) {
                    isContinuousBuild = true;
                    buildIntervalTimer_ = buildInterval;
                } else {
                    buildIntervalTimer_ -= deltaTime;
                }
            }
        } else {
            buildPushTimer_ = 0.0f;
            buildIntervalTimer_ = 0.0f;
        }

        const bool isMoveTriggered = Input::IsActionTriggered(InputAction::MoveRight)
            || Input::IsActionTriggered(InputAction::MoveLeft)
            || Input::IsActionTriggered(InputAction::MoveForward)
            || Input::IsActionTriggered(InputAction::MoveBack);
        if (!isMoveTriggered && !isContinuousBuild) {
            return;
        }

        const bool usePressed = !isMoveTriggered;
        int moveX = ActionValue(InputAction::MoveRight, usePressed) - ActionValue(InputAction::MoveLeft, usePressed);
        int moveZ = ActionValue(InputAction::MoveForward, usePressed) - ActionValue(InputAction::MoveBack, usePressed);
        if (moveX == 0 && moveZ == 0) {
            return;
        }
        if (horizontalPrioritize) {
            if (moveX != 0) {
                moveZ = 0;
            }
        } else if (moveZ != 0) {
            moveX = 0;
        }
        TryMove(gridX_ + moveX, gridZ_ + moveZ);
    }

    // GameDirector が LateUpdate から呼ぶ（トロッコが動いた後に高さを合わせる）
    void TickLate()
    {
        SyncTransformToGrid();
    }

    // ---------------------------------------------------------------- 敷く

    private void TryMove(int nextX, int nextZ)
    {
        if (nextX < 0 || nextZ < 0 || nextZ >= path_.mapSizeZ) {
            return;
        }

        // 1 つ前のレールへ戻るときは、移動ではなく最後のレールの取り消し
        if (path_.GetPendingCount() > 0) {
            const int previousRail = path_.GetPendingCount() >= 2
                ? path_.GetPending(path_.GetPendingCount() - 2)
                : path_.GetLastConfirmed();
            if (nextX == CellX(previousRail) && nextZ == CellZ(previousRail)) {
                TryUndoLastRail();
                return;
            }
        }

        // 既にレールがあるマスへは進まない
        if (path_.HasRail(nextX, nextZ)) {
            return;
        }
        // 画面に映らないマスへは進まない（マップを伸ばす前に断る）
        if (!IsCellInsideScreen(nextX, nextZ)) {
            return;
        }

        map_.CreateToX(nextX + 1);
        const int chip = map_.GetChip(nextX, nextZ);
        const bool isRock = chip == MapChip::Resource;
        if (!map_.CanConnectRail(gridX_, gridZ_, nextX, nextZ)) {
            return;
        }

        // 駅の前のレールは経路へつなぐだけなので費用 0
        const float railCost = map_.IsStationRailCell(nextX, nextZ) ? 0.0f : Max(0.0f, hunger_.railCost);
        const float staminaCost = hunger_.CalculateActionCost(GetBaseCost(nextX, nextZ));
        // スタミナを先に払う。足りなければ失敗の音を鳴らして何も敷かない
        if (!hunger_.TryConsumeStamina(staminaCost)) {
            if (onStaminaInsufficient_ !is null) {
                onStaminaInsufficient_();
            }
            PlaySe(failureSePath, 1.0f, 1.0f);
            return;
        }

        // 岩は壊す分を返さない（取り消しで返るのはレールの分だけ）
        const float refundableCost = isRock ? hunger_.CalculateActionCost(railCost) : staminaCost;
        if (!path_.PlaceRail(nextX, nextZ, refundableCost)) {
            hunger_.AddStamina(staminaCost);
            return;
        }

        if (IsAdjacentToBananaTree(map_, nextX, nextZ)) {
            PlaySe(bananaBuildSePath, 1.0f, Random::Range(1.0f, 1.3f));
        } else {
            // 0.5〜1.48 の 50 段
            PlaySe(buildSePath, 0.5f, 0.5f + float(Random::Range(0, 49)) / 50.0f);
        }

        gridX_ = nextX;
        gridZ_ = nextZ;

        if (isRock) {
            const bool wasBreaking = isBreakingRock_;
            isBreakingRock_ = true;
            isCursorAboveRock_ = true;
            rockBreakQueue_.insertLast(CellKey(gridX_, gridZ_));
            if (!wasBreaking) {
                train_.SetRockBreakPaused(true);
            }
            SyncTransformToGrid();
            if (!wasBreaking) {
                StartNextRockThrow();
            }
            return;
        }

        isCursorAboveRock_ = false;
        SyncTransformToGrid();
    }

    private bool TryUndoLastRail()
    {
        if (isBreakingRock_) {
            return false;
        }
        RailUndoResult@ undo = path_.UndoLastRailPlacement();
        if (!undo.succeeded) {
            return false;
        }
        hunger_.AddStamina(undo.refundAmount);
        gridX_ = CellX(undo.builderCell);
        gridZ_ = CellZ(undo.builderCell);
        SyncTransformToGrid();
        PlaySe(undoSePath, 1.0f, 1.0f);
        return true;
    }

    private float GetBaseCost(int x, int z) const
    {
        float baseCost = map_.IsStationRailCell(x, z) ? 0.0f : Max(0.0f, hunger_.railCost);
        const int chip = map_.GetChip(x, z);
        if (chip == MapChip::Water) {
            baseCost += Max(0.0f, hunger_.bridgeCost);
        } else if (chip == MapChip::Resource) {
            baseCost += Max(0.0f, hunger_.rockCost);
        }
        return baseCost;
    }

    // ---------------------------------------------------------------- 岩

    private void StartNextRockThrow()
    {
        if (rockBreakQueue_.length() == 0) {
            return;
        }
        const int request = rockBreakQueue_[0];
        Vector3 throwStart = train_.GetWorldPosition();
        throwStart.y += rockThrowStartHeight;
        const Vector3 impact(CellX(request) * gridSize, rockImpactHeight, CellZ(request) * gridSize);

        train_.PlayRockThrowJump();
        if (!rockThrow_.Play(throwStart, impact, RockImpactCallback(this.CompleteRockBreak))) {
            CompleteRockBreak();
            return;
        }
        PlaySe(rockThrowSePath, 1.0f, Random::Range(1.0f, 1.3f));
    }

    private void CompleteRockBreak()
    {
        if (!isBreakingRock_ || rockBreakQueue_.length() == 0) {
            return;
        }
        const int completed = rockBreakQueue_[0];
        rockBreakQueue_.removeAt(0);
        map_.SetChip(CellX(completed), CellZ(completed), MapChip::Ground);
        GameResult::AddBrokenRockCount();
        PlaySe(rockBreakSePath, 1.0f, Random::Range(1.0f, 1.3f));

        // 強い一撃のプリセットを土台に、強さ・長さ・細かさだけ差し替える
        if (rockBreakShakeStrength > 0.0f) {
            CameraShakeParams shake = CameraShakePresets::HeavyHit();
            shake.positionAmplitude *= rockBreakShakeStrength;
            shake.rotationAmplitude *= rockBreakShakeStrength;
            shake.duration = rockBreakShakeDuration;
            shake.frequency = rockBreakShakeFrequency;
            CameraShake::Play(shake);
        }

        // 同じ瞬間に、壊したマスの周りへ破片を散らす（前の破片は消さずに落ち切らせる）
        if (debris_ !is null && debris_.isAlive) {
            debris_.transform.position = Vector3(CellX(completed) * gridSize, rockImpactHeight + 0.45f, CellZ(completed) * gridSize);
            debris_.transform.UpdateMatrix();
            debris_.particleSystem.Emit(8);
        }

        isCursorAboveRock_ = false;
        if (rockBreakQueue_.length() > 0) {
            StartNextRockThrow();
            return;
        }
        isBreakingRock_ = false;
        train_.SetRockBreakPaused(false);
        SyncTransformToGrid();
    }

    // ---------------------------------------------------------------- 位置

    private void SyncTransformToGrid()
    {
        Vector3 position = transform_.position;
        position.x = gridX_ * gridSize;
        position.z = gridZ_ * gridSize;

        float cursorHeight = height;
        const int chip = map_.GetChip(gridX_, gridZ_);
        if (chip == MapChip::Station) {
            // 駅は 1 ブロックより背が高いので、屋根の上にも普段の浮き幅を取る
            const float stationTop = BlockLayout::GetSurfaceHeight(gridSize)
                + BlockLayout::kStationModelHeight * BlockLayout::GetScale(gridSize);
            const float clearance = Max(0.0f, height - BlockLayout::GetSurfaceHeight(gridSize));
            cursorHeight = Max(height + gridSize, stationTop + clearance);
        }
        if (chip == MapChip::Resource || isCursorAboveRock_) {
            cursorHeight = Max(cursorHeight, height + Max(gridSize, rockCursorHeightOffset));
        }
        if (train_ !is null) {
            // 駅と車両が同じマスにあっても足さず、要る高さの大きいほう
            cursorHeight = Max(cursorHeight, height + train_.GetCursorHeightOffsetAt(position.x, position.z));
        }
        position.y = cursorHeight;
        transform_.position = position;
    }

    // ---------------------------------------------------------------- 画面の外

    // 隣のマスへ動いたときに、矢印が画面の内側に収まるか。高さは今の矢印と同じとみなす
    private bool IsCellInsideScreen(int gridX, int gridZ) const
    {
        return IsInsideScreen(Vector3(gridX * gridSize, transform_.position.y, gridZ * gridSize));
    }

    // カメラは列車とカーソルの中点を写すので、カーソルが先へ行くほど列車は左へ押し出される。
    // 矢印の画面上の半径は、その場のカメラの右と上へ半径ぶん動かした点で測る
    private bool IsInsideScreen(const Vector3 &in worldPosition) const
    {
        GameCamera@ camera = Scene::GetGameCamera();
        if (!camera.exists) {
            return true;
        }
        const Vector3 point = camera.WorldToScreenPoint(worldPosition);
        // カメラの後ろ
        if (point.z <= 1.0e-5f) {
            return false;
        }
        const float halfWidth = 960.0f;
        const float halfHeight = 540.0f;
        const float ndcX = point.x / halfWidth - 1.0f;
        const float ndcY = 1.0f - point.y / halfHeight;

        const float radius = gridSize * Max(0.0f, cursorEdgeRadiusRatio);
        float extentX = 0.0f;
        float extentY = 0.0f;
        if (radius > 0.0f) {
            const Vector3 right = camera.WorldToScreenPoint(worldPosition + camera.right * radius);
            if (right.z > 1.0e-5f) {
                extentX = abs(right.x - point.x) / halfWidth;
            }
            const Vector3 up = camera.WorldToScreenPoint(worldPosition + camera.up * radius);
            if (up.z > 1.0e-5f) {
                extentY = abs(up.y - point.y) / halfHeight;
            }
        }
        return abs(ndcX) + extentX <= 1.0f && abs(ndcY) + extentY <= 1.0f;
    }

    // ---------------------------------------------------------------- 補助

    private int ActionValue(InputAction action, bool usePressed) const
    {
        const bool on = usePressed ? Input::IsActionPressed(action) : Input::IsActionTriggered(action);
        return on ? 1 : 0;
    }

    private void PlaySe(const string &in path, float volume, float pitch)
    {
        PlayParams params;
        params.bus = AudioBus::SE;
        params.volume = volume;
        params.pitch = pitch;
        Audio::PlayOneShot(path, params);
    }
}
