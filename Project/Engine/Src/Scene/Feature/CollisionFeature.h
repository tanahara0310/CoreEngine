#pragma once

#include "ISceneFeature.h"
#include "Collision/CollisionWorld.h"
#include "Collision/CollisionConfig.h"
#include "Collision/Debug/ColliderDebugRenderer.h"

#include <cstdint>
#include <limits>
#include <memory>

namespace CoreEngine
{
    class GameObjectManager;

    /// @brief シーンのコリジョン判定を管理する Feature。
    /// @details PostObjectUpdate（GameObject 更新後）で毎フレーム 収集 → 判定 を実行する。
    ///          コライダーのワイヤ表示（ILineSource）もここが所有する。
    class CollisionFeature : public ISceneFeature {
    public:
        const char* GetName() const override { return "Collision"; }

        /// @brief シーン初期化時（デバッグ描画オブジェクトの生成）
        void Initialize(SceneContext& ctx) override;

        void Update(SceneContext& ctx, SceneUpdatePhase phase) override;

        /// @brief シーン終了時（コライダー登録と衝突履歴を破棄する）
        /// @note GameObject より先に消しておく。生ポインタの履歴を残したまま
        ///       オブジェクトが解放される状態を作らないため。
        void Finalize(SceneContext& ctx) override;

        /// @brief レイヤー間の衝突判定を有効/無効に設定
        void SetCollisionEnabled(CollisionLayer a, CollisionLayer b, bool enable) {
            collisionConfig_.SetCollisionEnabled(a, b, enable);
        }

        /// @brief 衝突ワールドを取得する（レイキャスト等の問い合わせ用）
        /// @note 登録は判定のときのもの。判定の後・フレーム末の後始末の前に問い合わせる用。
        ///       ゲームの更新（Update）から問い合わせるときは GetQueryWorld を使う。
        CollisionWorld& GetWorld() { return collisionWorld_; }
        const CollisionWorld& GetWorld() const { return collisionWorld_; }

        /// @brief 問い合わせ用の衝突ワールド（このフレームにまだ登録していなければ、生きているコライダーで登録し直す）
        /// @details 消えたオブジェクトのコライダーはフレーム末に解放されるので、前のフレームの登録のまま
        ///          問い合わせると、解放済みのコライダーを読んでしまう。
        CollisionWorld& GetQueryWorld(GameObjectManager& manager);

        /// @brief レイヤー間の衝突マトリクスを取得する（エディタ用）
        CollisionConfig& GetConfig() { return collisionConfig_; }

    private:
        /// @brief CVar の値をワールドへ反映する（ブロードフェーズの選択など）
        void ApplyCVars();

        CollisionConfig collisionConfig_;
        CollisionWorld  collisionWorld_{ &collisionConfig_ };

        /// 衝突ワールドへコライダーを登録したフレームの番号
        uint64_t registeredFrame_ = (std::numeric_limits<uint64_t>::max)();

        /// コライダーのワイヤ表示（この Feature が所有。Line パスにはポインタ登録するだけ）
        std::unique_ptr<ColliderDebugRenderer> debugRenderer_;
    };
}
