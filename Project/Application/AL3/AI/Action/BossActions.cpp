#include "BossActions.h"
#include "Application/AL3/GameObject/BossObject.h"
#include "Application/AL3/GameObject/PlayerObject.h"
#include "Engine/Math/MathCore.h"
#include <numbers>
#include <cmath>

// ===== IdleAction =====
IdleAction::IdleAction(BossObject* boss, float duration)
    : boss_(boss), duration_(duration) {}

void IdleAction::Start() {
    BossAction::Start();
    elapsedTime_ = 0.0f;
    boss_->PlayIdleAnimation();
}

void IdleAction::Update(float deltaTime) {
    elapsedTime_ += deltaTime;
    
    if (elapsedTime_ >= duration_) {
        state_ = State::Completed;
    }
}

// ===== WalkAction =====
WalkAction::WalkAction(BossObject* boss, float duration)
    : boss_(boss), duration_(duration) {}

void WalkAction::Start() {
    BossAction::Start();
    elapsedTime_ = 0.0f;
    boss_->PlayWalkAnimation();
}

void WalkAction::Update(float deltaTime) {
    elapsedTime_ += deltaTime;
    
    // プレイヤーに向かって移動
    auto player = boss_->GetPlayer();
    if (player) {
        CoreEngine::Vector3 bossPos = boss_->GetTransform().translate;
        CoreEngine::Vector3 playerPos = player->GetTransform().translate;
        
        // プレイヤーへの方向ベクトルを計算
        CoreEngine::Vector3 direction = CoreEngine::MathCore::Vector::Subtract(playerPos, bossPos);
        direction.y = 0.0f;  // Y軸方向の移動を無効化（地上移動のみ）
        
        // 方向ベクトルを正規化
        float length = CoreEngine::MathCore::Vector::Length(direction);
        if (length > 0.0f) {
            direction = CoreEngine::MathCore::Vector::Multiply(1.0f / length, direction);
            
            // 移動速度
            constexpr float walkSpeed = 2.0f;
            
            // 移動量を計算
            CoreEngine::Vector3 velocity = CoreEngine::MathCore::Vector::Multiply(walkSpeed * deltaTime, direction);
            
            // 座標を更新
            boss_->GetTransform().translate = CoreEngine::MathCore::Vector::Add(bossPos, velocity);
            
            // プレイヤーの方向を向く（修正: Z軸が前方向なので引数を逆に）
            float PI = std::numbers::pi_v<float>;
            float targetAngle = std::atan2f(-direction.x, -direction.z);  // 引数を逆に
            
            // 現在の角度との差を計算
            float currentAngle = boss_->GetTransform().rotate.y;
            float angleDiff = targetAngle - currentAngle;
            
            // -πからπの範囲に正規化
            while (angleDiff > PI) {
                angleDiff -= 2.0f * PI;
            }
            while (angleDiff < -PI) {
                angleDiff += 2.0f * PI;
            }
            
            // 滑らかに回転（回転速度: 5.0 rad/s）
            constexpr float rotationSpeed = 5.0f;
            float rotationAmount = angleDiff * rotationSpeed * deltaTime;
            boss_->GetTransform().rotate.y += rotationAmount;
        }
    }
    
    if (elapsedTime_ >= duration_) {
        state_ = State::Completed;
    }
}

// ===== JumpAttackAction =====
JumpAttackAction::JumpAttackAction(BossObject* boss)
    : boss_(boss), targetPosition_({0.0f, 0.0f, 0.0f}) {}

void JumpAttackAction::Start() {
    BossAction::Start();
    elapsedTime_ = 0.0f;
    isInCooldown_ = false;
    hitboxSpawned_ = false;  // ヒットボックスフラグをリセット
    boss_->PlayJumpAttackAnimation();
    
    // 攻撃開始時にプレイヤーの位置を記録し、着地位置を計算
    auto player = boss_->GetPlayer();
    if (player) {
        CoreEngine::Vector3 bossPos = boss_->GetTransform().translate;
        CoreEngine::Vector3 playerPos = player->GetTransform().translate;
        
        CoreEngine::Vector3 direction = CoreEngine::MathCore::Vector::Subtract(playerPos, bossPos);
        direction.y = 0.0f;
        
        float length = CoreEngine::MathCore::Vector::Length(direction);
        if (length > 0.0f) {
            direction = CoreEngine::MathCore::Vector::Multiply(1.0f / length, direction);
            
            // ジャンプ攻撃の着地予定地点を計算（ボスの前方5.0f）
            constexpr float jumpDistance = 5.0f;
            targetPosition_ = {
                bossPos.x + direction.x * jumpDistance,
                bossPos.y,  // 同じY座標
                bossPos.z + direction.z * jumpDistance
            };
            
            // ボスに開始位置と着地位置を設定（影の補間用）
            boss_->SetJumpAttackPositions(bossPos, targetPosition_);
            
            // 修正: Z軸が前方向なので引数を逆に
            float targetAngle = std::atan2f(-direction.x, -direction.z);
            boss_->GetTransform().rotate.y = targetAngle;
        }
    }
}

