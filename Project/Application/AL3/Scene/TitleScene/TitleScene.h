#pragma once

#include "Scene/BaseScene.h"
#include "EngineSystem/EngineSystem.h"
#include "ObjectCommon/SpriteObject.h"
#include "../../GameObject/WallObject.h"
#include "Engine/Utility/Timer/GameTimer.h"
#include <memory>

// 前方宣言
class InputManager;

/// @brief タイトルシーンクラス
class TitleScene : public CoreEngine::BaseScene {
public:
    /// @brief 初期化
    void Initialize(CoreEngine::EngineSystem* engine) override;

    /// @brief 描画処理
    void Draw() override;

    /// @brief 解放
    void Finalize() override;

protected:
    /// @brief 更新
    void OnUpdate() override;

private:
    // ===== 初期化関連の関数 =====
    
    /// @brief カメラの初期化
    void InitializeCamera();
    
    /// @brief ライトの初期化
    void InitializeLight();
    
    /// @brief タイトルロゴの初期化
    void InitializeTitleLogo();
    
    /// @brief メニューボタンの初期化
    void InitializeMenuButtons();
    
    /// @brief 壁モデルの初期化
    void InitializeWallModel();
    
    /// @brief サウンドの初期化
    void InitializeSound();
    
    // ===== 更新関連の関数 =====
    
    /// @brief ロゴフェードイン処理
    /// @param deltaTime デルタタイム
    /// @return 処理を続行する場合true
    bool UpdateLogoFadeIn(float deltaTime);
    
    /// @brief メニュー表示遅延処理
    /// @param deltaTime デルタタイム
    /// @return 処理を続行する場合true
    bool UpdateMenuDelay(float deltaTime);
    
    /// @brief メニューボタンアニメーション処理
    /// @param deltaTime デルタタイム
    /// @return 処理を続行する場合true
    bool UpdateMenuButtonAnimation(float deltaTime);
    
    /// @brief メニュー選択入力処理
    /// @param keyboard キーボード入力
    /// @param inputManager 入力マネージャー
    /// @param deltaTime デルタタイム
    void UpdateMenuInput(CoreEngine::KeyboardInput* keyboard, CoreEngine::InputManager* inputManager, float deltaTime);
    
    /// @brief メニューボタンのスケールアニメーション処理
    /// @param deltaTime デルタタイム
    void UpdateMenuScaleAnimation(float deltaTime);
    
    /// @brief メニュー決定入力処理
    /// @param keyboard キーボード入力
    /// @param inputManager 入力マネージャー
    void UpdateMenuDecision(CoreEngine::KeyboardInput* keyboard, CoreEngine::InputManager* inputManager);

private:
    // タイトルスプライト
    CoreEngine::SpriteObject* titleSprite_ = nullptr;

    // メニュー選択用スプライト
    CoreEngine::SpriteObject* startSprite_ = nullptr;
    CoreEngine::SpriteObject* exitSprite_ = nullptr;

    // 壁モデル
    WallObject* wallObject_ = nullptr;

    // メニュー選択状態 (0: Start, 1: Exit)
    int selectedMenuIndex_ = 0;

    // アニメーション用
    float animationTime_ = 0.0f;
    const float kAnimationSpeed = 3.0f;
    const float kBaseScale = 0.7f;
    const float kMinScale = 0.63f;  // 0.7 * 0.9
    const float kMaxScale = 0.77f;  // 0.7 * 1.1

    // ロゴフェードイン用
    CoreEngine::GameTimer logoFadeTimer_;  // ロゴフェードイン用タイマー
    const float kLogoFadeDuration = 2.0f;  // フェードイン時間（秒）

    // メニューボタン演出用
    CoreEngine::GameTimer menuDelayTimer_;    // メニュー表示遅延用タイマー
    CoreEngine::GameTimer startButtonTimer_;  // スタートボタンアニメーション用タイマー
    CoreEngine::GameTimer exitButtonTimer_;   // やめるボタンアニメーション用タイマー
    const float kMenuAppearDelay = 0.3f;  // ロゴフェード完了後の遅延時間（秒）
    const float kMenuAnimDuration = 0.6f;  // メニューボタンのアニメーション時間（秒）
    const float kStartButtonStartX = -640.0f;  // スタートボタンの初期X座標（画面左外）
    const float kExitButtonStartX = 640.0f;   // やめるボタンの初期X座標（画面右外）
    const float kButtonTargetX = 0.0f;  // ボタンの目標X座標（中央）
    const float kStartButtonTargetY = -116.0f;  // スタートボタンの目標Y座標
    const float kExitButtonTargetY = -250.0f;  // やめるボタンの目標Y座標

    // サウンドリソース
    std::unique_ptr<CoreEngine::SoundManager::SoundResource> selectSE_;  // 選択時SE
    std::unique_ptr<CoreEngine::SoundManager::SoundResource> decideSE_;  // 決定時SE
    std::unique_ptr<CoreEngine::SoundManager::SoundResource> titleBGM_;  // タイトルBGM
};
