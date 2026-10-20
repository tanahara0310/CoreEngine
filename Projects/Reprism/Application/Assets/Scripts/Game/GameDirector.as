using namespace CoreEngine;

// ゲーム画面（元の GameScene2D）。ステージを組み立て、自機とハットの操作・ゴール・Undo・UI をまとめて進める
const string kGoalSuctionMaterial = "Application/Assets/Materials/GoalSuction.mat";

[DisplayName("Reprism：ゲーム")]
class GameDirector : ScriptComponent
{
    private Td3SpriteFactory@ factory_;
    private TileMap@ tileMap_;
    private SpriteGrid stageSprites_;

    private Td3Sprite@ playerSprite_;
    private Td3Sprite@ playerLegL_;
    private Td3Sprite@ playerLegR_;
    private Player2D@ player_;
    private Td3Sprite@ hatSprite_;
    private Hat2D@ hat_;
    private Td3Sprite@ goalSprite_;
    private Goal2D@ goal_;

    private GameObject@ cameraObject_;
    private float zoom_ = 1.0f;
    private Vector2 cameraPosition_;

    private GameBackgroundEffect backgroundEffect_;
    private CyberGridBackground grid_;
    private RangeFrame2D@ rangeFrame_;
    private NormalBlockAnimator@ blockAnimator_;

    private GameActionSoundController soundController_;
    private CameraShakeController cameraShake_;
    private SwitchParticleSystem switchParticles_;
    private BlockDisappearEffect blockDisappearEffect_;
    private GoalClearDirector goalClearDirector_;
    private ActionModeController actionModeController_;
    private EdgeInvasionController edgeInvasion_;

    private GameButtonUI buttonUI_;
    private UndoNotificationEffect undoNotification_;
    private StageNumberUI stageNumberUI_;
    private bool gameplayUIVisible_ = false;

    void Start()
    {
        @factory_ = Td3SpriteFactory(owner);

        // ステージを切り替えても BGM は鳴らし続ける
        EnsureGameSceneBgm();

        InitStage();
        edgeInvasion_.Setup(kActionModePlayer);
        InitBackground();
        InitBlockAnimator();
        InitSubsystems();
        InitUI();
    }

    void OnDestroy()
    {
        if (!gPreserveGameSceneBgm) {
            StopGameSceneBgm();
        }
        gPreserveGameSceneBgm = false;
    }

    // タイル・自機・ハット・ゴールを並べ、カメラをマップの幅に合わせる
    private void InitStage()
    {
        @tileMap_ = LoadStage(GetStageNumber());
        stageSprites_.Initialize(tileMap_.cols, tileMap_.rows);

        // 列ごとに上から並べる
        for (int c = 0; c < tileMap_.cols; ++c) {
            for (int r = 0; r < tileMap_.rows; ++r) {
                const int id = tileMap_.GetTile(c, r);
                if (id == kTileBlank) {
                    continue;
                }
                const Vector2 worldPos = tileMap_.TileToWorld(c, r);
                if (id == kTileBlock) {
                    stageSprites_.Set(stageSprites_.Key(c, r), CreateTileSprite("Block/normalBlock.png", "Tile_Block", worldPos));
                } else if (id == kTileUnbreakable) {
                    stageSprites_.Set(stageSprites_.Key(c, r), CreateTileSprite("Block/unBreakBlock.png", "Tile_Unbreakable", worldPos));
                } else if (id == kTileGoal) {
                    @goalSprite_ = CreateTileSprite("white1x1.png", "Goal", worldPos);
                } else if (id == kTilePlayer) {
                    @playerSprite_ = CreateTileSprite("Player/player.png", "Player", worldPos);
                    @playerLegL_ = CreateTileSprite("Player/PlayerLeg.png", "PlayerLegL", worldPos);
                    @playerLegR_ = CreateTileSprite("Player/PlayerLeg.png", "PlayerLegR", worldPos);
                } else if (id == kTileHat) {
                    @hatSprite_ = CreateTileSprite("Hat/Player2.png", "Hat", worldPos);
                }
            }
        }

        if (playerSprite_ !is null) {
            playerSprite_.SetSortingLayer(5);
            // 右向きで始まるので左足が奥・右足が手前
            playerLegL_.SetSortingLayer(4);
            playerLegR_.SetSortingLayer(6);
            @player_ = Player2D();
            player_.Initialize(playerSprite_, playerLegL_, playerLegR_, playerSprite_.GetTranslate(), tileMap_);
        }
        if (hatSprite_ !is null) {
            hatSprite_.SetSortingLayer(5);
            @hat_ = Hat2D();
            hat_.Initialize(hatSprite_, hatSprite_.GetTranslate(), tileMap_, factory_);
        }
        if (goalSprite_ !is null) {
            @goal_ = Goal2D();
            goal_.Initialize(goalSprite_, goalSprite_.GetTranslate());
        }

        // 自機とハットはゴールに吸い込まれる見た目のシェーダで描く（スプライトの色は効かない）
        if (playerSprite_ !is null) {
            playerSprite_.SetMaterial(kGoalSuctionMaterial);
        }
        if (hatSprite_ !is null) {
            hatSprite_.SetMaterial(kGoalSuctionMaterial);
        }

        if (hat_ !is null) {
            hat_.SetStageSpriteMap(stageSprites_);
        }

        // マップの幅が画面の幅になるように拡大する
        @cameraObject_ = owner.FindObject("MainCamera");
        const float mapWidth = float(tileMap_.cols) * kTileSize;
        const float mapHeight = float(tileMap_.rows) * kTileSize;
        zoom_ = kScreenWidth / mapWidth;
        cameraPosition_ = Vector2(0.0f, 0.0f);
        ApplyCamera();

        const float halfVisibleW = (kScreenWidth * 0.5f) / zoom_;
        const float halfVisibleH = (kScreenHeight * 0.5f) / zoom_;
        const float left = Max(tileMap_.origin.x, -halfVisibleW);
        const float right = Min(tileMap_.origin.x + mapWidth, halfVisibleW);
        const float top = Min(tileMap_.origin.y, halfVisibleH);
        const float bottom = Max(tileMap_.origin.y - mapHeight, -halfVisibleH);
        if (player_ !is null) {
            player_.SetMapBounds(left, right, bottom, top);
        }
        if (hat_ !is null) {
            hat_.SetMapBounds(left, right, bottom, top);
        }
    }

