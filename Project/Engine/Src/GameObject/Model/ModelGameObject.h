#pragma once

#include "GameObject/GameObject.h"
#include "WorldTransform/WorldTransform.h"
#include "Graphics/Texture/TextureManager.h"
#include "Graphics/Model/Model.h"
#include "Graphics/Shader/ICustomShaderProvider.h"
#include "Graphics/Pipeline/CustomShaderPipeline.h"
#include "Math/BoundingBox.h"
#include <string>
#include <memory>

#ifdef USE_IMGUI
#include "Graphics/Material/Debug/MaterialDebugUI.h"
#endif

namespace CoreEngine {

    class ModelManager;

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

        /// @brief 描画処理（RenderGraph を経由しない直接呼び出し用のレガシー経路）
        /// @details GameView・Forward パス扱いの DrawViewInfo を組み立てて本経路へ委譲する。
        void Draw(const Camera* camera) override;

        /// @brief ビュー/パス情報つき描画処理（本経路。カリング → model_->Draw → OnDraw）
        void Draw(const DrawViewInfo& view) override;

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

        /// @brief ワールド空間のAABBを取得（視錐台カリング用）
        /// @return ワールド変換後のバウンディングボックス
        BoundingBox GetWorldBoundingBox() const;

        /// @brief Transform + active を JSON に書き出す
        json OnSerialize() const override;

        /// @brief JSON から Transform + active を復元する
        void OnDeserialize(const json& j) override;

#ifdef USE_IMGUI
        /// @brief CollapsingScope なしで直接プロパティを表示する
        bool DrawImGui() override;

        /// @brief インスペクタータブ定義を返す
        int GetInspectorTabs(InspectorTabDef* outTabs, int maxTabs) const override;

        /// @brief 指定タブのコンテンツを描画する
        bool DrawInspectorTabContent(int tabIndex) override;
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
        virtual void OnDraw(const Camera* camera) { (void)camera; }

        /// @brief カスタムシェーダープロバイダーを登録する
        /// OnInitialize() 内で this を渡すことで Initialize() 完了後にカスタム PSO が構築される。
        /// @param provider ICustomShaderProvider を実装したオブジェクト（所有権は移さない）
        void SetCustomShaderProvider(ICustomShaderProvider* provider) { customShaderProvider_ = provider; }

        // === 共通描画リソース ===

        /// @brief 3Dモデル
        std::unique_ptr<Model> model_;

        /// @brief ワールドトランスフォーム
        WorldTransform transform_;

        /// @brief テクスチャハンドル（空の場合はモデル組み込みテクスチャを使用）
        TextureManager::LoadedTexture texture_;

        /// @brief 現在適用中のテクスチャファイル名（表示・シリアライズ用）
        std::string textureName_;

        /// @brief ブレンドモード（Render Properties タブで変更可能）
        BlendMode blendMode_ = BlendMode::kBlendModeNone;

    public:
        BlendMode GetBlendMode() const override { return blendMode_; }
        void SetBlendMode(BlendMode blendMode) override { blendMode_ = blendMode; }

    protected:

        /// @brief カスタムシェーダープロバイダーが設定されている場合に PSO を構築する
        /// ModelGameObject::Initialize() および PrimitiveGameObject::Initialize() の
        /// OnInitialize() 呼び出し直後に実行される共通処理。
        void BuildCustomShaderPipelineIfNeeded(ID3D12Device* device, ModelManager* modelMgr);


#ifdef USE_IMGUI
        // ImGui 編集追跡用（操作前スナップショット）
        Vector3 imguiSnapTranslate_ = { 0.0f, 0.0f, 0.0f };
        Vector3 imguiSnapRotate_ = { 0.0f, 0.0f, 0.0f };
        Vector3 imguiSnapScale_ = { 1.0f, 1.0f, 1.0f };
        bool    imguiSnapActive_ = true;

        /// マテリアルデバッグUI
        std::unique_ptr<MaterialDebugUI> materialDebugUI_;

        /// マテリアルUIを描画するヘルパー
        bool DrawMaterialImGui();

        /// @brief Transform セクション（Object Properties タブ）を描画するヘルパー
        bool DrawTransformSection();

        /// @brief Render Properties タブを描画するヘルパー
        bool DrawRenderSection();

        /// @brief Texture Properties タブを描画するヘルパー
        bool DrawTextureSection();

        /// @brief Active 変更時に Undo/Redo コールバックを発火する
        void OnImGuiActiveChanged(bool prevActive) override;
#endif

    private:

        /// @brief カスタムシェーダープロバイダー（nullptr = 既定シェーダーを使用）
        ICustomShaderProvider* customShaderProvider_ = nullptr;

        /// @brief カスタムシェーダー用 PSO を管理するコンポーネント
        std::unique_ptr<CustomShaderPipeline> customShaderPipeline_;

    };

}  // namespace CoreEngine
