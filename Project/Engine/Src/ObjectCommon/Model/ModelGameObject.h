#pragma once

#include "ObjectCommon/GameObject.h"
#include "WorldTransform/WorldTransform.h"
#include "Graphics/Texture/TextureManager.h"
#include "Graphics/Model/Model.h"
#include <string>
#include <memory>

#ifdef _DEBUG
#include "Graphics/Material/Debug/MaterialDebugUI.h"
#endif

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

        /// @brief シャドウ描画処理
        void DrawShadow(ID3D12GraphicsCommandList* cmdList) override;

        /// @brief トランスフォームを取得
        WorldTransform& GetTransform() { return transform_; }
        /// @brief トランスフォームを取得（const版）
        const WorldTransform& GetTransform() const { return transform_; }

        /// @brief モデルを取得
        Model* GetModel() { return model_.get(); }
        /// @brief モデルを取得（const版）
        const Model* GetModel() const { return model_.get(); }

        /// @brief ワールド座標での位置を取得
        Vector3 GetWorldPosition() const override { return transform_.GetWorldPosition(); }

        /// @brief Transform + active を JSON に書き出す
        json OnSerialize() const override;

        /// @brief JSON から Transform + active を復元する
        void OnDeserialize(const json& j) override;

#ifdef _DEBUG
        /// @brief Transform + Material の ImGui UI を描画する（基底の DrawImGui から自動呼出し）
        bool DrawImGuiExtended() override;
#endif

    protected:
        // ========== テンプレートメソッドフック ==========

        /// @brief ロードするモデルのパスを返す（空文字列 = モデルなし）
        /// @note ファイル名のみ指定。ディレクトリは AssetDatabase が自動解決する。
        virtual std::string GetModelPath() const { return ""; }

        /// @brief テクスチャパスを返す（空文字列 = モデル組み込みテクスチャを使用）
        virtual std::string GetTexturePath() const { return ""; }

        /// @brief Initialize() の最後に呼ばれる
        virtual void OnInitialize() {}

        /// @brief Update() 内で TransferMatrix() の後に呼ばれる
        virtual void OnUpdate() {}

        /// @brief Draw() 内で model_->Draw() の後に呼ばれる
        virtual void OnDraw(const ICamera* camera) { (void)camera; }

        // === 共通描画リソース ===

        /// @brief 3Dモデル
        std::unique_ptr<Model> model_;

        /// @brief ワールドトランスフォーム
        WorldTransform transform_;

        /// @brief テクスチャハンドル（空の場合はモデル組み込みテクスチャを使用）
        TextureManager::LoadedTexture texture_;

#ifdef _DEBUG
        // ImGui 編集追跡用（操作前スナップショット）
        Vector3 imguiSnapTranslate_ = { 0.0f, 0.0f, 0.0f };
        Vector3 imguiSnapRotate_ = { 0.0f, 0.0f, 0.0f };
        Vector3 imguiSnapScale_ = { 1.0f, 1.0f, 1.0f };
        bool    imguiSnapActive_ = true;

        /// @brief マテリアルデバッグUI
        std::unique_ptr<MaterialDebugUI> materialDebugUI_;

        /// @brief マテリアルUIを描画するヘルパー
        bool DrawMaterialImGui();

        /// @brief Active 変更時に Undo/Redo コールバックを発火する
        void OnImGuiActiveChanged(bool prevActive) override;
#endif
    };

}  // namespace CoreEngine