    private Td3Sprite@ CreateTileSprite(const string &in texture, const string &in name, const Vector2 &in worldPos)
    {
        Td3Sprite@ sprite = factory_.Create(texture, name);
        sprite.SetAnchor(Vector2(0.5f, 0.5f));
        sprite.SetSortingLayer(0);
        sprite.SetScale(1.0f, 1.0f);
        sprite.SetTranslate(worldPos);
        sprite.SetColor(Vector4(1.0f, 1.0f, 1.0f, 1.0f));
        return sprite;
    }

    // 黒い背景・範囲の枠・背景の粒・格子
    private void InitBackground()
    {
        const float mapW = float(tileMap_.cols) * kTileSize;
        const float mapH = float(tileMap_.rows) * kTileSize;
        Td3Sprite@ black = factory_.Create("white1x1.png", "Debug_BG");
        black.SetColor(Vector4(0.0f, 0.0f, 0.0f, 1.0f));
        black.SetSortingLayer(-10);
        black.SetAnchor(Vector2(0.5f, 0.5f));
        black.SetScale(Max(mapW, 1280.0f), Max(mapH, 720.0f));
        black.SetTranslate(0.0f, 0.0f);

        @rangeFrame_ = RangeFrame2D();
        rangeFrame_.Initialize(factory_);
        rangeFrame_.SetPlayerVisible(player_ !is null);
        rangeFrame_.SetHatVisible(hat_ !is null);
        if (hat_ !is null) {
            hat_.SetRangeFrame(rangeFrame_);
        }

        backgroundEffect_.Initialize(factory_, tileMap_.cols, tileMap_.rows, 36);
        grid_.Initialize(factory_, "bgGrid_game");
    }

    private void InitBlockAnimator()
    {
        @blockAnimator_ = NormalBlockAnimator();
        // 壊せないブロックはブロックの見た目のシェーダを使わない
        array<int> unbreakableKeys;
        for (int c = 0; c < tileMap_.cols; ++c) {
            for (int r = 0; r < tileMap_.rows; ++r) {
                const int id = tileMap_.GetTile(c, r);
                if (id == kTileUnbreakable || id == kTileWall) {
                    unbreakableKeys.insertLast(stageSprites_.Key(c, r));
                }
            }
        }
        blockAnimator_.SetUnbreakableKeys(unbreakableKeys);
        blockAnimator_.Initialize(stageSprites_);
        if (hat_ !is null) {
            hat_.SetBlockAnimator(blockAnimator_);
        }
    }

