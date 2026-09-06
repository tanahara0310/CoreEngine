#pragma once

#include "ParticleModule.h"
#include "Math/MathCore.h"

namespace CoreEngine
{
/// @brief パーティクルと床（水平面）の当たり判定モジュール（Unity Collision Module の Planes 相当）
/// @details 高さ planeHeight の無限に広い水平面を床として、めり込んだ粒を押し戻し、
///          跳ね返り・摩擦・転がりを与える。落下速度が restSpeed 以下になると跳ねずに
///          着地し、そのあとは摩擦で横に滑りながら転がって止まる。
/// @note **既定は無効**。使う側が床の高さを決めて SetEnabled(true) すること
///       （有効が既定だと、床を意識していない既存のエフェクトが y = 0 の面に乗ってしまう）。
/// @note CPU 版（ParticleSystem）専用。位置を書き換えるモジュールなので、
///       ParticleUpdater の更新チェーンの最後に呼ばれる（ノイズより後）。
/// @note 床は 1 枚の水平面なので、地形の高さが場所によって変わる場合は合わない
///       （このモジュールは「一定の高さの床」だけを見る）。
class CollisionModule : public ParticleModule {
public:
    /// @brief 床との当たり判定の設定
    struct CollisionData {
        float planeHeight = 0.0f;       // 床のワールドY
        float contactOffset = 0.0f;     // 粒の原点から下面までの距離（この分だけ床から浮かせる）
        float bounce = 0.3f;            // 反発係数（0 で跳ねない、1 で勢いを保ったまま跳ねる）
        float friction = 3.0f;          // 接地中に横速度を削る強さ [1/秒]（0 で滑り続ける）
        float restSpeed = 0.6f;         // この落下速度以下なら跳ねずに着地させる [m/s]
        bool  roll = true;              // 接地中は進行方向へ転がす（回転速度を上書きする）
        float rollRadius = 0.2f;        // 転がり半径 [m]。小さいほど速く回る
        bool  useParticleScale = true;  // contactOffset と rollRadius に粒のスケールを掛ける
    };

    CollisionModule();
    ~CollisionModule() = default;

    /// @brief 当たり判定データを設定
    /// @param data 当たり判定データ
    void SetCollisionData(const CollisionData& data) { collisionData_ = data; }

    /// @brief 当たり判定データを取得
    /// @return 当たり判定データの参照
    const CollisionData& GetCollisionData() const { return collisionData_; }

    /// @brief 当たり判定データを取得（書き換え可能）
    /// @return 当たり判定データの参照
    CollisionData& GetCollisionData() { return collisionData_; }

    /// @brief パーティクルを床と衝突させる
    /// @param particle 対象のパーティクル
    /// @param deltaTime フレーム時間
    /// @note 床の上にいる粒には何もしない。
    void ApplyCollision(Particle& particle, float deltaTime);

#ifdef USE_IMGUI
    /// @brief ImGuiデバッグ表示
    /// @return UIに変更があった場合true
    bool ShowImGui() override;
#endif

private:
    CollisionData collisionData_;
};
}
