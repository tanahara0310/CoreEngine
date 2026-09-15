#pragma once

#include "GameObject/Component/Core/IComponent.h"
#include "GameObject/Component/Render/IRenderableComponent.h"
#include "GameObject/Component/Transform/TransformComponent.h"
#include "Graphics/Asset/AssetRef.h"
#include "Graphics/Model/Model.h"
#include "Graphics/Pipeline/CustomShaderPipeline.h"
#include "Graphics/Primitive/IPrimitiveMeshGenerator.h"
#include "Graphics/Texture/TextureManager.h"
#include "Math/Geometry/Shapes.h"
#include "Reflection/Reflect.h"

#include <memory>
#include <optional>
#include <string>

#ifdef USE_IMGUI
#include "Graphics/Material/Debug/MaterialDebugUI.h"
#endif

namespace CoreEngine
{
class ICustomShaderProvider;
class CustomShaderPipeline;

/// @brief メッシュの所有と描画を担うコンポーネント（ロード・カリング・AABB・ブレンド込み）。
/// @details メッシュの出どころはコンストラクタ引数で指定し、`Awake()` で自分でロードする。
///          そのため継承クラスを用意する必要がなく、`AddComponent<MeshRendererComponent>("x.obj")`
///          だけでモデルが出る。`TransformComponent` は無ければ自動で足す。
class MeshRendererComponent : public IComponent, public IRenderableComponent {
public:
    /// @brief メッシュを持たない状態で作る（後から Set*Mesh で指定）
    MeshRendererComponent() = default;

    /// @brief モデルファイルから静的メッシュを作る
    explicit MeshRendererComponent(std::string modelPath);

    /// @brief 手続き的メッシュ（プリミティブ）を作る
    explicit MeshRendererComponent(std::unique_ptr<IPrimitiveMeshGenerator> generator)
        : generator_(std::move(generator)), source_(Source::Primitive) {}

    ~MeshRendererComponent() override;

    const char* GetTypeName() const override { return "MeshRenderer"; }

    /// @brief トランスフォームを使う
    bool RequiresComponent(const IComponent& other) const override
    {
        return dynamic_cast<const TransformComponent*>(&other) != nullptr;
    }

    REFLECT_DECLARE(MeshRendererComponent)

#ifdef USE_IMGUI
    const char* GetInspectorName() const override { return "メッシュ描画"; }

    const char* GetInspectorIcon() const override { return "scene.png"; }

    void GetInspectorIconColor(float* outRgba) const override
    {
        outRgba[0] = 0.34f; outRgba[1] = 0.67f; outRgba[2] = 0.88f; outRgba[3] = 1.0f;
    }

    /// @brief メッシュの取得元・テクスチャ・描画設定・マテリアルの編集 UI
    /// @return 値が変更されたら true
    bool DrawInspector() override;
#endif

    // ===== シリアライズ =====

    /// @brief テクスチャ・ブレンド・マテリアルのうち、既定から変えたものを書き出す
    /// @details `texture` は `{"guid", "path"}`、`blendMode` は番号、`materials` はスロットごとの配列。
    ///          マテリアルは、どれか 1 つのスロットでもモデルの既定と違うときだけ全スロットを書く。
    json OnSerialize() const override;

    /// @brief テクスチャ・ブレンド・マテリアルを読む
    /// @note マテリアルはメッシュを読み込んだ後に当てる（まだ無ければ読み込むまで控える）。
    void OnDeserialize(const json& j) override;

    // ===== メッシュの指定（Awake より前に呼ぶ） =====

    /// @brief モデルファイル（静的メッシュ）を指定する
    void SetModelFile(std::string modelPath);

    /// @brief スケルトン付きモデルを指定する（`AnimatorComponent` と併用する）
    /// @param initialClipName 最初に再生するクリップの識別名
    void SetSkinnedModelFile(std::string modelPath, std::string initialClipName);

    /// @brief 手続き的メッシュを指定する
    void SetPrimitive(std::unique_ptr<IPrimitiveMeshGenerator> generator);

    /// @brief 指しているモデルファイル（型記述子とやり取りする値）
    Reflection::AssetRefValue GetModelAsset() const { return modelAsset_.GetValue(); }