void JumpAttackAction::Update(float deltaTime) {
    elapsedTime_ += deltaTime;
    
    // クールダウン中は移動しない（待機アニメーションに移行）
    if (isInCooldown_) {
        if (elapsedTime_ >= duration_ + cooldownDuration_) {
            state_ = State::Completed;
        }
        return;
    }
    
    // 着地タイミング（アニメーションの80%経過時点）でヒットボックスを生成
    constexpr float landingTiming = 0.8f;
    
    if (!hitboxSpawned_ && elapsedTime_ >= duration_ * landingTiming) {
        // Start()で記録した着地地点にヒットボックスを生成
        boss_->SpawnJumpAttackHitbox(targetPosition_);
        hitboxSpawned_ = true;
    }
    
    // アニメーションが終わったらクールダウンに入る
    if (elapsedTime_ >= duration_ && !isInCooldown_) {
        isInCooldown_ = true;
        // 撃破されていない場合のみ待機アニメーションに切り替え
        if (!boss_->IsDestroyed()) {
            boss_->PlayIdleAnimation();
        }
    }
}

void JumpAttackAction::End() {
    BossAction::End();
    // 撃破されている場合は何もしない
    if (boss_->IsDestroyed()) {
        return;
    }
    // 終了時に待機アニメーションに戻す（念のため）
    if (isInCooldown_) {
        boss_->PlayIdleAnimation();
    }
}

// ===== PunchAttackAction =====
PunchAttackAction::PunchAttackAction(BossObject* boss)
    : boss_(boss) {}

void PunchAttackAction::Start() {
    BossAction::Start();
    elapsedTime_ = 0.0f;
    isInCooldown_ = false;
    boss_->PlayPunchAnimation();
    
    // 攻撃開始時にプレイヤーの方向を向く
    auto player = boss_->GetPlayer();
    if (player) {
        CoreEngine::Vector3 bossPos = boss_->GetTransform().translate;
        CoreEngine::Vector3 playerPos = player->GetTransform().translate;
        
        CoreEngine::Vector3 direction = CoreEngine::MathCore::Vector::Subtract(playerPos, bossPos);
        direction.y = 0.0f;
        
        float length = CoreEngine::MathCore::Vector::Length(direction);
        if (length > 0.0f) {
            direction = CoreEngine::MathCore::Vector::Multiply(1.0f / length, direction);
            
            // 修正: Z軸が前方向なので引数を逆に
            float targetAngle = std::atan2f(-direction.x, -direction.z);
            boss_->GetTransform().rotate.y = targetAngle;
        }
    }
}

void PunchAttackAction::Update(float deltaTime) {
    elapsedTime_ += deltaTime;
    
    // クールダウン中は移動しない（待機アニメーションに移行）
    if (isInCooldown_) {
        if (elapsedTime_ >= duration_ + cooldownDuration_) {
            state_ = State::Completed;
        }
        return;
    }
    
    // パンチ攻撃中に少し前方へ踏み込む（軽微な移動のみ）
    if (elapsedTime_ < duration_ * 0.2f) {  // アニメーションの20%の時間まで移動（30%→20%に変更）
        CoreEngine::Vector3 bossPos = boss_->GetTransform().translate;
        float angle = boss_->GetTransform().rotate.y;
        
        // 前方方向ベクトルを計算
        CoreEngine::Vector3 forward = {
            -std::sinf(angle),
            0.0f,
            -std::cosf(angle)
        };
        
        // 踏み込み速度を抑える（3.0→1.5に変更）
        constexpr float stepSpeed = 1.5f;
        
        // 移動量を計算
        CoreEngine::Vector3 velocity = CoreEngine::MathCore::Vector::Multiply(stepSpeed * deltaTime, forward);
        
        // 座標を更新
        boss_->GetTransform().translate = CoreEngine::MathCore::Vector::Add(bossPos, velocity);
    }
    
    // アニメーションが終わったらクールダウンに入る
    if (elapsedTime_ >= duration_ && !isInCooldown_) {
        isInCooldown_ = true;
        // 撃破されていない場合のみ待機アニメーションに切り替え
        if (!boss_->IsDestroyed()) {
            boss_->PlayIdleAnimation();
        }
    }
}

void PunchAttackAction::End() {
    BossAction::End();
    // 撃破されている場合は何もしない
    if (boss_->IsDestroyed()) {
        return;
    }
    // 終了時に待機アニメーションに戻す（念のため）
    if (isInCooldown_) {
        boss_->PlayIdleAnimation();
    }
}

// ===== DashAttackAction =====
DashAttackAction::DashAttackAction(BossObject* boss)
    : boss_(boss), dashDirection_({0.0f, 0.0f, 0.0f}) {}

