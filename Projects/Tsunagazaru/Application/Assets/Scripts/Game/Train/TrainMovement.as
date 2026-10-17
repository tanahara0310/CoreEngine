using namespace CoreEngine;

// 連結した車両 1 両
class TrainCarriage
{
    GameObject@ object;
    Transform@ transform;
    float yaw = 0.0f;
    // 出てくるときの弾み（縦に伸び上がり、潰れて、跳ね戻る）を掛ける拡縮
    Vector3 popScale = Vector3(1.0f, 1.0f, 1.0f);

    void SetPopScale(const Vector3 &in value)
    {
        popScale = value;
    }
}

// サル 1 匹と、飛んでいくときの軌跡
class TrainMonkey
{
    GameObject@ object;
    GameObject@ trail;

    // 飛んでいる間、軌跡の出どころをサルへ付いていかせる（出た粒はその場に残る）
    void OnLaunchUpdate(float progress)
    {
        if (trail !is null && object !is null && object.isAlive) {
            trail.transform.position = object.transform.position;
        }
    }

    // 生きている粒は残し、以降の放出だけ止める
    void OnLaunchComplete()
    {
        if (trail !is null && trail.isAlive) {
            trail.particleSystem.Stop();
        }
    }
}

// 車両が連結されたときに呼ぶ関数（連結した車両の数）
funcdef void CarriageAddedCallback(int carriageCount);

// トロッコの動き。未確定のレールが決まった数たまったら発車し、マスの中心から次のマスの中心へ進む。
// 速さは敷いたレールの数とサルの数で上がり、駅に入ると最低の速さへ戻る。レールの先に追いつくとゲームオーバー
[DisplayName("トロッコの動き")]
class TrainMovement : ScriptComponent
{
    [DisplayName("マスの大きさ"), Range(0.01, 20)]
    float gridSize = 1.0f;

    [DisplayName("最初の速さ（マス/秒）"), Range(0, 20)]
    float initialMoveSpeed = 1.5f;

    [DisplayName("開始のマス X"), Range(0, 500)]
    int initialGridX = 1;

    [DisplayName("開始のマス Z"), Range(0, 99)]
    int initialGridZ = 4;

    [DisplayName("レール 1 本あたりの最低速度の増加"), Range(0, 10)]
    float minimumSpeedIncreasePerRail = 0.0045f;

    [DisplayName("サル 1 匹あたりの最低速度の補正率"), Range(0, 1)]
    float minimumSpeedMonkeyBonusRate = 0.04f;

    [DisplayName("加速度（速度/秒）"), Range(0, 20)]
    float acceleration = 0.1f;

    [DisplayName("サル 1 匹あたりの加速度の補正率"), Range(0, 1)]
    float accelerationMonkeyBonusRate = 0.08f;

    [DisplayName("最高速度"), Range(0.01, 100)]
    float maximumMoveSpeed = 32.0f;

    [DisplayName("曲がり角で向きを補間する幅（マス）"), Range(0, 0.5)]
    float turnBlendRatio = 0.35f;

    [DisplayName("投石で跳ねる高さ"), Range(0, 10)]
    float rockThrowJumpHeight = 0.6f;

    [DisplayName("投石で跳ねる時間"), Range(0, 5)]
    float rockThrowJumpDuration = 0.35f;

    [DisplayName("発車に要る未確定のレールの数"), Range(1, 100)]
    int requiredRailCount = 5;

    [DisplayName("先頭のサルのオブジェクトの名前")]
    string headMonkeyName = "Monkey";

    [DisplayName("出てくる弾み：最初の倍率"), Range(0, 1)]
    float spawnScale = 0.05f;

    [DisplayName("出てくる弾み：伸び上がる時間"), Range(0.01, 2)]
    float stretchDuration = 0.18f;

    [DisplayName("出てくる弾み：伸びたときの横の倍率"), Range(0.1, 3)]
    float stretchWidth = 0.7f;

    [DisplayName("出てくる弾み：伸びたときの縦の倍率"), Range(0.1, 3)]
    float stretchHeight = 1.4f;

