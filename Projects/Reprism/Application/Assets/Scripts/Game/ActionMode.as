using namespace CoreEngine;

// 操作の切り替えと Undo（元の ActionModeController・UndoManager）

const int kActionModePlayer = 0;
const int kActionModeHat = 1;

// Undo 1 回分の記録
class UndoSnapshot
{
    array<int> tiles;
    Vector2 playerPos;
    Vector2 hatPos;
    array<HatUndoCommittedCell@> hatCommittedCells;
}

// 操作を切り替える直前の状態を 20 回分まで覚える
class UndoManager
{
    private array<UndoSnapshot@> history_;

    void PushSnapshot(TileMap@ tileMap, const Vector2 &in playerPos, const Vector2 &in hatPos,
        array<HatUndoCommittedCell@>@ hatCommittedCells)
    {
        if (tileMap is null) {
            return;
        }
        UndoSnapshot snap;
        snap.tiles = tileMap.tiles;
        snap.playerPos = playerPos;
        snap.hatPos = hatPos;
        if (hatCommittedCells !is null) {
            snap.hatCommittedCells = hatCommittedCells;
        }
        history_.insertLast(snap);
        // 上限を超えたら古いものから捨てる
        if (history_.length() > 20) {
            history_.removeAt(0);
        }
    }

    // 最後の記録へタイルを戻し、その記録を返す（無ければ null）
    UndoSnapshot@ Undo(TileMap@ tileMap)
    {
        if (history_.length() == 0 || tileMap is null) {
            return null;
        }
        UndoSnapshot@ snap = history_[history_.length() - 1];
        for (int row = 0; row < tileMap.rows; ++row) {
            for (int col = 0; col < tileMap.cols; ++col) {
                const uint index = uint(row * tileMap.cols + col);
                tileMap.SetTile(col, row, index < snap.tiles.length() ? snap.tiles[index] : 0);
            }
        }
        history_.removeLast();
        return snap;
    }

    void Clear() { history_.resize(0); }
    bool CanUndo() const { return history_.length() > 0; }
}

class ActionModeController
{
    private int actionMode_ = kActionModePlayer;
    private UndoManager undoManager_;
    private Player2D@ player_;
    private Hat2D@ hat_;
    private TileMap@ tileMap_;
    private SpriteGrid@ stageSpriteMap_;
    private NormalBlockAnimator@ blockAnimator_;
    private SwitchParticleSystem@ switchParticles_;
    private CameraShakeController@ cameraShake_;
    private BlockDisappearEffect@ blockDisappearEffect_;
    private GameActionSoundController@ soundController_;

    void Setup(Player2D@ player, Hat2D@ hat, TileMap@ tileMap, SpriteGrid@ stageSpriteMap,
        NormalBlockAnimator@ blockAnimator, SwitchParticleSystem@ switchParticles, CameraShakeController@ cameraShake,
        BlockDisappearEffect@ blockDisappearEffect, GameActionSoundController@ soundController)
    {
        @player_ = player;
        @hat_ = hat;
        @tileMap_ = tileMap;
        @stageSpriteMap_ = stageSpriteMap;
        @blockAnimator_ = blockAnimator;
        @switchParticles_ = switchParticles;
        @cameraShake_ = cameraShake;
        @blockDisappearEffect_ = blockDisappearEffect;
        @soundController_ = soundController;

        // ハットしかいないステージは最初からハットを操作する
        actionMode_ = (player_ is null && hat_ !is null) ? kActionModeHat : kActionModePlayer;
    }

    int GetMode() const { return actionMode_; }
    bool CanUndo() const { return undoManager_.CanUndo(); }

    private array<Td3Sprite@>@ CommittedHatSprites() const
    {
        if (hat_ is null) {
            return array<Td3Sprite@>();
        }
        return hat_.GetCommittedHatSprites();
    }

    // ハットの置いたブロックの位置に、消える演出を出す
    private void TriggerBlockDisappearEffect()
    {
        if (hat_ is null || blockDisappearEffect_ is null) {
            return;
        }
        array<Td3Sprite@>@ sprites = hat_.GetCommittedHatSprites();
        for (uint i = 0; i < sprites.length(); ++i) {
            if (sprites[i] is null) {
                continue;
            }
            blockDisappearEffect_.StartDisappear(sprites[i].GetTranslate());
        }
    }

