#pragma once

#include "ObjectCommon/Model/ModelGameObject.h"
#include <string>

namespace CoreEngine {

    /// @brief スケルトンアニメーション付き 3D モデルの中間基底クラス
    ///
    /// ModelGameObject をさらに特化し、アニメーションのロード・更新を自動化する。
    /// 派生クラスは GetModelPath() / GetAnimationName() をオーバーライドするだけでよい。
    ///
    /// 使用例:
    /// @code
    /// class WalkModelObject : public CoreEngine::AnimatedModelObject {
    /// protected:
    ///     std::string GetModelPath()     const override { return "walk.gltf"; }
    ///     std::string GetAnimationName() const override { return "walkAnimation"; }
    /// public:
    ///     const char* GetObjectName()    const override { return "WalkModel"; }
    /// };
    /// @endcode
    class AnimatedModelObject : public ModelGameObject {
    public:
        /// @brief 初期化処理（アニメーションロード → スケルトンモデル生成）
        /// @note ModelGameObject::Initialize() の代わりに呼ばれる
        void Initialize() override;

        /// @brief スキニングモデル用の描画パスタイプを返す
        RenderPassType GetRenderPassType() const override { return RenderPassType::SkinnedModel; }

    protected:
        // ========== テンプレートメソッドフック ==========

        /// @brief アニメーション識別名を返す（必須オーバーライド）
        virtual std::string GetAnimationName() const = 0;

        /// @brief アニメーションファイル名を返す（省略時はモデルファイルと同じ）
        virtual std::string GetAnimationFile() const { return GetModelPath(); }

        /// @brief Update() 内で TransferMatrix() の後に呼ばれる（アニメーション更新を含む）
        void OnUpdate() override;
    };

}  // namespace CoreEngine