    [DisplayName("出てくる弾み：潰れる時間"), Range(0.01, 2)]
    float squashDuration = 0.1f;

    [DisplayName("出てくる弾み：潰れたときの横の倍率"), Range(0.1, 3)]
    float squashWidth = 1.28f;

    [DisplayName("出てくる弾み：潰れたときの縦の倍率"), Range(0.1, 3)]
    float squashHeight = 0.74f;

    [DisplayName("出てくる弾み：戻る時間"), Range(0.01, 2)]
    float settleDuration = 0.22f;

    [DisplayName("ゲームオーバーの音")]
    string gameOverSePath = "Application/Assets/Sounds/SE/gameover.mp3";

    [DisplayName("ゲームオーバーのサルの声")]
    string gameOverVoicePath = "Application/Assets/Sounds/SE/guaaaaaaaaaaa.mp3";

    private RailPath@ path_;
    private GameManager@ gameManager_;
    private Hunger@ hunger_;
    private Transform@ transform_;
    private float moveSpeed_ = 1.5f;
    private float minMoveSpeed_ = 1.5f;
    private int gridX_ = 0;
    private int gridZ_ = 0;
    private int destinationX_ = 0;
    private int destinationZ_ = 0;
    private float progress_ = 0.0f;
    private float previousHeadingYaw_ = 0.0f;
    private float headingYaw_ = 0.0f;
    private float nextHeadingYaw_ = 0.0f;
    private float entryTurnProgress_ = 0.0f;
    private array<TrainCarriage@> carriages_;
    private array<TrainMonkey@> monkeys_;
    // 通ったマス（最後尾の 1 マス後ろまで残す）
    private array<int> traveledCells_;
    // 駅に入ったときの通算の歩数（最後尾が駅を抜けたら連結する）
    private array<int> pendingStationSteps_;
    private int traveledBlockCount_ = 0;
    private int horizontalProgressBlocks_ = 0;
    private float rockThrowJumpElapsed_ = 0.0f;
    private bool isMoving_ = false;
    private bool hasHeading_ = false;
    private bool isRockThrowJumping_ = false;
    private bool gameOverLaunchStarted_ = false;
    private bool hasStarted_ = false;
    private bool isGameOver_ = false;
    private bool isPausedForRockBreak_ = false;
    private bool stopped_ = false;

    // GameDirector が最初に呼ぶ
    void Setup(RailPath@ path, GameManager@ gameManager, Hunger@ hunger)
    {
        @path_ = path;
        @gameManager_ = gameManager;
        @hunger_ = hunger;
        @transform_ = owner.transform;
        moveSpeed_ = initialMoveSpeed;
        minMoveSpeed_ = initialMoveSpeed;
        gridX_ = initialGridX;
        gridZ_ = initialGridZ;

        const float modelScale = BlockLayout::GetScale(gridSize);
        transform_.scale = Vector3(modelScale, modelScale, modelScale);
        transform_.position = Vector3(gridX_ * gridSize, BlockLayout::GetRailTopHeight(gridSize), gridZ_ * gridSize);
        previousHeadingYaw_ = kTrainInitialHeadingYaw;
        headingYaw_ = kTrainInitialHeadingYaw;
        nextHeadingYaw_ = kTrainInitialHeadingYaw;
        transform_.rotation = Vector3(0.0f, kTrainInitialHeadingYaw, 0.0f);
        traveledCells_.resize(0);
        traveledCells_.insertLast(CellKey(gridX_, gridZ_));

        // 先頭のサルはシーンでトロッコの子に置いてある
        GameObject@ headMonkey = owner.FindObject(headMonkeyName);
        if (headMonkey !is null) {
            AddMonkey(headMonkey);
        } else {
            Warn("TrainMovement: 先頭のサル " + headMonkeyName + " が見つかりません");
        }
    }

    // ---------------------------------------------------------------- 問い合わせ

