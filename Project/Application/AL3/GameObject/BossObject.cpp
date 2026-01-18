#include "BossObject.h"
#include "Engine/EngineSystem/EngineSystem.h"
#include "Engine/Graphics/Model/ModelManager.h"
#include "Engine/Graphics/Common/DirectXCommon.h"
#include "Engine/Graphics/TextureManager.h"
#include "Engine/Utility/FrameRate/FrameRateController.h"
#include "Engine/Camera/ICamera.h"
#include "Engine/Collider/AABBCollider.h"
#include "Application/AL3/GameObject/BulletObject.h"
#include "Application/AL3/GameObject/JumpAttackHitbox.h"
#include "Application/AL3/AI/BossAI.h"
#include "Application/AL3/GameObject/PlayerObject.h"
#include "Engine/Particle/ParticleSystem.h"

BossObject::BossObject() = default;

BossObject::~BossObject() = default;

void BossObject::Initialize(std::unique_ptr<CoreEngine::Model> model, CoreEngine::TextureManager::LoadedTexture texture) {
    auto engine = GetEngineSystem();

    // モデルとテクスチャを外部から受け取る
    model_ = std::move(model);
    texture_ = texture;

    // Transformの初期化
    auto dxCommon = engine->GetComponent<CoreEngine::DirectXCommon>();
    if (dxCommon) {
        transform_.Initialize(dxCommon->GetDevice());
    }

    // 初期位置・スケール設定
    transform_.translate = { 0.0f, 0.0f, 10.0f };  // プレイヤーより前方に配置
    transform_.scale = { 1.0f, 1.0f, 1.0f };
    transform_.rotate = { 0.0f, 0.0f, 0.0f };

    // アクティブ状態に設定
    SetActive(true);

    // アニメーション初期化（デフォルトは歩くアニメーション）
    currentAnimationState_ = AnimationState::Walk;
    jumpAttackTimer_.SetName("JumpAttackTimer");
    punchTimer_.SetName("PunchTimer");
    jumpTimer_.SetName("JumpTimer");
    destroyTimer_.SetName("DestroyTimer");

    // ダメージ点滅タイマーを初期化
    damageFlashTimer_.SetName("DamageFlashTimer");
    isFlashing_ = false;
    flashElapsedTime_ = 0.0f;

    // コライダーを作成（サイズ: 幅3, 高さ4, 奥行2の矩形）
    collider_ = std::make_unique<CoreEngine::AABBCollider>(this, CoreEngine::Vector3{ 3.0f, 4.0f, 2.0f });
    collider_->SetLayer(CoreEngine::CollisionLayer::Boss);

    // AIは後で初期化（プレイヤー参照が必要なため）
    ai_ = std::make_unique<BossAI>();
}

void BossObject::InitializeAI(PlayerObject* player) {
    if (ai_ && player) {
        player_ = player;  // プレイヤー参照を保存
        ai_->Initialize(this, player);
    }
}

void BossObject::Update() {
    if (!IsActive() || !model_) {
        return;
    }

    auto engine = GetEngineSystem();

    // FrameRateControllerから1フレームあたりの時間を取得
    auto frameRateController = engine->GetComponent<CoreEngine::FrameRateController>();
    if (!frameRateController) {
        return;
    }

    float deltaTime = frameRateController->GetDeltaTime();

    // 撃破されていない場合のみAI更新
    if (!IsDestroyed() && isAIEnabled_ && ai_) {
        ai_->Update(deltaTime);
    }

    // アニメーション更新
    UpdateAnimation(deltaTime);

    // Transformの更新
    transform_.TransferMatrix();

    // アニメーションの更新（コントローラー経由で自動的にスケルトンも更新される）
    if (model_->HasAnimationController()) {
        model_->UpdateAnimation(deltaTime);
    }

    // ジャンプ攻撃タイマーの更新
    jumpAttackTimer_.Update(deltaTime);

    // パンチタイマーの更新
    punchTimer_.Update(deltaTime);

    // ジャンプタイマーの更新
    jumpTimer_.Update(deltaTime);

    // 撃破タイマーの更新
    destroyTimer_.Update(deltaTime);

    // ダメージ点滅タイマーの更新
    damageFlashTimer_.Update(deltaTime);

    // 点滅処理の更新
    if (isFlashing_) {
        flashElapsedTime_ += deltaTime;

        // 点滅終了判定
        if (flashElapsedTime_ >= damageFlashDuration_) {
            isFlashing_ = false;
            flashElapsedTime_ = 0.0f;

            // 通常色に戻す（マテリアルカラーをリセット）
            if (model_) {
                model_->SetMaterialColor(CoreEngine::Vector4{ 1.0f, 1.0f, 1.0f, 1.0f });
            }
        } else {
            // 点滅エフェクト（0.1秒ごとに赤⇔白を切り替え）
            float interval = fmodf(flashElapsedTime_, flashInterval_ * 2.0f);
            if (interval < flashInterval_) {
                // 赤色
                if (model_) {
                    model_->SetMaterialColor(CoreEngine::Vector4{ 2.0f, 0.5f, 0.5f, 1.0f });
                }
            } else {
                // 白色
                if (model_) {
                    model_->SetMaterialColor(CoreEngine::Vector4{ 1.0f, 1.0f, 1.0f, 1.0f });
                }
            }
        }
    }
}