void DashAttackAction::Start() {
    BossAction::Start();
    elapsedTime_ = 0.0f;
    currentPhase_ = Phase::Charging;
    
    // プレイヤー方向を向く
    auto player = boss_->GetPlayer();
    if (player) {
        CoreEngine::Vector3 bossPos = boss_->GetTransform().translate;
        CoreEngine::Vector3 playerPos = player->GetTransform().translate;
        
        // プレイヤーへの方向ベクトルを計算
        dashDirection_ = CoreEngine::MathCore::Vector::Subtract(playerPos, bossPos);
        dashDirection_.y = 0.0f;
        
        float length = CoreEngine::MathCore::Vector::Length(dashDirection_);
        if (length > 0.0f) {
            dashDirection_ = CoreEngine::MathCore::Vector::Multiply(1.0f / length, dashDirection_);
            
            // プレイヤーの方向を向く
            float targetAngle = std::atan2f(-dashDirection_.x, -dashDirection_.z);
            boss_->GetTransform().rotate.y = targetAngle;
        }
    }
    
    // 待機アニメーション（チャージ動作として）
    boss_->PlayIdleAnimation();
}

void DashAttackAction::Update(float deltaTime) {
    elapsedTime_ += deltaTime;
    
    switch (currentPhase_) {
    case Phase::Charging:
        // チャージ時間が経過したら突進フェーズへ
        if (elapsedTime_ >= chargeDuration_) {
            currentPhase_ = Phase::Dashing;
            elapsedTime_ = 0.0f;
            // 撃破されていない場合のみ突進アニメーションを再生
            if (!boss_->IsDestroyed()) {
                boss_->PlayDashAttackAnimation();
            }
        }
        break;
        
    case Phase::Dashing:
    {
        // 突進移動
        CoreEngine::Vector3 bossPos = boss_->GetTransform().translate;
        CoreEngine::Vector3 velocity = CoreEngine::MathCore::Vector::Multiply(dashSpeed_ * deltaTime, dashDirection_);
        boss_->GetTransform().translate = CoreEngine::MathCore::Vector::Add(bossPos, velocity);
        
        // 突進時間が経過したらクールダウンフェーズへ
        if (elapsedTime_ >= dashDuration_) {
            currentPhase_ = Phase::Cooldown;
            elapsedTime_ = 0.0f;
            // 撃破されていない場合のみ待機アニメーションに切り替え
            if (!boss_->IsDestroyed()) {
                boss_->PlayIdleAnimation();
            }
        }
        break;
    }
    
    case Phase::Cooldown:
        // クールダウン時間が経過したら完了
        if (elapsedTime_ >= cooldownDuration_) {
            state_ = State::Completed;
        }
        break;
    }
}

void DashAttackAction::End() {
    BossAction::End();
    // 撃破されている場合は何もしない
    if (boss_->IsDestroyed()) {
        return;
    }
    // 終了時に待機アニメーションに戻す
    boss_->PlayIdleAnimation();
}

// ===== CircularBarrageAction =====
CircularBarrageAction::CircularBarrageAction(BossObject* boss, int bulletCount)
    : boss_(boss), bulletCount_(bulletCount) {}

void CircularBarrageAction::Start() {
    BossAction::Start();
    elapsedTime_ = 0.0f;
    hasFired_ = false;
    
    // ジャンプアニメーションを再生
    boss_->PlayJumpAnimation();
}

void CircularBarrageAction::Update(float deltaTime) {
    elapsedTime_ += deltaTime;
    
    // ジャンプアニメーション終了直前（90%地点）に弾を発射
    // BossObject.hのjumpDuration_と同期
    constexpr float jumpAnimationDuration = 1.5f;
    constexpr float fireTimingRatio = 0.8f;  // アニメーションの90%地点で発射
    float fireTime = jumpAnimationDuration * fireTimingRatio;  // 1.35秒
    
    if (!hasFired_ && elapsedTime_ >= fireTime) {
        hasFired_ = true;
        
        // ボスの位置から全方向に弾を発射
        CoreEngine::Vector3 bossPos = boss_->GetTransform().translate;
        float PI = std::numbers::pi_v<float>;
        
        for (int i = 0; i < bulletCount_; ++i) {
            // 各弾の角度を計算（360度を均等に分割）
            float angle = (2.0f * PI * i) / bulletCount_;
            
            // 発射方向を計算
            CoreEngine::Vector3 direction = {
                std::cosf(angle),
                0.0f,
                std::sinf(angle)
            };
            
            // ボスに弾幕弾を生成させる
            boss_->SpawnBarrageBullet(bossPos, direction);
        }
    }
    
    // 弾発射後のクールダウン
    if (hasFired_ && elapsedTime_ >= jumpAnimationDuration + cooldownDuration_) {
        state_ = State::Completed;
    }
}

void CircularBarrageAction::End() {
    BossAction::End();
    // 撃破されている場合は何もしない
    if (boss_->IsDestroyed()) {
        return;
    }
}