    float GetMoveSpeed() const { return moveSpeed_; }
    float GetMinMoveSpeed() const { return minMoveSpeed_; }
    int GetHorizontalProgressBlocks() const { return horizontalProgressBlocks_; }
    bool IsGameOver() const { return isGameOver_; }
    int GetCarriageCount() const { return int(carriages_.length()); }

    Vector3 GetWorldPosition() const
    {
        return transform_.position;
    }

    // サルが乗っている車両（0 が先頭のトロッコ）
    Transform@ GetMonkeyTransform(int monkeyIndex)
    {
        if (monkeyIndex == 0) {
            return transform_;
        }
        const int carriageIndex = monkeyIndex - 1;
        return carriageIndex < int(carriages_.length()) ? carriages_[carriageIndex].transform : null;
    }

    // 矢印と車両が重なるとき、矢印を持ち上げる高さ
    float GetCursorHeightOffsetAt(float worldX, float worldZ) const
    {
        float offset = ConsiderVehicle(transform_.position, worldX, worldZ, 0.0f);
        for (uint i = 0; i < carriages_.length(); ++i) {
            offset = ConsiderVehicle(carriages_[i].transform.position, worldX, worldZ, offset);
        }
        return offset;
    }

    // ---------------------------------------------------------------- 外からの指示

    void SetRockBreakPaused(bool paused)
    {
        isPausedForRockBreak_ = paused;
    }

    // 終わりが決まったら止める
    void Stop()
    {
        stopped_ = true;
    }

    void PlayRockThrowJump()
    {
        rockThrowJumpElapsed_ = 0.0f;
        isRockThrowJumping_ = rockThrowJumpDuration > 0.0f && rockThrowJumpHeight > 0.0f;
    }

    // 駅から連結した車両とサルを足す（スタミナの AddMonkey から呼ばれる）
    void SpawnCarriage(int monkeyCount)
    {
        if (traveledCells_.length() < carriages_.length() + 2) {
            return;
        }
        GameObject@ carriageObject = owner.InstantiatePrefab(
            "Application/Assets/Prefabs/Game/Trolley.prefab", "TrainCarriage_" + monkeyCount);
        if (carriageObject is null) {
            return;
        }
        TrainCarriage@ carriage = TrainCarriage();
        @carriage.object = carriageObject;
        @carriage.transform = carriageObject.transform;
        carriage.yaw = headingYaw_;
        carriages_.insertLast(carriage);
        PlayPopIn(carriage);
        SyncCarriageTransforms();
        carriage.transform.UpdateMatrix();

        GameObject@ monkey = owner.InstantiatePrefab(
            "Application/Assets/Prefabs/Game/Monkey.prefab", "Monkey_" + monkeyCount);
        if (monkey !is null) {
            monkey.transform.SetParent(carriage.transform);
            monkey.transform.position = Vector3(0, 0, 0);
            monkey.transform.rotation = Vector3(0, 0, 0);
            monkey.transform.scale = Vector3(1, 1, 1);
            monkey.transform.UpdateMatrix();
            AddMonkey(monkey);
        }
    }

    // ---------------------------------------------------------------- 毎フレーム