    private void InitSubsystems()
    {
        @switchParticles_.pool = Td3SpritePool(factory_, "white1x1.png", "SwitchParticle");

        goalClearDirector_.Initialize(factory_, GoalTransitionCallback(this.CompleteGoalTransition));
        goalClearDirector_.SetSoundController(soundController_);
        goalClearDirector_.SetGoal(goal_);
        goalClearDirector_.SetGoalSprite(goalSprite_);
        goalClearDirector_.SetPlayerSprites(playerSprite_, playerLegL_, playerLegR_);
        goalClearDirector_.SetHatSprite(hatSprite_);
        goalClearDirector_.SetSuctionMaterial(ShaderMaterial(kGoalSuctionMaterial));
        goalClearDirector_.SetGoalClearTime(1.5f);

        @blockDisappearEffect_.blockPool = Td3SpritePool(factory_, "white1x1.png", "BlockDisappearBlock");
        @blockDisappearEffect_.particlePool = Td3SpritePool(factory_, "white1x1.png", "BlockDisappearParticle");

        actionModeController_.Setup(player_, hat_, tileMap_, stageSprites_, blockAnimator_, switchParticles_,
            cameraShake_, blockDisappearEffect_, soundController_);
    }

    private void InitUI()
    {
        buttonUI_.Initialize(owner);
        undoNotification_.Initialize(owner);
        stageNumberUI_.Initialize(owner, GetStageNumber());
        SetGameplayUIVisible(false);
    }

    void Update()
    {
        const float dt = Time::DeltaTime();

        backgroundEffect_.Update(dt);
        grid_.Update(dt, zoom_, cameraPosition_);

        cameraShake_.Update(dt);
        cameraPosition_ = cameraShake_.offset;
        ApplyCamera();

        switchParticles_.Update(dt);
        blockDisappearEffect_.Update(dt);

        edgeInvasion_.Update(dt, actionModeController_.GetMode());

        UpdateInput(dt);
        UpdateGoal(dt);
        UpdateSceneInput();
        UpdateAnimators(dt);

        const bool shouldShowGameplayUI = stageNumberUI_.IsFinished();
        if (shouldShowGameplayUI != gameplayUIVisible_) {
            SetGameplayUIVisible(shouldShowGameplayUI);
        }
        undoNotification_.Update(dt);
        stageNumberUI_.Update(dt);

        // 板のアニメを進める（元のオブジェクト側の更新）
        if (playerSprite_ !is null) {
            playerSprite_.ObjectUpdate(dt);
        }
        if (hat_ !is null) {
            hat_.ObjectUpdate(dt);
        }
    }

    private void ApplyCamera()
    {
        if (cameraObject_ is null) {
            return;
        }
        cameraObject_.transform.position = Vector3(cameraPosition_.x, cameraPosition_.y, -10.0f);
        cameraObject_.camera.orthographicSize = kScreenHeight * 0.5f / zoom_;
    }

    private void UpdatePlayerWithoutInput(float dt)
    {
        if (player_ !is null) {
            player_.Update(dt);
        }
    }

    private void UpdateHatWithoutInput(float dt)
    {
        if (hat_ !is null) {
            hat_.Update(dt);
        }
    }

    private void UpdatePlayerWithInput(float dt)
    {
        if (player_ is null) {
            return;
        }
        // 落ちて戻る間は操作を受けない
        const FallOutState prevFallState = player_.GetFallOutState();
        const bool disabled = prevFallState == FallOutState::FallingOut || prevFallState == FallOutState::Returning;
        if (!disabled) {
            if (ActionPressed(InputAction::MoveLeft)) {
                player_.MoveLeft();
            }
            if (ActionPressed(InputAction::MoveRight)) {
                player_.MoveRight();
            }
            if (ActionTriggered(InputAction::Jump)) {
                if (player_.Jump()) {
                    soundController_.PlayJump();
                }
            }
        }
        player_.Update(dt);
        if (prevFallState == FallOutState::Normal && player_.GetFallOutState() == FallOutState::FallingOut) {
            soundController_.PlayDead();
        }
    }

    private void UpdateHatWithInput(float dt)
    {
        if (hat_ is null) {
            return;
        }
        // 置けなかった演出の間は操作を受けない
        if (hat_.IsBlocked()) {
            hat_.Update(dt);
            return;
        }
        if (ActionPressed(InputAction::MoveUp)) {
            hat_.Move(HatDirection::Up);
        }
        if (ActionPressed(InputAction::MoveDown)) {
            hat_.Move(HatDirection::Down);
        }
        if (ActionPressed(InputAction::MoveLeft)) {
            hat_.Move(HatDirection::Left);
        }
        if (ActionPressed(InputAction::MoveRight)) {
            hat_.Move(HatDirection::Right);
        }
        hat_.Update(dt);
        if (hat_.PopStuckMoveAttempt()) {
            soundController_.PlayError();
        }
    }

