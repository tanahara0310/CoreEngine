#pragma once

#include "ObjectCommon/GameObject.h"

#ifdef _DEBUG
#include "Graphics/Material/Debug/MaterialDebugUI.h"
#endif

/// @brief AnimatedCubeモデルオブジェクト
class AnimatedCubeObject : public CoreEngine::GameObject {
public:
    /// @brief 初期化処理
    /// @param engine エンジンシステムへのポインタ
    void Initialize();

    /// @brief 更新処理
    void Update() override;

    /// @brief 描画処理
    /// @param camera カメラ
    void Draw(const CoreEngine::ICamera* camera) override;

#ifdef _DEBUG
    /// @brief ImGui拡張UI描画（アニメーション制御を追加）
    /// @return ImGuiで変更があった場合 true
    bool DrawImGuiExtended() override;
#endif

    /// @brief オブジェクト名を取得
    const char* GetObjectName() const override { return "AnimatedCube"; }

    /// @brief アニメーション速度を設定
    /// @param speed 再生速度（1.0が通常速度）
    void SetAnimationSpeed(float speed);

    /// @brief アニメーション速度を取得
    /// @return 再生速度
    float GetAnimationSpeed() const;

    /// @brief アニメーションをリセット
    void ResetAnimation();

    /// @brief アニメーション時刻を取得
    /// @return 現在の再生時刻（秒）
    float GetAnimationTime() const;

    /// @brief アニメーションが終了したか確認
    /// @return アニメーションが終了していればtrue
    bool IsAnimationFinished() const;

    /// @brief トランスフォームを取得
    CoreEngine::WorldTransform& GetTransform() { return transform_; }

    /// @brief モデルを取得
    CoreEngine::Model* GetModel() { return model_.get(); }

private:
    float deltaTime_ = 1.0f / 60.0f;  //!< デルタタイム（60FPS想定）
#ifdef _DEBUG
    std::unique_ptr<CoreEngine::MaterialDebugUI> materialDebugUI_;
#endif
};