void BossObject::Draw(const CoreEngine::ICamera* camera) {
    if (!camera || !model_) return;

    // モデルの描画
    model_->Draw(transform_, camera, texture_.gpuHandle);
}

void BossObject::PlayJumpAttackAnimation() {
    if (!model_) return;

    // 既にジャンプ攻撃アニメーション中の場合は無視
    if (jumpAttackTimer_.IsActive()) {
        return;
    }

    // ジャンプ攻撃開始時にコライダーを無効化
    if (collider_) {
        collider_->SetEnabled(false);
    }

    // ジャンプ攻撃アニメーションに切り替え（ループなし）
    currentAnimationState_ = AnimationState::JumpAttack;
    model_->SwitchAnimationWithBlend("bossJumpAttackAnimation", 0.3f, false);

    // ジャンプ攻撃タイマーを開始
    jumpAttackTimer_.Start(jumpAttackDuration_, false);

    // タイマー終了時に歩くアニメーションに戻す（撃破されていない場合のみ）
    jumpAttackTimer_.SetOnComplete([this]() {
        // コライダーを再度有効化
        if (collider_) {
            collider_->SetEnabled(true);
        }

        // 撃破されている場合は何もしない
        if (IsDestroyed()) {
            return;
        }
        currentAnimationState_ = AnimationState::Walk;
        model_->SwitchAnimationWithBlend("bossWalkAnimation", 0.6f, true);
    });
}

void BossObject::PlayPunchAnimation() {
    if (!model_) return;

    // 既にパンチアニメーション中の場合は無視
    if (punchTimer_.IsActive()) {
        return;
    }

    // パンチ攻撃SEを再生
    if (bossPunchSE_ && bossPunchSE_->IsValid()) {
        bossPunchSE_->Play(false);
    }

    // パンチアニメーションに切り替え（ループなし）
    currentAnimationState_ = AnimationState::Punch;
    model_->SwitchAnimationWithBlend("bossPunchAnimation", 0.2f, false);

    // パンチタイマーを開始
    punchTimer_.Start(punchDuration_, false);

    // タイマー終了時に歩くアニメーションに戻す（撃破されていない場合のみ）
    punchTimer_.SetOnComplete([this]() {
        // 撃破されている場合は何もしない
        if (IsDestroyed()) {
            return;
        }
        currentAnimationState_ = AnimationState::Walk;
        model_->SwitchAnimationWithBlend("bossWalkAnimation", 0.3f, true);
    });
}

void BossObject::PlayIdleAnimation() {
    if (!model_) return;

    // 待機アニメーションに切り替え
    currentAnimationState_ = AnimationState::Idle;
    model_->SwitchAnimationWithBlend("bossIdleAnimation", 0.3f, true);
}

void BossObject::PlayWalkAnimation() {
    if (!model_) return;

    // 歩くアニメーションに切り替え
    currentAnimationState_ = AnimationState::Walk;
    model_->SwitchAnimationWithBlend("bossWalkAnimation", 0.3f, true);
}

void BossObject::PlayDashAttackAnimation() {
    if (!model_) return;

    // 突進攻撃アニメーションに切り替え（ループなし）
    currentAnimationState_ = AnimationState::DashAttack;
    model_->SwitchAnimationWithBlend("bossDashAttackAnimation", 0.2f, false);
}

void BossObject::PlayJumpAnimation() {
    if (!model_) return;

    // 既にジャンプアニメーション中の場合は無視
    if (jumpTimer_.IsActive()) {
        return;
    }

    // ジャンプアニメーションに切り替え（ループなし）
    currentAnimationState_ = AnimationState::Jump;
    model_->SwitchAnimationWithBlend("bossJumpAnimation", 0.2f, false);

    // ジャンプタイマーを開始
    jumpTimer_.Start(jumpDuration_, false);

    // タイマー終了時に歩くアニメーションに戻す（撃破されていない場合のみ）
    jumpTimer_.SetOnComplete([this]() {
        // 撃破されている場合は何もしない
        if (IsDestroyed()) {
            return;
        }
        currentAnimationState_ = AnimationState::Walk;
        model_->SwitchAnimationWithBlend("bossWalkAnimation", 0.3f, true);
    });
}

