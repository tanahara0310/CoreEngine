#pragma once

#include "Engine/ObjectCommon/GameObject.h"
#include "Engine/Utility/Timer/GameTimer.h"
#include "Engine/Audio/SoundManager.h"
#include "Engine/Collider/AABBCollider.h"
#include <memory>
#include <functional>

// 前方宣言
class BossAI;
class PlayerObject;
class JumpAttackHitbox;
class ParticleSystem;

/// @brief ボスオブジェクト
class BossObject : public CoreEngine::GameObject {
public:
    /// @brief ボスアニメーション状態
    enum class AnimationState {
        Walk,        // 歩く（デフォルト）
        Idle,        // 待機
        JumpAttack,  // ジャンプ攻撃
        Jump,        // ジャンプ（弾幕攻撃用）
        Punch,       // パンチ攻撃
        DashAttack,  // 突進攻撃
        Destroy      // 撃破アニメーション
    };

    BossObject();
    ~BossObject() override;

    /// @brief 初期化処理
    /// @param model モデルポインタ
    /// @param texture テクスチャハンドル
    void Initialize(std::unique_ptr<CoreEngine::Model> model, CoreEngine::TextureManager::LoadedTexture texture);

    /// @brief AIの初期化（プレイヤー参照が必要）
    /// @param player プレイヤーオブジェクト
    void InitializeAI(PlayerObject* player);

    // 更新処理
    void Update() override;

    // 描画処理
    // @param camera カメラ
    void Draw(const CoreEngine::ICamera* camera) override;

    // 描画パスタイプを取得（スキニングモデル用）
    // @return 描画パスタイプ
    CoreEngine::RenderPassType GetRenderPassType() const override { return CoreEngine::RenderPassType::SkinnedModel; }

    // 衝突開始イベント
    // @param other 衝突相手のオブジェクト
    void OnCollisionEnter(GameObject* other) override;

    // トランスフォームを取得
    CoreEngine::WorldTransform& GetTransform() { return transform_; }

    // モデルを取得
    CoreEngine::Model* GetModel() { return model_.get(); }

    // ジャンプ攻撃アニメーションを再生
    void PlayJumpAttackAnimation();

    // パンチ攻撃アニメーションを再生
    void PlayPunchAnimation();

    // 待機アニメーションを再生
    void PlayIdleAnimation();

    // 歩くアニメーションを再生
    void PlayWalkAnimation();

    // 突進攻撃アニメーションを再生
    void PlayDashAttackAnimation();

    // ジャンプアニメーションを再生
    void PlayJumpAnimation();

    // 撃破アニメーションを再生
    void PlayDestroyAnimation();

    /// @brief ボスが撃破されたか確認
    /// @return 撃破された場合true
    bool IsDestroyed() const { return currentHP_ <= 0; }

    /// @brief コライダーを取得
    /// @return コライダー
    CoreEngine::AABBCollider* GetCollider() const { return collider_.get(); }

    /// @brief AIを有効化/無効化
    /// @param enabled trueで有効化、falseで無効化
    void SetAIEnabled(bool enabled) { isAIEnabled_ = enabled; }

    /// @brief AIが有効か確認
    /// @return 有効な場合true
    bool IsAIEnabled() const { return isAIEnabled_; }

    /// @brief ボスAIを取得（デバッグ用）
    /// @return ボスAI（なければnullptr）
    BossAI* GetAI() const { return ai_.get(); }

    /// @brief プレイヤーへの参照を取得
    /// @return プレイヤーオブジェクト（なければnullptr）
    PlayerObject* GetPlayer() const { return player_; }

    /// @brief ジャンプ攻撃ヒットボックスを生成
    /// @param landingPosition 着地予定位置
    /// @return 生成されたヒットボックス
    JumpAttackHitbox* SpawnJumpAttackHitbox(const CoreEngine::Vector3& landingPosition);

