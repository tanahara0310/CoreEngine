#pragma once

#include "GameObject/GameObject.h"
#include "GameObject/Component/Transform/TransformComponent.h"
#include "GameObject/Component/Render/MeshRendererComponent.h"
#include "WorldTransform/WorldTransform.h"
#include "Graphics/Texture/TextureManager.h"
#include "Graphics/Model/Model.h"
#include "Graphics/Shader/ICustomShaderProvider.h"
#include "Graphics/Pipeline/CustomShaderPipeline.h"
#include "Math/Geometry/Shapes.h"
#include <string>
#include <memory>

#ifdef USE_IMGUI
#include "Graphics/Material/Debug/MaterialDebugUI.h"
#endif

namespace CoreEngine {

    class ModelManager;

    /// @brief 3D モデルを持つゲームオブジェクトの中間基底（移行用の薄いシム）。
    /// @details 実体は `TransformComponent` / `MeshRendererComponent` が持ち、`transform_` /
    ///          `model_` 等はその参照。ロードのボイラープレートと GetModelPath() 等のフックを提供する。
    class ModelGameObject : public GameObject {
    public:
        /// @brief コンストラクタ（コンポーネントをアタッチし、参照メンバを束縛する）
        /// @note **この 2 つのコンポーネントは取り外し禁止**（参照が宙に浮く）。
        ModelGameObject()
            : transformComponent_(AddComponent<TransformComponent>())
            , meshRenderer_(AddComponent<MeshRendererComponent>())
            , transform_(transformComponent_->Get())
            , model_(meshRenderer_->ModelPtr())
            , texture_(meshRenderer_->TextureRef())
            , textureName_(meshRenderer_->TextureNameRef())
            , blendMode_(meshRenderer_->BlendModeRef()) {}

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

        /// @brief ワールド空間でのスケールを取得
        /// @details ワールド行列の基底ベクトル長から求めるため、親の階層スケールも含む。
        ///          コライダーのサイズ／半径に乗る。
        Vector3 GetWorldScale() const override;

        /// @brief 衝突解決による移動を受け入れる
        /// @note `TransformComponent::ApplyWorldDelta()` へ委譲する。
        ///       **親を持つ場合もワールド量として正しく動く**（以前はワールド量を
        ///       ローカル translate へ素で加算していたため、親の回転・スケールを
        ///       無視していた ―― Phase 4 時点の制限。①で解消済み）。
        bool TryApplyCollisionPush(const Vector3& delta) override;

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

        /// @brief カスタムシェーダープロバイダーを登録する（`MeshRendererComponent` へ委譲）
        /// @param provider ICustomShaderProvider を実装したオブジェクト（所有権は移さない）
        /// @note OnInitialize() 内で this を渡すと Initialize() 完了時にカスタム PSO が構築される。
        void SetCustomShaderProvider(ICustomShaderProvider* provider) {
            meshRenderer_->SetCustomShaderProvider(provider);
        }

        // === コンポーネント（実体の所有者。コンストラクタでアタッチ済み・必ず非 nullptr） ===

        /// @brief トランスフォームを持つコンポーネント
        TransformComponent* transformComponent_ = nullptr;

        /// @brief メッシュ描画を持つコンポーネント
        MeshRendererComponent* meshRenderer_ = nullptr;

        // === 以下はすべて上のコンポーネントが持つ実体への「参照」 ===
        // 参照なのは移行のため（既存の `transform_.xxx` / `model_->xxx` を書き換えずに
        // 実体だけコンポーネント側へ移すため）。`ComponentHost` がコンポーネントを
        // 個別ヒープへ確保しているのでオブジェクトの寿命の間ずっと有効。

        /// @brief ワールドトランスフォーム（`transformComponent_` が持つ実体への参照）
        WorldTransform& transform_;

        /// @brief 3Dモデル（`meshRenderer_` が持つ実体への参照）
        std::unique_ptr<Model>& model_;

        /// @brief テクスチャハンドル（空の場合はモデル組み込みテクスチャを使用）
        TextureManager::LoadedTexture& texture_;

        /// @brief 現在適用中のテクスチャファイル名（表示・シリアライズ用）
        std::string& textureName_;

        /// @brief ブレンドモード（Render Properties タブで変更可能）
        BlendMode& blendMode_;

    public:
        BlendMode GetBlendMode() const override { return blendMode_; }
        void SetBlendMode(BlendMode blendMode) override { blendMode_ = blendMode; }

        /// @brief メッシュ描画コンポーネントを取得する
        /// @note 新しいコードはこちらを使う（`GetModel()` は移行用の互換 API）。
        MeshRendererComponent* GetMeshRenderer() const { return meshRenderer_; }

    protected:

        /// @brief カスタムシェーダー PSO を作り直す（シェーダー切替時に呼ぶ）
        /// @note 引数は互換のため受けるだけで使わない（コンポーネントが自分で引く）。
        void BuildCustomShaderPipelineIfNeeded(ID3D12Device* device = nullptr,
                                               ModelManager* modelMgr = nullptr);


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

        // カスタムシェーダーの実体（プロバイダと PSO）は MeshRendererComponent が持つ
    };

}  // namespace CoreEngine