    // GameDirector が Update から呼ぶ
    void Tick(float deltaTime)
    {
        if (stopped_ || isGameOver_ || path_ is null) {
            return;
        }

        UpdateRockThrowJump(deltaTime);
        const float modelScale = BlockLayout::GetScale(gridSize);
        transform_.scale = Vector3(modelScale, modelScale, modelScale);
        SetHeight(BlockLayout::GetRailTopHeight(gridSize) + GetRockThrowJumpOffset());
        SyncCarriageTransforms();

        // 岩を壊し終わるまではその場で投石の跳ねだけ
        if (isPausedForRockBreak_) {
            return;
        }
        // 発車前は、未確定のレールが決まった数たまるまで待つ
        if (!hasStarted_) {
            if (path_.GetUnconfirmedRailCount() < requiredRailCount) {
                return;
            }
            hasStarted_ = true;
        }
        if (deltaTime <= 0.0f) {
            return;
        }

        const int monkeyCount = hunger_.GetMonkeyCount();
        const float additionalMonkeys = float(monkeyCount > 0 ? monkeyCount - 1 : 0);
        const float speedBonus = 1.0f + additionalMonkeys * minimumSpeedMonkeyBonusRate;
        const float accelerationBonus = 1.0f + additionalMonkeys * accelerationMonkeyBonusRate;
        const float dynamicMinimum = initialMoveSpeed + float(path_.GetLaidRailCount()) * minimumSpeedIncreasePerRail * speedBonus;
        minMoveSpeed_ = Min(dynamicMinimum, maximumMoveSpeed);
        moveSpeed_ = Clamp(moveSpeed_ + acceleration * accelerationBonus * deltaTime, minMoveSpeed_, maximumMoveSpeed);

        if (!isMoving_ && !BeginNextSegment()) {
            NotifyGameOver();
            return;
        }
        float remaining = moveSpeed_ * deltaTime;
        if (remaining <= 0.0f) {
            return;
        }

        // 大きな時間差でも目的地を飛び越さないよう、余った進み分を次のマスへ持ち越す
        while (remaining > 0.0f && !isGameOver_) {
            if (!isMoving_ && !BeginNextSegment()) {
                NotifyGameOver();
                break;
            }
            const float applied = Min(remaining, 1.0f - progress_);
            progress_ += applied;
            remaining -= applied;
            SyncTransformToProgress();
            if (progress_ < 1.0f) {
                break;
            }

            // 誤差を残さず、着いたマスの中央へ固定する
            gridX_ = destinationX_;
            gridZ_ = destinationZ_;
            progress_ = 0.0f;
            isMoving_ = false;
            Vector3 position = transform_.position;
            position.x = gridX_ * gridSize;
            position.z = gridZ_ * gridSize;
            transform_.position = position;

            horizontalProgressBlocks_ = Max(horizontalProgressBlocks_, Max(0, gridX_ - initialGridX));
            const bool stationActivated = hunger_.OnTrainEnteredCell(gridX_, gridZ_);
            if (stationActivated) {
                // 駅では今の最低速度を次の加速の始まりにする
                moveSpeed_ = minMoveSpeed_;
            }
            ProcessCarriageArrival(stationActivated);

            // 発車後に先端へ着いたらゲームオーバー
            if (path_.GetUnconfirmedRailCount() == 0) {
                NotifyGameOver();
            }
        }
    }

    // ---------------------------------------------------------------- 進む

    private bool BeginNextSegment()
    {
        const int destination = path_.GetNextUnconfirmedRail();
        if (destination < 0) {
            return false;
        }
        const int deltaX = CellX(destination) - gridX_;
        const int deltaZ = CellZ(destination) - gridZ_;
        if (AbsInt(deltaX) + AbsInt(deltaZ) != 1) {
            Error("TrainMovement: レールが続いていません");
            return false;
        }

        // 確定でキューから消える前に目的地を控える
        destinationX_ = CellX(destination);
        destinationZ_ = CellZ(destination);
        if (!path_.ConfirmNextRailPlacement()) {
            return false;
        }
        progress_ = 0.0f;
        isMoving_ = true;

        // 直前のマスで先読みした向きと同じなら、角の手前で半分曲がり終えている
        const float headingYaw = HeadingYawFromDelta(deltaX, deltaZ, headingYaw_);
        entryTurnProgress_ = (hasHeading_ && abs(nextHeadingYaw_ - headingYaw) < 1.0e-4f) ? 0.5f : 0.0f;
        previousHeadingYaw_ = hasHeading_ ? headingYaw_ : headingYaw;
        headingYaw_ = headingYaw;
        hasHeading_ = true;

        // 確定でキューの先頭は目的地の次のマスになる。そこから曲がり終わりの向きを先読みする
        nextHeadingYaw_ = headingYaw_;
        const int lookahead = path_.GetNextUnconfirmedRail();
        if (lookahead >= 0) {
            const int nextDeltaX = CellX(lookahead) - destinationX_;
            const int nextDeltaZ = CellZ(lookahead) - destinationZ_;
            if (AbsInt(nextDeltaX) + AbsInt(nextDeltaZ) == 1) {
                nextHeadingYaw_ = HeadingYawFromDelta(nextDeltaX, nextDeltaZ, headingYaw_);
            }
        }
        UpdateRotation();
        return true;
    }