    void SwitchMode()
    {
        // ブロックが出てくる途中は切り替えない
        if (hat_ !is null && hat_.IsSpawnAnimationPlaying()) {
            return;
        }

        if (actionMode_ == kActionModePlayer) {
            const Vector2 playerPos = (player_ !is null) ? player_.GetPosition() : Vector2(0.0f, 0.0f);
            const Vector2 hatPos = (hat_ !is null) ? hat_.GetPosition() : Vector2(0.0f, 0.0f);
            array<HatUndoCommittedCell@>@ hatCommittedCells = null;
            if (hat_ !is null) {
                @hatCommittedCells = hat_.CaptureUndoCommittedCells();
            }
            undoManager_.PushSnapshot(tileMap_, playerPos, hatPos, hatCommittedCells);

            actionMode_ = kActionModeHat;
            if (hat_ !is null) {
                // 置いたブロックは写す前に消える演出を出す
                TriggerBlockDisappearEffect();
                hat_.OnModeChange();
                if (player_ !is null) {
                    hat_.CopyBlocksFrom(player_.GetPosition());
                }
                if (blockAnimator_ !is null) {
                    blockAnimator_.Reapply(stageSpriteMap_, hat_.GetCommittedHatSprites());
                }
            }
            if (player_ !is null && hat_ !is null && switchParticles_ !is null) {
                switchParticles_.Spawn(playerPos, hatPos);
            }
            if (soundController_ !is null) {
                soundController_.PlayChangeToHat();
            }
        } else {
            if (hat_ !is null && !hat_.CanCommit()) {
                if (soundController_ !is null) {
                    soundController_.PlayError();
                }
                hat_.StartOverlapFlash();
                return;
            }
            const Vector2 hatPos = (hat_ !is null) ? hat_.GetPosition() : Vector2(0.0f, 0.0f);
            const Vector2 playerPos = (player_ !is null) ? player_.GetPosition() : Vector2(0.0f, 0.0f);
            actionMode_ = kActionModePlayer;
            if (hat_ !is null) {
                hat_.OnModeChange();
                hat_.CommitPlacement();
            }
            if (blockAnimator_ !is null) {
                blockAnimator_.Reapply(stageSpriteMap_, CommittedHatSprites());
            }
            if (hat_ !is null && player_ !is null && switchParticles_ !is null) {
                switchParticles_.Spawn(hatPos, playerPos);
            }
            if (soundController_ !is null) {
                soundController_.PlayChangeToPlayer();
            }
        }

        if (cameraShake_ !is null) {
            cameraShake_.Start(0.18f, 5.0f);
        }
    }

    void ExecuteUndo()
    {
        UndoSnapshot@ snap = undoManager_.Undo(tileMap_);
        if (snap is null) {
            return;
        }
        if (player_ !is null) {
            player_.SetPosition(snap.playerPos);
        }
        if (hat_ !is null) {
            hat_.SetPosition(snap.hatPos);
        }

        TriggerBlockDisappearEffect();

        // タイルは戻したので、ハットの置いた物と写した物だけを片付けて元のスプライトへ戻す
        if (hat_ !is null) {
            hat_.ResetAfterUndo(snap.hatCommittedCells);
        }

        actionMode_ = kActionModePlayer;

        SyncStageSpriteVisibility();
        if (blockAnimator_ !is null) {
            blockAnimator_.Reapply(stageSpriteMap_, CommittedHatSprites());
        }
    }

    // ステージのスプライトをタイルのある所だけ見せる
    private void SyncStageSpriteVisibility()
    {
        if (tileMap_ is null || stageSpriteMap_ is null) {
            return;
        }
        for (int row = 0; row < stageSpriteMap_.rows; ++row) {
            for (int col = 0; col < stageSpriteMap_.cols; ++col) {
                Td3Sprite@ sprite = stageSpriteMap_.Find(col, row);
                if (sprite is null) {
                    continue;
                }
                sprite.SetActive(tileMap_.GetTile(col, row) != 0);
            }
        }
    }
}
