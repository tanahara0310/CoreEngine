#pragma once

#include "GameObject/Component/IComponent.h"
#include "GameObject/Component/TransformComponent.h"
#include "Graphics/Model/Model.h"
#include "Graphics/Pipeline/PipelineStateManager.h"
#include "Graphics/Render/DrawViewInfo.h"
#include "Graphics/Texture/TextureManager.h"
#include "Math/Geometry/Shapes.h"

#include <memory>
#include <string>

namespace CoreEngine
{
/// @brief メッシュを描画するコンポーネント
///
/// @details **これが `dynamic_cast<ModelGameObject*>` の置き換え先**。
///          「シーン内でメッシュを持つものを全部集める」という問い合わせは、
///          具象クラスへのダウンキャストではなく
///          @code
///          objectManager->ForEachComponent<MeshRendererComponent>(
///              [](MeshRendererComponent& renderer, GameObject& owner) { ... });
///          @endcode
///          で書く。以前は RT の TLAS 構築・IBL 一括適用・ピッキング・エディタが
///          それぞれ `dynamic_cast<ModelGameObject*>` していた。
///
///          **意図的に 1 コンポーネントにまとめてある**: 計画では
///          `MeshRenderer` / `Material` / `CustomShader` の 3 つに割る想定だったが、
///          `Material` 側の実体は `Model` が持つ `MaterialInstance` であり、
///          このコンポーネントに残るのは「上書き用テクスチャハンドル 1 個」だけになる。
///          `CustomShader` も利用者が数個しかない。**2 人目の利用者が現れるまで割らない**
///          （用も無く分割して継承ツリーを深くしたのが元の問題だったので、
///          同じ失敗をコンポーネントで繰り返さない）。
///
///          **移行方針**: `ModelGameObject` は `model_` / `texture_` / `textureName_` /
///          `blendMode_` を「このコンポーネントが持つ実体への参照」として公開し続ける。
///          そのため派生クラス（`AnimatedModelObject` / `PrimitiveGameObject` /
///          `ModelObject` / `SphereObject` 等）の `model_->...` という記述は変わらない。
class MeshRendererComponent : public IComponent {
public:
    const char* GetTypeName() const override { return "MeshRenderer"; }

#ifdef USE_IMGUI
    const char* GetInspectorName() const override { return "メッシュ描画"; }
#endif

    // ===== モデル =====

    /// @brief モデルの所有権を持つスマートポインタへの参照
    /// @note `ModelGameObject` がこれを `model_` として公開するため参照を返す。
    std::unique_ptr<Model>& ModelPtr() { return model_; }

    Model* GetModel() { return model_.get(); }
    const Model* GetModel() const { return model_.get(); }

    bool HasModel() const { return model_ != nullptr; }

    // ===== テクスチャ（モデル組み込みを上書きする場合） =====

    TextureManager::LoadedTexture& TextureRef() { return texture_; }
    const TextureManager::LoadedTexture& GetTexture() const { return texture_; }

    std::string& TextureNameRef() { return textureName_; }
    const std::string& GetTextureName() const { return textureName_; }

    // ===== ブレンドモード =====

    BlendMode& BlendModeRef() { return blendMode_; }
    BlendMode GetBlendMode() const { return blendMode_; }
    void SetBlendMode(BlendMode mode) { blendMode_ = mode; }

    // ===== 描画 =====

    /// @brief 視錐台カリングを行い、通過したらモデルを描画する
    /// @return 実際に描画したら true（カリングされた場合 false）
    /// @note カリングの判定内容とデバッグトグルは `ModelVisibility`（Culling 層）が持つ。
    ///       視錐台は `ViewInfo` 構築時に 1 回だけ抽出済み。
    bool DrawIfVisible(const DrawViewInfo& view);

    /// @brief ワールド空間の AABB（視錐台カリング / Hi-Z / ピッキングの事前棄却用）
    /// @return モデルが無ければ無効な AABB
    BoundingBox GetWorldBoundingBox() const;

    /// @brief 兄弟の TransformComponent（描画に必要なワールド行列の出どころ）
    /// @return アタッチされていなければ nullptr
    TransformComponent* GetTransformComponent() const { return transform_; }

    // ===== ライフサイクル =====

    /// @brief 兄弟の TransformComponent を捕まえる
    /// @note `Start()` ではなく `Awake()` では取れない（自分が先にアタッチされた場合、
    ///       Transform がまだ居ない）。`ModelGameObject` は Transform を先に
    ///       アタッチするので実際には Awake でも取れるが、順序に依存しないよう
    ///       Start で取る。`DrawIfVisible` は遅延取得もするので初回描画前でも安全。
    void Start() override { transform_ = Sibling<TransformComponent>(); }

private:
    /// @brief transform_ が未取得なら取りに行く（Start より前に描画された場合の保険）
    TransformComponent* ResolveTransform() const;

    std::unique_ptr<Model> model_;
    TextureManager::LoadedTexture texture_{};
    std::string textureName_;
    BlendMode blendMode_ = BlendMode::kBlendModeNone;

    mutable TransformComponent* transform_ = nullptr;
};
}