    private void SyncTransformToProgress()
    {
        const float startX = gridX_ * gridSize;
        const float startZ = gridZ_ * gridSize;
        const float endX = destinationX_ * gridSize;
        const float endZ = destinationZ_ * gridSize;
        transform_.position = Vector3(startX + (endX - startX) * progress_,
            BlockLayout::GetRailTopHeight(gridSize) + GetRockThrowJumpOffset(),
            startZ + (endZ - startZ) * progress_);
        UpdateRotation();
        SyncCarriageTransforms();
    }

    private void UpdateRotation()
    {
        Vector3 rotation = transform_.rotation;
        rotation.y = EvaluateTurnYaw(previousHeadingYaw_, headingYaw_, nextHeadingYaw_, progress_, entryTurnProgress_);
        transform_.rotation = rotation;
    }

    // 曲がり角の中心を境に、前後 turnBlendRatio マスかけて向きを変える
    private float EvaluateTurnYaw(float previousYaw, float headingYaw, float nextYaw, float progress, float entryTurnProgress) const
    {
        const float ratio = Clamp(turnBlendRatio, 0.0f, 0.5f);
        if (ratio <= 0.0f) {
            return headingYaw;
        }
        const float clamped = Clamp(progress, 0.0f, 1.0f);
        if (clamped < ratio) {
            const float entry = Clamp(entryTurnProgress, 0.0f, 1.0f);
            const float turnProgress = entry + (1.0f - entry) * (clamped / ratio);
            return LerpAngle(previousYaw, headingYaw, turnProgress, EaseType::EaseInOutSine);
        }
        if (clamped > 1.0f - ratio) {
            const float turnProgress = 0.5f * ((clamped - (1.0f - ratio)) / ratio);
            return LerpAngle(headingYaw, nextYaw, turnProgress, EaseType::EaseInOutSine);
        }
        return headingYaw;
    }

    // ---------------------------------------------------------------- 車両

    private void AddMonkey(GameObject@ monkey)
    {
        TrainMonkey@ entry = TrainMonkey();
        @entry.object = monkey;
        @entry.trail = owner.InstantiatePrefab(
            "Application/Assets/Prefabs/Game/MonkeyLaunchTrail.prefab", "MonkeyLaunchTrail_" + monkeys_.length());
        monkeys_.insertLast(entry);
    }

    // 先頭がマスの中央に着いたら、後ろの車両のサルも同じ瞬間に 1 マス後ろの中央へ着く
    private void ProcessCarriageArrival(bool stationActivated)
    {
        ++traveledBlockCount_;
        traveledCells_.insertLast(CellKey(gridX_, gridZ_));
        for (uint index = 0; index < carriages_.length(); ++index) {
            const uint offset = index + 1;
            if (traveledCells_.length() <= offset) {
                break;
            }
            const int cell = traveledCells_[traveledCells_.length() - 1 - offset];
            hunger_.OnMonkeyEnteredCell(int(offset), CellX(cell), CellZ(cell));
        }
        if (stationActivated) {
            pendingStationSteps_.insertLast(traveledBlockCount_);
        }

        // 最後尾が駅の次の中央へ着いたら連結する（先の駅で増えた車両も含めた最後尾で毎回見る）
        while (pendingStationSteps_.length() > 0
            && traveledBlockCount_ - pendingStationSteps_[0] >= int(carriages_.length()) + 1) {
            pendingStationSteps_.removeAt(0);
            hunger_.AddMonkey();
        }

        // 最後尾の 1 マス後ろまで残し、走った距離に比例して増やさない
        while (traveledCells_.length() > carriages_.length() + 2) {
            traveledCells_.removeAt(0);
        }
        SyncCarriageTransforms();
    }