void BossObject::PlayDestroyAnimation() {
    if (!model_) return;

    // 既に撃破アニメーション中の場合は無視
    if (destroyTimer_.IsActive()) {
        return;
    }

    // 撃破アニメーションに切り替え（ループなし）
    currentAnimationState_ = AnimationState::Destroy;
    model_->SwitchAnimationWithBlend("bossDestroyAnimation", 0.3f, false);

    // AIを無効化
    isAIEnabled_ = false;

    // 撃破タイマーを開始
    destroyTimer_.Start(destroyDuration_, false);

    // タイマー終了時に撃破パーティクルを生成し、ボスと影を非表示にする
    destroyTimer_.SetOnComplete([this]() {
        // 撃破パーティクルを生成
        SpawnDestroyParticle(transform_.translate);
        
        // 影を非表示にする
        if (hideShadowCallback_) {
            hideShadowCallback_();
        }
        
        // ボスを非表示にする
        SetActive(false);
    });
}

void BossObject::UpdateAnimation(float deltaTime) {
    // 今後の拡張用（AI実装時などに使用）
    (void)deltaTime;
}

void BossObject::OnCollisionEnter(GameObject* other) {
    if (!other) return;

    // 弾と衝突した場合の処理
    if (dynamic_cast<BulletObject*>(other)) {
        // ダメージを受ける（弾1発につき10ダメージ）
        TakeDamage(10);
        
#ifdef _DEBUG
        auto engine = GetEngineSystem();
        if (engine) {
            // 弾と衝突（デバッグログ）
        }
#endif
    }

    // プレイヤーと衝突した場合の処理
    if (dynamic_cast<PlayerObject*>(other)) {
        // プレイヤーとの衝突処理は既に別で実装されている
#ifdef _DEBUG
        auto engine = GetEngineSystem();
        if (engine) {
            // プレイヤーと衝突（デバッグログ）
        }
#endif
    }
}

JumpAttackHitbox* BossObject::SpawnJumpAttackHitbox(const CoreEngine::Vector3& landingPosition) {
    // ジャンプアタックSEを再生（着地した瞬間）
    if (bossJumpAttackSE_ && bossJumpAttackSE_->IsValid()) {
        bossJumpAttackSE_->Play(false);
    }

    // ヒットボックスを生成
    auto hitbox = std::make_unique<JumpAttackHitbox>();
    
    // ヒットボックスのサイズ（着地範囲）
    CoreEngine::Vector3 hitboxSize = { 4.0f, 2.0f, 4.0f };  // 幅4、高さ2、奥行4
    
    // ヒットボックスのアクティブ時間（着地の瞬間のみ、短めに設定）
    float activeDuration = 0.2f;  // 0.3秒→0.2秒に短縮
    
    // 初期化
    hitbox->Initialize(landingPosition, hitboxSize, activeDuration);
    
    // コールバックがあればシーンに追加
    JumpAttackHitbox* hitboxPtr = hitbox.get();
    if (hitboxSpawnCallback_) {
        hitboxSpawnCallback_(std::move(hitbox));
    }
    
    // 着地時にパーティクルを生成
    SpawnLandingParticle(landingPosition);
    
    return hitboxPtr;
}

void BossObject::SpawnLandingParticle(const CoreEngine::Vector3& position) {
    // コールバックがあればパーティクルを生成
    if (landingParticleCallback_) {
        landingParticleCallback_(position);
    }
}

void BossObject::SpawnDestroyParticle(const CoreEngine::Vector3& position) {
    // 撃破SEを再生
    if (bossDieSE_ && bossDieSE_->IsValid()) {
        bossDieSE_->Play(false);
    }

    // コールバックがあればパーティクルを生成
    if (destroyParticleCallback_) {
        destroyParticleCallback_(position);
    }
}

void BossObject::TakeDamage(int damage) {
    // 既に撃破されている場合はダメージを受けない
    if (IsDestroyed()) {
        return;
    }

    // ダメージを受ける
    currentHP_ -= damage;
    
    // HPが0未満にならないようにクランプ
    if (currentHP_ < 0) {
        currentHP_ = 0;
    }

    // ダメージ点滅を開始
    StartDamageFlash();

    // HPが0になった場合、撃破アニメーションを再生
    if (currentHP_ <= 0) {
        PlayDestroyAnimation();
    }
}

void BossObject::StartDamageFlash() {
    // 点滅を開始
    isFlashing_ = true;
    flashElapsedTime_ = 0.0f;

    // タイマーを開始
    damageFlashTimer_.Start(damageFlashDuration_, false);

    // タイマー終了時に通常色に戻す
    damageFlashTimer_.SetOnComplete([this]() {
        isFlashing_ = false;
        flashElapsedTime_ = 0.0f;
        if (model_) {
            model_->SetMaterialColor(CoreEngine::Vector4{ 1.0f, 1.0f, 1.0f, 1.0f });
        }
    });
}

void BossObject::SpawnBarrageBullet(const CoreEngine::Vector3& position, const CoreEngine::Vector3& direction) {
    // コールバックがあれば弾を生成
    if (barrageBulletSpawnCallback_) {
        barrageBulletSpawnCallback_(position, direction);
    }
}