    private void UpdateInput(float dt)
    {
        // ステージの始めの表示中と、ゴールに吸い込まれている間は操作を受けない
        if (!stageNumberUI_.IsFinished() || goalClearDirector_.IsGoalDisappearStarted()) {
            UpdatePlayerWithoutInput(dt);
            UpdateHatWithoutInput(dt);
            return;
        }

        if (actionModeController_.GetMode() == kActionModePlayer) {
            UpdatePlayerWithInput(dt);
            UpdateHatWithoutInput(dt);
        } else {
            UpdateHatWithInput(dt);
            UpdatePlayerWithoutInput(dt);
        }

        if (hat_ !is null) {
            if (player_ !is null) {
                hat_.SetPlayerWorldPos(true, player_.GetPosition());
            } else {
                hat_.SetPlayerWorldPos(false, Vector2(0.0f, 0.0f));
            }
        }

        // 自機からハットへは地面にいるときだけ切り替えられる
        if (hat_ !is null && ActionTriggered(InputAction::SwitchMode)) {
            if (actionModeController_.GetMode() != kActionModePlayer || (player_ !is null && player_.OnGround())) {
                actionModeController_.SwitchMode();
            }
        }

        if (ActionTriggered(InputAction::Undo) && actionModeController_.CanUndo()) {
            actionModeController_.ExecuteUndo();
            soundController_.PlayUndo();
            undoNotification_.Play();
            cameraShake_.Start(0.08f, 3.0f);
        }
    }

    private void UpdateGoal(float dt)
    {
        if (goal_ is null) {
            return;
        }
        const bool playerTouch = player_ !is null && goal_.IsTouching(player_.GetPosition(), kPlayerHalfW, kPlayerHalfH);
        const bool hatTouch = hat_ !is null && goal_.IsTouching(hat_.GetPosition(), kHatHalfW, kHatHalfH);
        goalClearDirector_.Update(dt, playerTouch, hatTouch, cameraPosition_, zoom_);
    }

    private void UpdateSceneInput()
    {
        if (ActionTriggered(InputAction::Back)) {
            PlaySe("SE_Decide.mp3");
            gPreserveGameSceneBgm = false;
            ChangeSceneTd3(kSceneStageSelect);
        }
        if (ActionTriggered(InputAction::Restart)) {
            gPreserveGameSceneBgm = true;
            ChangeSceneTd3(kSceneGame);
        }
    }

    private void UpdateAnimators(float dt)
    {
        const Vector2 playerPos = (player_ !is null) ? player_.GetPosition() : Vector2(0.0f, 0.0f);
        const Vector2 hatPos = (hat_ !is null) ? hat_.GetPosition() : Vector2(0.0f, 0.0f);
        rangeFrame_.Update(playerPos, hatPos, dt, actionModeController_.GetMode());

        if (player_ is null) {
            return;
        }

        // ハットを操作している間は、ハットの範囲と自機の範囲のブロックを描かない
        const float halfSize = 2.5f * kTileSize;
        const bool hatActive = hat_ !is null && hat_.IsActionMode();
        if (hatActive) {
            blockAnimator_.SetHatDiscardRect(true, hatPos, halfSize, true, playerPos, halfSize);
        } else {
            blockAnimator_.SetHatDiscardRect(false, Vector2(0.0f, 0.0f), 0.0f, true, playerPos, halfSize);
        }
        blockAnimator_.Update(dt, playerPos, kTileSize, kPlayerHalfH);
        blockAnimator_.ReapplyOverrides();

        if (hatActive) {
            blockAnimator_.ReapplyCopiedSprites(hat_.GetCopiedSprites(), true, playerPos, halfSize);
        }
    }

    private void SetGameplayUIVisible(bool visible)
    {
        gameplayUIVisible_ = visible;
        buttonUI_.SetVisible(visible);
    }

    // ブロックが落ちきったら次のステージへ（最後のステージならステージ選択へ）。画面は落としきった所から開く
    void CompleteGoalTransition()
    {
        const int stage = GetStageNumber();
        if (stage < kMaxStage) {
            SetStageNumber(stage + 1);
            gPreserveGameSceneBgm = true;
            ChangeSceneFadeInOnly(kSceneGame);
        } else {
            gPreserveGameSceneBgm = false;
            ChangeSceneFadeInOnly(kSceneStageSelect);
        }
    }
}
