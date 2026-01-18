#pragma once

#include "Engine/ObjectCommon/GameObject.h"
#include "Engine/Utility/Timer/GameTimer.h"
#include "Engine/Audio/SoundManager.h"
#include "Engine/Collider/AABBCollider.h"
#include <memory>

// 前方宣言
class BossObject;
class GameCameraController;
class ReticleObject;
class ICamera;

/// @brief プレイヤーオブジェクト
class PlayerObject : public CoreEngine::GameObject {
public:
    /// @brief プレイヤーアニメーション状態
    enum class AnimationState {
        Idle,   // 待機
        Walk,   // 歩く
        Run,    // 走る
        Gun,    // 銃を構える
        Die     // 死亡
    };

    /// @brief 初期化処理
    /// @param model モデルポインタ
    /// @param texture テクスチャハンドル
    void Initialize(std::unique_ptr<CoreEngine::Model> model, CoreEngine::TextureManager::LoadedTexture texture);

    /// @brief 更新処理
    void Update() override;

    /// @brief 描画処理
    /// @param camera カメラ
    void Draw(const CoreEngine::ICamera* camera) override;

    /// @brief 描画パスタイプを取得（スキニングモデル用）
    /// @return 描画パスタイプ
    CoreEngine::RenderPassType GetRenderPassType() const override { return CoreEngine::RenderPassType::SkinnedModel; }

    /// @brief トランスフォームを取得
    CoreEngine::WorldTransform& GetTransform() { return transform_; }

    /// @brief モデルを取得
    CoreEngine::Model* GetModel() { return model_.get(); }

    /// @brief カメラコントローラーを設定
    /// @param cameraController カメラコントローラー
    void SetCameraController(GameCameraController* cameraController) { cameraController_ = cameraController; }

    /// @brief レティクルを設定
    /// @param reticle レティクル
    void SetReticle(ReticleObject* reticle) { reticle_ = reticle; }

    /// @brief カメラを設定
    /// @param camera カメラ
    void SetCamera(CoreEngine::ICamera* camera) { camera_ = camera; }

    /// @brief 弾を発射するか確認（スペースキー入力処理）
    /// @return 発射する場合true
    bool ShouldShoot() const;

    /// @brief 弾発射後のクールダウンを開始
    void StartShootCooldown();

    /// @brief 弾発射アニメーションを再生
    void PlayShootAnimation();

    /// @brief 弾の発射位置を取得
    /// @return 発射位置（プレイヤーの中心位置）
    CoreEngine::Vector3 GetBulletSpawnPosition() const;

    /// @brief 弾の発射方向を取得
    /// @return 発射方向（プレイヤーの前方向き）
    CoreEngine::Vector3 GetBulletDirection() const;

    /// @brief コライダーを取得
    /// @return コライダー（なければnullptr）
    CoreEngine::AABBCollider* GetCollider() const { return collider_.get(); }

    /// @brief 現在の移動方向を取得
    /// @return 移動方向ベクトル（正規化済み、移動していない場合はゼロベクトル）
    CoreEngine::Vector3 GetMoveDirection() const { return currentMoveDirection_; }

    /// @brief 衝突開始時の処理
    /// @param other 衝突相手のオブジェクト
    void OnCollisionEnter(GameObject* other) override;

    /// @brief ダメージ時の点滅処理を開始
    void StartDamageFlash();

    /// @brief ダメージを受ける
    /// @param damage ダメージ量
    void TakeDamage(int damage);

    /// @brief 無敵状態かどうか
    /// @return 無敵中の場合true
    bool IsInvincible() const { return isInvincible_; }

    /// @brief 現在のHPを取得
    /// @return 現在のHP
    int GetCurrentHP() const { return currentHP_; }

    /// @brief 最大HPを取得
    /// @return 最大HP
    int GetMaxHP() const { return maxHP_; }

    /// @brief HP割合を取得（0.0〜1.0）
    /// @return HP割合
    float GetHPRatio() const { return static_cast<float>(currentHP_) / static_cast<float>(maxHP_); }

    /// @brief 死亡アニメーションを再生
    void PlayDeathAnimation();

    /// @brief 死亡しているかどうか
    /// @return 死亡している場合true
    bool IsDead() const { return currentHP_ <= 0; }

    /// @brief 演出中フラグを設定
    /// @param isPlaying 演出中かどうか
    void SetCutscenePlaying(bool isPlaying) { isCutscenePlaying_ = isPlaying; }