    /// @brief ヒットボックス生成コールバックを設定
    /// @param callback コールバック関数
    void SetHitboxSpawnCallback(std::function<void(std::unique_ptr<JumpAttackHitbox>)> callback) {
        hitboxSpawnCallback_ = callback;
    }

    /// @brief 着地パーティクル生成コールバックを設定
    /// @param callback コールバック関数
    void SetLandingParticleCallback(std::function<void(const CoreEngine::Vector3&)> callback) {
        landingParticleCallback_ = callback;
    }

    /// @brief 撃破パーティクル生成コールバックを設定
    /// @param callback コールバック関数
    void SetDestroyParticleCallback(std::function<void(const CoreEngine::Vector3&)> callback) {
        destroyParticleCallback_ = callback;
    }

    /// @brief 影非表示コールバックを設定
    /// @param callback コールバック関数
    void SetHideShadowCallback(std::function<void()> callback) {
        hideShadowCallback_ = callback;
    }

    /// @brief 着地時のパーティクルを生成
    /// @param position 着地位置
    void SpawnLandingParticle(const CoreEngine::Vector3& position);

    /// @brief 撃破時のパーティクルを生成
    /// @param position 撃破位置
    void SpawnDestroyParticle(const CoreEngine::Vector3& position);

    /// @brief ダメージを受ける
    /// @param damage ダメージ量
    void TakeDamage(int damage);

    /// @brief ダメージ時の点滅処理を開始
    void StartDamageFlash();

    /// @brief サウンドリソースを設定
    /// @param deathSound 死亡SE
    /// @param jumpAttackSound ジャンプアタックSE
    /// @param punchSound パンチ攻撃SE
    void SetSoundResources(
        std::unique_ptr<CoreEngine::SoundManager::SoundResource> deathSound,
        std::unique_ptr<CoreEngine::SoundManager::SoundResource> jumpAttackSound,
        std::unique_ptr<CoreEngine::SoundManager::SoundResource> punchSound) {
        bossDieSE_ = std::move(deathSound);
        bossJumpAttackSE_ = std::move(jumpAttackSound);
        bossPunchSE_ = std::move(punchSound);
    }

    /// @brief 現在のHPを取得
    /// @return 現在のHP
    int GetCurrentHP() const { return currentHP_; }

    /// @brief 最大HPを取得
    /// @return 最大HP
    int GetMaxHP() const { return maxHP_; }

    /// @brief HP割合を取得（0.0〜1.0）
    /// @return HP割合
    float GetHPRatio() const { return static_cast<float>(currentHP_) / static_cast<float>(maxHP_); }

    /// @brief ジャンプ攻撃中かどうか
    /// @return ジャンプ攻撃中の場合true
    bool IsJumpAttacking() const { return currentAnimationState_ == AnimationState::JumpAttack && jumpAttackTimer_.IsActive(); }

    /// @brief ジャンプ攻撃の進行度を取得（0.0〜1.0）
    /// @return 進行度（0.0=開始、1.0=終了）
    float GetJumpAttackProgress() const {
        if (!jumpAttackTimer_.IsActive()) return 0.0f;
        return jumpAttackTimer_.GetElapsedTime() / jumpAttackDuration_;
    }

    /// @brief ジャンプ攻撃の着地予定位置を取得
    /// @return 着地予定位置
    CoreEngine::Vector3 GetJumpAttackLandingPosition() const { return jumpAttackLandingPos_; }

    /// @brief ジャンプ攻撃の開始位置を設定
    /// @param startPos 開始位置
    /// @param landingPos 着地予定位置
    void SetJumpAttackPositions(const CoreEngine::Vector3& startPos, const CoreEngine::Vector3& landingPos) {
        jumpAttackStartPos_ = startPos;
        jumpAttackLandingPos_ = landingPos;
    }

    /// @brief 弾幕弾を生成
    /// @param position 発射位置
    /// @param direction 発射方向（正規化済み）
    void SpawnBarrageBullet(const CoreEngine::Vector3& position, const CoreEngine::Vector3& direction);