    /// @brief モデルファイルを指し直す（Awake 済みなら読み込み直す）
    /// @note 何も指さない値を渡すと、ファイルから作ったメッシュを外す。
    void SetModelAsset(const Reflection::AssetRefValue& value);

    /// @brief 上書きテクスチャを指定する（空ならモデル組み込みを使う）
    void SetTexture(std::string texturePath);

    /// @brief カスタムシェーダーを使う場合のプロバイダを登録する（所有権は移さない）
    void SetCustomShaderProvider(ICustomShaderProvider* provider) { customShaderProvider_ = provider; }

    /// @brief 描画パスを明示指定する（既定はメッシュ種別から自動判定）
    void SetRenderPassType(RenderPassType passType) { passTypeOverride_ = passType; }

    // ===== IRenderableComponent =====

    RenderPassType GetRenderPassType() const override;
    BlendMode GetBlendMode() const override { return blendMode_; }
    void SetBlendMode(BlendMode mode) override { blendMode_ = mode; }
    void Render(const DrawViewInfo& view) override { DrawIfVisible(view); }

    // ===== ライフサイクル =====

    /// @brief 指定されたメッシュとテクスチャをロードし、Transform を確保する
    void Awake() override;

    /// @brief 指定内容でメッシュを作り直す（`Awake()` より後に Set*Mesh した場合に呼ぶ）
    void ReloadFromSpec();

    /// @brief カスタムシェーダー PSO を作り直す（シェーダー切替時。水面の FFT 切替など）
    void RebuildCustomShaderPipeline() { BuildCustomShaderPipelineIfNeeded(); }

    // ===== アクセス =====

    Model* GetModel() { return model_.get(); }
    const Model* GetModel() const { return model_.get(); }
    bool HasModel() const { return model_ != nullptr; }
    const std::string& GetModelPath() const { return modelPath_; }

    const std::string& GetTextureName() const { return textureName_; }

    /// @brief 視錐台カリングを行い、通過したらモデルを描画する
    /// @return 実際に描画したら true
    bool DrawIfVisible(const DrawViewInfo& view);

    /// @brief ワールド空間の AABB（カリング / Hi-Z / ピッキングの事前棄却用）
    BoundingBox GetWorldBoundingBox() const;

    /// @brief 兄弟の TransformComponent（描画に必要なワールド行列の出どころ）
    TransformComponent* GetTransformComponent() const { return ResolveTransform(); }

private:
    /// @brief メッシュの出どころ
    enum class Source { None, ModelFile, SkinnedModelFile, Primitive };

    /// @brief transform_ が未取得なら取りに行く
    TransformComponent* ResolveTransform() const;

    /// @brief 指定に従ってモデルを生成する
    void LoadMesh();

    /// @brief 控えたマテリアルをモデルの各スロットへ当てる（モデルの既定と同じスロットは当てない）
    void ApplyPendingMaterials();

    /// @brief カスタムシェーダー用 PSO を構築する（プロバイダ登録時のみ）
    void BuildCustomShaderPipelineIfNeeded();

    std::unique_ptr<Model> model_;
    std::unique_ptr<IPrimitiveMeshGenerator> generator_;
    std::string modelPath_;
    std::string initialClipName_;
    Source source_ = Source::None;
    AssetRef<ModelAsset> modelAsset_;  ///< 保存とインスペクタに出すモデルファイル
    bool awoken_ = false;              ///< Awake を済ませたか

    TextureManager::LoadedTexture texture_{};
    std::string textureName_;
    std::string pendingTexturePath_;
    BlendMode blendMode_ = BlendMode::kBlendModeNone;

    json pendingMaterials_;  ///< メッシュを読み込むまで控えるマテリアル（`materials` の配列）

    std::optional<RenderPassType> passTypeOverride_;

    ICustomShaderProvider* customShaderProvider_ = nullptr;
    // CustomShaderPipelineCache から得る共有パイプライン
    // （同一シェーダー＋同一設定の他コンポーネントと共有される）
    std::shared_ptr<CustomShaderPipeline> customShaderPipeline_;

    mutable TransformComponent* transform_ = nullptr;

#ifdef USE_IMGUI
    std::unique_ptr<MaterialDebugUI> materialDebugUI_;
#endif
};
}
