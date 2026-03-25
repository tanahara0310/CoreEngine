#pragma once

#include "ObjectCommon/GameObject.h"
#include <string>

namespace CoreEngine {

    /// @brief 3Dモデルを持つゲームオブジェクトの中間基底クラス
    ///
    /// Initialize / Update / Draw のボイラープレートを集約する。
    /// 派生クラスは GetModelPath() 等のフックをオーバーライドするだけでよい。
    ///
    /// 使用例:
    /// @code
    /// class FenceObject : public CoreEngine::ModelGameObject {
    /// protected:
    ///     std::string GetModelPath()   const override { return "fence.obj"; }
    ///     std::string GetTexturePath() const override { return "fence.png"; }
    /// public:
    ///     const char* GetObjectName()  const override { return "Fence"; }
    /// };
    /// @endcode
    class ModelGameObject : public GameObject {
    public:

        /// @brief 初期化処理（モデル・テクスチャ・トランスフォームのロードを自動実行）
        virtual void Initialize();

        /// @brief 更新処理（TransferMatrix → OnUpdate の順に実行）
        void Update() override;

        /// @brief 描画処理（model_->Draw → OnDraw の順に実行）
        void Draw(const ICamera* camera) override;

    protected:
        // ========== テンプレートメソッドフック ==========
        // 派生クラスはこれらをオーバーライドして固有処理を記述する

        /// @brief ロードするモデルのパスを返す（空文字列 = モデルなし）
        /// @note ファイル名のみ指定。ディレクトリは AssetDatabase が自動解決する。
        virtual std::string GetModelPath() const { return ""; }

        /// @brief テクスチャパスを返す（空文字列 = モデル組み込みテクスチャを使用）
        virtual std::string GetTexturePath() const { return ""; }

        /// @brief Initialize() の最後に呼ばれる
        /// @note 初期座標・スケール・コライダー設定などモデルロード後の初期化処理に使う
        virtual void OnInitialize() {}

        /// @brief Update() 内で TransferMatrix() の後に呼ばれる
        /// @note 毎フレームの追加処理（アニメーション更新など）に使う
        virtual void OnUpdate() {}

        /// @brief Draw() 内で model_->Draw() の後に呼ばれる
        /// @note 追加の描画処理が必要な場合にオーバーライドする
        virtual void OnDraw(const ICamera* camera) { (void)camera; }
    };

}  // namespace CoreEngine