    /// @brief 弾幕弾生成コールバックを設定
    /// @param callback コールバック関数
    void SetBarrageBulletSpawnCallback(std::function<void(const CoreEngine::Vector3&, const CoreEngine::Vector3&)> callback) {
        barrageBulletSpawnCallback_ = callback;
    }

private:
    // アニメーション状態を更新
    void UpdateAnimation(float deltaTime);

    // アニメーション関連
    AnimationState currentAnimationState_ = AnimationState::Walk;  // 現在のアニメーション状態（デフォルトは歩く）
    CoreEngine::GameTimer jumpAttackTimer_;                                     // ジャンプ攻撃アニメーションタイマー
    float jumpAttackDuration_ = 2.0f;                              // ジャンプ攻撃アニメーションの持続時間（秒）
    CoreEngine::GameTimer punchTimer_;                                          // パンチ攻撃アニメーションタイマー
    float punchDuration_ = 1.0f;                                   // パンチ攻撃アニメーションの持続時間（秒）
    CoreEngine::GameTimer jumpTimer_;                                           // ジャンプアニメーションタイマー
    float jumpDuration_ = 1.5f;                                    // ジャンプアニメーションの持続時間（秒）
    CoreEngine::GameTimer destroyTimer_;                                        // 撃破アニメーションタイマー
    float destroyDuration_ = 3.0f;                                 // 撃破アニメーションの持続時間（秒）

    // コライダー関連
    std::unique_ptr<CoreEngine::AABBCollider> collider_;                       // AABB（矩形）コライダー

    // AI関連
    std::unique_ptr<BossAI> ai_;                                   // ボスAI
    bool isAIEnabled_ = true;                                      // AI有効フラグ
    PlayerObject* player_ = nullptr;                               // プレイヤーへの参照

    // ジャンプ攻撃ヒットボックス管理
    std::vector<JumpAttackHitbox*> jumpAttackHitboxes_;           // 生成したヒットボックスのリスト

    // ジャンプ攻撃ヒットボックス生成コールバック
    std::function<void(std::unique_ptr<JumpAttackHitbox>)> hitboxSpawnCallback_;

    // 着地パーティクル生成コールバック
    std::function<void(const CoreEngine::Vector3&)> landingParticleCallback_;

    // 撃破パーティクル生成コールバック
    std::function<void(const CoreEngine::Vector3&)> destroyParticleCallback_;

    // 影非表示コールバック
    std::function<void()> hideShadowCallback_;

    // HP関連
    int maxHP_ = 1500;                                              // 最大HP
    int currentHP_ = 1500;                                          // 現在のHP

    // サウンドリソース
    std::unique_ptr<CoreEngine::SoundManager::SoundResource> bossDieSE_;           // 死亡SE
    std::unique_ptr<CoreEngine::SoundManager::SoundResource> bossJumpAttackSE_;    // ジャンプアタックSE
    std::unique_ptr<CoreEngine::SoundManager::SoundResource> bossPunchSE_;         // パンチ攻撃SE

    // ダメージ点滅関連
    CoreEngine::GameTimer damageFlashTimer_;            // ダメージ点滅タイマー
    float damageFlashDuration_ = 0.5f;      // 点滅の持続時間（秒）
    float flashInterval_ = 0.1f;            // 点滅の間隔（秒）
    bool isFlashing_ = false;               // 点滅中フラグ
    float flashElapsedTime_ = 0.0f;         // 点滅経過時間

    // ジャンプ攻撃の位置情報（影の補間用）
    CoreEngine::Vector3 jumpAttackStartPos_ = { 0.0f, 0.0f, 0.0f };     // ジャンプ攻撃開始位置
    CoreEngine::Vector3 jumpAttackLandingPos_ = { 0.0f, 0.0f, 0.0f };   // ジャンプ攻撃着地予定位置

    // 弾幕弾生成コールバック
    std::function<void(const CoreEngine::Vector3&, const CoreEngine::Vector3&)> barrageBulletSpawnCallback_;
};
