#pragma once

#include "Engine/ObjectCommon/GameObject.h"
#include "Engine/Utility/Timer/GameTimer.h"
#include "Engine/Math/Vector/Vector3.h"
#include "Engine/Collider/SphereCollider.h"

/// @brief 弾オブジェクト
class BulletObject : public CoreEngine::GameObject {
public:
    /// @brief 初期化処理
    /// @param model モデルポインタ
    /// @param texture テクスチャハンドル
    /// @param position 初期位置
    /// @param direction 発射方向
    void Initialize(std::unique_ptr<CoreEngine::Model> model, CoreEngine::TextureManager::LoadedTexture texture, const CoreEngine::Vector3& position, const CoreEngine::Vector3& direction);

    /// @brief 更新処理
    void Update() override;

    /// @brief 描画処理
    /// @param camera カメラ
    void Draw(const CoreEngine::ICamera* camera) override;

    /// @brief 描画パスタイプを取得
    /// @return 描画パスタイプ
    CoreEngine::RenderPassType GetRenderPassType() const override { return CoreEngine::RenderPassType::Model; }

    /// @brief 衝突開始イベント
    /// @param other 衝突相手のオブジェクト
    void OnCollisionEnter(GameObject* other) override;

    /// @brief コライダーを取得
    /// @return コライダーへのポインタ
    CoreEngine::SphereCollider* GetCollider() const { return collider_.get(); }

    /// @brief ヒット時のコールバックを設定
    /// @param callback ヒット時に呼ばれる関数
    void SetOnHitCallback(std::function<void(const CoreEngine::Vector3&)> callback) { onHitCallback_ = callback; }

    /// @brief 弾が範囲外に出たかチェック
    /// @return 範囲外に出た場合true
    bool IsOutOfBounds() const;

#ifdef _DEBUG
    /// @brief オブジェクト名を取得
    /// @return オブジェクト名
    const char* GetObjectName() const override { return "Bullet"; }
#endif

    /// @brief トランスフォームを取得
    CoreEngine::WorldTransform& GetTransform() { return transform_; }

    /// @brief モデルを取得
    CoreEngine::Model* GetModel() { return model_.get(); }

private:
    CoreEngine::Vector3 velocity_;                          // 移動速度ベクトル
    float speed_ = 20.0f;                       // 弾の速度
    CoreEngine::GameTimer lifetimeTimer_;                   // 寿命タイマー
    float lifetime_ = 3.0f;                     // 寿命（秒）
    std::unique_ptr<CoreEngine::SphereCollider> collider_;  // 球形コライダー
    std::function<void(const CoreEngine::Vector3&)> onHitCallback_; // ヒット時コールバック

    // アリーナ範囲制限用の定数
    static constexpr float ARENA_SIZE = 40.0f;  // アリーナの半径（30 → 40に拡大）
    static constexpr float WALL_MODEL_SIZE = 23.0f;  // 壁モデルのサイズ
    static constexpr float BOUNDS_LIMIT = ARENA_SIZE - (WALL_MODEL_SIZE * 0.5f);  // 範囲制限
};