    // i 両目は通ったマスの履歴を i+1 マス後ろから、先頭と同じ進み具合でなぞる
    private void SyncCarriageTransforms()
    {
        const float modelScale = BlockLayout::GetScale(gridSize);
        const uint cellCount = traveledCells_.length();
        for (uint index = 0; index < carriages_.length(); ++index) {
            const uint offset = index + 1;
            if (cellCount <= offset) {
                break;
            }
            const uint cellIndex = cellCount - 1 - offset;
            const int startCell = traveledCells_[cellIndex];
            const int endCell = traveledCells_[cellIndex + 1];
            const int startX = CellX(startCell);
            const int startZ = CellZ(startCell);
            const int endX = CellX(endCell);
            const int endZ = CellZ(endCell);
            TrainCarriage@ carriage = carriages_[index];

            carriage.transform.scale = Vector3(modelScale * carriage.popScale.x,
                modelScale * carriage.popScale.y, modelScale * carriage.popScale.z);
            carriage.transform.position = Vector3(
                (float(startX) + float(endX - startX) * progress_) * gridSize,
                BlockLayout::GetRailTopHeight(gridSize),
                (float(startZ) + float(endZ - startZ) * progress_) * gridSize);

            const float headingYaw = HeadingYawFromDelta(endX - startX, endZ - startZ, carriage.yaw);
            float previousYaw = headingYaw;
            if (cellIndex > 0) {
                const int before = traveledCells_[cellIndex - 1];
                previousYaw = HeadingYawFromDelta(startX - CellX(before), startZ - CellZ(before), headingYaw);
            }
            float nextYaw = headingYaw;
            if (cellIndex + 2 < cellCount) {
                const int after = traveledCells_[cellIndex + 2];
                nextYaw = HeadingYawFromDelta(CellX(after) - endX, CellZ(after) - endZ, headingYaw);
            } else if (isMoving_) {
                // 先頭の次のマスはまだ履歴に無いので、先頭の目的地を使う
                nextYaw = HeadingYawFromDelta(destinationX_ - endX, destinationZ_ - endZ, headingYaw);
            }
            // 車両は履歴から前後が分かるので、いつも角の手前から曲がり始めている
            carriage.yaw = EvaluateTurnYaw(previousYaw, headingYaw, nextYaw, progress_, 0.5f);
            Vector3 rotation = carriage.transform.rotation;
            rotation.y = carriage.yaw;
            carriage.transform.rotation = rotation;
        }
    }

    private void PlayPopIn(TrainCarriage@ carriage)
    {
        carriage.popScale = Vector3(spawnScale, spawnScale, spawnScale);
        TweenSequence pop = Tween::Sequence();
        pop.Append(Tween::To(carriage.popScale, Vector3(stretchWidth, stretchHeight, stretchWidth), stretchDuration,
            TweenVector3Setter(carriage.SetPopScale)).SetEase(EaseType::EaseOutCubic));
        pop.Append(Tween::To(Vector3(stretchWidth, stretchHeight, stretchWidth), Vector3(squashWidth, squashHeight, squashWidth),
            squashDuration, TweenVector3Setter(carriage.SetPopScale)).SetEase(EaseType::EaseOutQuad));
        pop.Append(Tween::To(Vector3(squashWidth, squashHeight, squashWidth), Vector3(1.0f, 1.0f, 1.0f), settleDuration,
            TweenVector3Setter(carriage.SetPopScale)).SetEase(EaseType::EaseOutBack));
        pop.SetLink(carriage.object).SetId("SpawnPop");
    }

    // ---------------------------------------------------------------- 投石の跳ね

    private void UpdateRockThrowJump(float deltaTime)
    {
        if (!isRockThrowJumping_) {
            return;
        }
        rockThrowJumpElapsed_ += Max(deltaTime, 0.0f);
        if (rockThrowJumpElapsed_ >= rockThrowJumpDuration) {
            rockThrowJumpElapsed_ = rockThrowJumpDuration;
            isRockThrowJumping_ = false;
        }
    }