    /// @brief サウンドリソースを設定
    /// @param bulletShotSound 弾発射SE
    /// @param deathSound 死亡SE
    /// @param hitSound ダメージSE
    void SetSoundResources(
        std::unique_ptr<CoreEngine::SoundManager::SoundResource> bulletShotSound,
        std::unique_ptr<CoreEngine::SoundManager::SoundResource> deathSound,
        std::unique_ptr<CoreEngine::SoundManager::SoundResource> hitSound) {
        bulletShotSE_ = std::move(bulletShotSound);
        playerDieSE_ = std::move(deathSound);
        playerHitSE_ = std::move(hitSound);
    }

private:
    // ===== 移動処理 =====
    /// @brief 入力処理
    void ProcessInput(float deltaTime);
    
    /// @brief 移動ベクトルを計算
    /// @return 正規化された移動ベクトル
    CoreEngine::Vector3 CalculateMoveDirection() const;
    
    /// @brief キャラクターを移動
    /// @param direction 移動方向
    /// @param deltaTime デルタタイム
    void MoveCharacter(const CoreEngine::Vector3& direction, float deltaTime);
    
    /// @brief 移動速度に応じたアニメーションを設定
    /// @param moveSpeed 現在の移動速度
    void UpdateMovementAnimation(float moveSpeed);
    
    /// @briefキャラクターの向きを更新
    /// @param direction 移動方向
    void UpdateRotation(const CoreEngine::Vector3& direction);

    /// @brief レティクル方向にキャラクターの向きを更新
    void UpdateRotationToReticle();

    // ===== アニメーション管理 =====
    // NOTE: 将来的にAnimationComponentへ分離予定
    /// @brief アニメーション切り替えコールバック
    void OnAnimationSwitch();

    /// @brief 次のアニメーション状態を取得
    AnimationState GetNextAnimationState() const;

    // ===== メンバ変数 =====
    // アニメーション関連
    CoreEngine::GameTimer animationSwitchTimer_;        // アニメーション切り替えタイマー
    AnimationState currentAnimationState_;  // 現在のアニメーション状態
    
    // 移動関連
    float moveSpeed_ = 2.0f;                // 歩く速度
    float runSpeed_ = 5.0f;                // 走る速度
    float rotationSpeed_ = 6.0f;           // 回転速度
    bool isMoving_ = false;                 // 移動中フラグ
    CoreEngine::Vector3 currentMoveDirection_ = { 0.0f, 0.0f, 0.0f }; // 現在の移動方向

    // 弾発射関連
    CoreEngine::GameTimer shootCooldownTimer_;          // 弾発射のクールダウンタイマー
    float shootCooldown_ = 0.2f;            // 弾発射間隔（秒）
    CoreEngine::GameTimer gunAnimationTimer_;           // Gunアニメーションの持続タイマー
    float gunAnimationDuration_ = 0.3f;     // Gunアニメーションの持続時間（秒）

    // ダメージ点滅関連
    CoreEngine::GameTimer damageFlashTimer_;            // ダメージ点滅タイマー
    float damageFlashDuration_ = 1.0f;      // 点滅の持続時間（秒）
    float flashInterval_ = 0.1f;            // 点滅の間隔（秒）
    bool isFlashing_ = false;               // 点滅中フラグ
    float flashElapsedTime_ = 0.0f;         // 点滅経過時間

    // 無敵時間関連
    CoreEngine::GameTimer invincibleTimer_;             // 無敵時間タイマー
    float invincibleDuration_ = 1.0f;       // 無敵時間の持続時間（秒）
    bool isInvincible_ = false;             // 無敵中フラグ

    // コライダー
    std::unique_ptr<CoreEngine::AABBCollider> collider_;  // AABBコライダー

    // HP関連
    int maxHP_ = 100;                       // 最大HP
    int currentHP_ = 100;                   // 現在のHP
    
    // 死亡関連
    CoreEngine::GameTimer deathTimer_;                  // 死亡アニメーションタイマー
    float deathDuration_ = 3.0f;            // 死亡アニメーションの持続時間（秒）
    bool isDying_ = false;                  // 死亡中フラグ

    // 演出制御
    bool isCutscenePlaying_ = false;        // 演出中フラグ

    // サウンドリソース
    std::unique_ptr<CoreEngine::SoundManager::SoundResource> bulletShotSE_;  // 弾発射SE
    std::unique_ptr<CoreEngine::SoundManager::SoundResource> playerDieSE_;   // 死亡SE
    std::unique_ptr<CoreEngine::SoundManager::SoundResource> playerHitSE_;   // ダメージSE

    // カメラコントローラーへの参照
    GameCameraController* cameraController_ = nullptr;

    // レティクルへの参照
    ReticleObject* reticle_ = nullptr;

    // カメラへの参照
    CoreEngine::ICamera* camera_ = nullptr;
};