    private float GetRockThrowJumpOffset() const
    {
        if (!isRockThrowJumping_ || rockThrowJumpDuration <= 0.0f) {
            return 0.0f;
        }
        const float progress = Clamp(rockThrowJumpElapsed_ / rockThrowJumpDuration, 0.0f, 1.0f);
        return rockThrowJumpHeight * 4.0f * progress * (1.0f - progress);
    }

    // ---------------------------------------------------------------- ゲームオーバー

    private void NotifyGameOver()
    {
        if (isGameOver_) {
            return;
        }
        isGameOver_ = true;
        SetHeight(BlockLayout::GetRailTopHeight(gridSize));
        PlayGameOverLaunch();
        gameManager_.RequestGameOver();
    }

    // 画面を強く揺らし、トロッコを傾け、乗っているサルを最後のレールの先へ放り投げる
    void PlayGameOverLaunch()
    {
        if (gameOverLaunchStarted_) {
            return;
        }
        gameOverLaunchStarted_ = true;

        CameraShakeParams shake = CameraShakePresets::HeavyHit();
        shake.positionAmplitude *= 2.0f;
        shake.rotationAmplitude *= 2.0f;
        shake.duration = 1.2f;
        shake.timeMode = ShakeTimeMode::Unscaled;
        CameraShake::Play(shake);

        PlayParams sound;
        sound.bus = AudioBus::SE;
        Audio::PlayOneShot(gameOverSePath, sound);
        Audio::PlayOneShot(gameOverVoicePath, sound);

        transform_.UpdateMatrix();
        for (uint i = 0; i < carriages_.length(); ++i) {
            carriages_[i].transform.UpdateMatrix();
        }

        // まだ一度も発車していなければ、履歴の最後の 2 マスから向きを出す
        float finalYaw = hasHeading_ ? headingYaw_ : kTrainInitialHeadingYaw;
        if (!hasHeading_ && traveledCells_.length() >= 2) {
            const int start = traveledCells_[traveledCells_.length() - 2];
            const int end = traveledCells_[traveledCells_.length() - 1];
            finalYaw = HeadingYawFromDelta(CellX(end) - CellX(start), CellZ(end) - CellZ(start), finalYaw);
        }
        // モデルの正面は -Z
        const Vector3 launchDirection(-sin(finalYaw), 0.0f, -cos(finalYaw));

        if (monkeys_.length() > 0) {
            PlayTrolleyTilt(owner, transform_, 0);
            for (uint i = 0; i < carriages_.length(); ++i) {
                PlayTrolleyTilt(carriages_[i].object, carriages_[i].transform, int(i) + 1);
            }
        }
        for (uint i = 0; i < monkeys_.length(); ++i) {
            LaunchMonkey(monkeys_[i], int(i), launchDirection);
        }
    }

    // サルが飛び出す瞬間だけトロッコを傾け、すぐ元へ戻す
    private void PlayTrolleyTilt(GameObject@ trolley, Transform@ trolleyTransform, int index)
    {
        const Vector3 original = trolleyTransform.rotation;
        const Vector3 tilted(original.x + 0.30f, original.y, original.z - 0.46f);
        TweenSequence tilt = Tween::Sequence();
        tilt.Append(Tween::RotateTo(trolley, tilted, 0.10f).SetEase(EaseType::EaseOutQuad));
        tilt.Append(Tween::RotateTo(trolley, original, 0.36f).SetEase(EaseType::EaseInOutSine));
        tilt.SetLink(trolley).SetUpdateType(TweenUpdate::Unscaled).SetId("game_over_trolley_tilt_" + index);
    }

    private void LaunchMonkey(TrainMonkey@ entry, int index, const Vector3 &in launchDirection)
    {
        GameObject@ monkey = entry.object;
        if (monkey is null || !monkey.isAlive) {
            return;
        }
        // 親の車両から外しても見た目が跳ねないよう、今のワールドの姿勢を自分の値へ入れ直す。
        // サルは車両の原点に向きを変えずに乗っているので、ワールドの姿勢は車両と同じ
        Transform@ monkeyTransform = monkey.transform;
        Transform@ parent = monkeyTransform.parent;
        Vector3 startPosition = monkeyTransform.worldPosition;
        Vector3 startRotation = monkeyTransform.rotation;
        Vector3 startScale = monkeyTransform.scale;
        if (parent !is null && parent.exists) {
            const Vector3 parentScale = parent.scale;
            startRotation = parent.rotation + startRotation;
            startScale = Vector3(parentScale.x * startScale.x, parentScale.y * startScale.y, parentScale.z * startScale.z);
        }
        monkeyTransform.SetParent(null);
        monkeyTransform.position = startPosition;
        monkeyTransform.rotation = startRotation;
        monkeyTransform.scale = startScale;

        const float distance = 8.0f + float(index) * 0.35f;
        const float spin = 5.5f;
        const Vector3 apex(startPosition.x + launchDirection.x * distance * 0.42f,
            startPosition.y + 5.5f, startPosition.z + launchDirection.z * distance * 0.42f);
        // 2 区間目も最高点の高さを保ち、落ちずに飛び続ける
        const Vector3 end(startPosition.x + launchDirection.x * distance, apex.y, startPosition.z + launchDirection.z * distance);
        const Vector3 midRotation(startRotation.x + spin * 0.4f, startRotation.y - spin * 0.3f, startRotation.z + spin * 0.5f);
        const Vector3 endRotation(startRotation.x + spin, startRotation.y - spin * 0.75f, startRotation.z + spin * 0.9f);

        if (entry.trail !is null) {
            entry.trail.transform.position = startPosition;
            entry.trail.particleSystem.Clear();
            entry.trail.particleSystem.Play();
        }

        TweenSequence launch = Tween::Sequence();
        launch.Append(Tween::MoveTo(monkey, apex, 0.26f).SetEase(EaseType::EaseOutQuad));
        launch.Join(Tween::RotateTo(monkey, midRotation, 0.26f).SetEase(EaseType::EaseOutCubic));
        launch.Append(Tween::MoveTo(monkey, end, 0.78f).SetEase(EaseType::EaseInCubic));
        launch.Join(Tween::RotateTo(monkey, endRotation, 0.78f).SetEase(EaseType::EaseInCubic));
        launch.SetLink(monkey).SetUpdateType(TweenUpdate::Unscaled).SetId("game_over_monkey_launch_" + index);
        launch.handle.OnUpdate(TweenProgressCallback(entry.OnLaunchUpdate));
        launch.OnComplete(TweenCallback(entry.OnLaunchComplete));
    }

    // ---------------------------------------------------------------- 補助

    private void SetHeight(float y)
    {
        Vector3 position = transform_.position;
        position.y = y;
        transform_.position = position;
    }

    private float ConsiderVehicle(const Vector3 &in position, float worldX, float worldZ, float current) const
    {
        if (abs(position.x - worldX) < gridSize && abs(position.z - worldZ) < gridSize) {
            const float jump = Max(0.0f, position.y - BlockLayout::GetRailTopHeight(gridSize));
            return Max(current, gridSize + jump);
        }
        return current;
    }
}

// 発車前は +X を向く（モデルの正面は -Z）
const float kTrainInitialHeadingYaw = -1.57079633f;

// 進む向き（マスの差）から Y 軸の回転を出す。モデルの正面は -Z。差が無ければ今の向きのまま
float HeadingYawFromDelta(int deltaX, int deltaZ, float fallbackYaw)
{
    if (deltaX > 0) {
        return -kPi * 0.5f;
    }
    if (deltaX < 0) {
        return kPi * 0.5f;
    }
    if (deltaZ < 0) {
        return 0.0f;
    }
    if (deltaZ > 0) {
        return kPi;
    }
    return fallbackYaw;
}
