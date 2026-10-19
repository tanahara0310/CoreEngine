#pragma once

#include "GameObject/Component/Core/IComponent.h"
#include "GameObject/Component/Render/IRenderableComponent.h"
#include "Graphics/Asset/AssetRef.h"
#include "Graphics/Material/ShaderMaterialSlot.h"
#include "Graphics/Material/UIMaterialInstance.h"
#include "Graphics/Model/VertexData.h"
#include "Graphics/Texture/TextureManager.h"
#include "Math/Vector/Vector2.h"
#include "Math/Vector/Vector4.h"
#include "Reflection/Reflect.h"

#include <d3d12.h>
#include <array>
#include <functional>
#include <memory>
#include <string>
#include <wrl.h>

namespace CoreEngine
{
    class RectTransformComponent;
    class UIRenderer;
    class UploadRing;

    /// @brief テクスチャを貼った矩形を UI パスで描くコンポーネント
    /// @details 位置・大きさ・回転は兄弟の `RectTransformComponent` から取る（無ければ Awake で足す）。
    ///          テクスチャを指していなければ白い矩形を描く。
    ///          テクスチャを横と縦のコマ数で格子に分け、そのうち 1 コマを描く。
    class UIImageComponent : public IComponent, public IRenderableComponent
    {
    public:
        UIImageComponent() = default;
        ~UIImageComponent() override;

        const char* GetTypeName() const override { return "UIImage"; }

        REFLECT_BEGIN(UIImageComponent, "UI 画像")
            REFLECT_ACCESSOR("texture", "テクスチャ", GetTextureAsset, SetTextureAsset,
                p.assetType = ::CoreEngine::AssetType::Texture)
            REFLECT_ACCESSOR("color", "色", GetColor, SetColor,
                p.type = ::CoreEngine::Reflection::PropertyType::Color)
            REFLECT_ACCESSOR("material", "マテリアル", GetMaterialAsset, SetMaterialAsset,
                p.assetType = ::CoreEngine::AssetType::Material,
                p.tooltip = "描き方を決めるシェーダと値（.mat）。指さなければテクスチャ × 色で描く")
            REFLECT_ACCESSOR("columns", "横のコマ数", GetColumns, SetColumns,
                p.range = Range(1.0f, 256.0f, 0.05f), p.tooltip = "テクスチャを横に何コマへ分けるか（スプライトシート用）")
            REFLECT_ACCESSOR("rows", "縦のコマ数", GetRows, SetRows,
                p.range = Range(1.0f, 256.0f, 0.05f), p.tooltip = "テクスチャを縦に何コマへ分けるか")
            REFLECT_ACCESSOR("frame", "コマ", GetFrame, SetFrame,
                p.range = Range(0.0f, 65535.0f, 0.05f), p.tooltip = "描くコマ。左上から右へ、行の終わりで次の行へ数えて 0 から")
        REFLECT_END()

        /// @brief UI トランスフォームを使う
        bool RequiresComponent(const IComponent& other) const override;

        /// @brief UI トランスフォームを確保し、レンダラー・頂点バッファ・マテリアル・テクスチャを用意する
        void Awake() override;

        // ===== IRenderableComponent =====

        RenderPassType GetRenderPassType() const override { return RenderPassType::UI; }
        BlendMode GetBlendMode() const override { return BlendMode::kBlendModeNormal; }
        void SetBlendMode(BlendMode) override {}

        /// @brief UI トランスフォームの配置で矩形を描く
        void Render(const DrawViewInfo& view) override;

        // ===== テクスチャ =====

        /// @brief テクスチャをパスかファイル名で指す（空なら白い矩形に戻す）
        void SetTexture(const std::string& texturePath);

        /// @brief 指しているテクスチャのパス（指していなければ空）
        std::string GetTexturePath() const;

        /// @brief 指しているテクスチャ（型記述子とやり取りする値）
        Reflection::AssetRefValue GetTextureAsset() const { return textureAsset_.GetValue(); }

        /// @brief テクスチャを指し直す（何も指さない値なら白い矩形に戻す）
        void SetTextureAsset(const Reflection::AssetRefValue& value);

        /// @brief テクスチャの実際の大きさ（px）
        Vector2 GetTextureSize() const { return textureSize_; }

        /// @brief テクスチャの GPU ハンドル（読み込む前は 0）
        D3D12_GPU_DESCRIPTOR_HANDLE GetTextureGpuHandle() const { return textureHandle_.gpuHandle; }

        /// @brief UI トランスフォームの大きさを 1 コマの画素数にする
        void SetNativeSize();

        // ===== コマ割り =====

        /// @brief テクスチャを横に何コマへ分けるか（1 以上）
        int GetColumns() const { return columns_; }
        void SetColumns(int columns);

        /// @brief テクスチャを縦に何コマへ分けるか（1 以上）
        int GetRows() const { return rows_; }
        void SetRows(int rows);

        /// @brief 描くコマ（左上から右へ、行の終わりで次の行へ数えて 0 から。範囲外は丸める）
        int GetFrame() const { return frame_; }
        void SetFrame(int frame);

        // ===== 色 =====

        void SetColor(const Vector4& color);
        Vector4 GetColor() const { return color_; }

        // ===== マテリアル =====

        /// @brief マテリアル（.mat）をパスかファイル名で指す（空なら標準の描き方に戻す）
        void SetMaterial(const std::string& materialPath);

        /// @brief 指しているマテリアルのパス（指していなければ空）
        std::string GetMaterialPath() const { return materialSlot_.GetPath(); }

        /// @brief マテリアルの欄（共有のマテリアルとこの部品だけの上書き）
        ShaderMaterialSlot& GetMaterialSlot() { return materialSlot_; }

        /// @brief 指しているマテリアル（型記述子とやり取りする値）
        Reflection::AssetRefValue GetMaterialAsset() const { return materialSlot_.GetAsset(); }
        void SetMaterialAsset(const Reflection::AssetRefValue& value);

        /// @brief 兄弟の UI トランスフォーム（配置の出どころ）
        RectTransformComponent* GetRectTransform() const;

        // ===== 操作 =====

        /// @brief 押されたときに呼ぶ関数を登録する
        void SetOnClick(std::function<void()> callback) { onClick_ = std::move(callback); }

        /// @brief カーソルが乗ったときに呼ぶ関数を登録する
        void SetOnHover(std::function<void()> callback) { onHover_ = std::move(callback); }

        /// @brief 押す・乗せるの判定を行うか
        void SetInteractable(bool interactable) { interactable_ = interactable; }
        bool IsInteractable() const { return interactable_; }

        /// @brief 押されたときの関数を呼ぶ
        void InvokeOnClick() { if (onClick_) { onClick_(); } }

        /// @brief カーソルが乗ったときの関数を呼ぶ
        void InvokeOnHover() { if (onHover_) { onHover_(); } }

        /// @brief 押されたときの関数が登録されているか
        bool HasOnClick() const { return static_cast<bool>(onClick_); }

    private:
        /// @brief RenderManager から UI パスのレンダラーを取る
        void ResolveRenderer();

        /// @brief 頂点・インデックスバッファとマテリアルを作る
        void CreateGpuResources();

        /// @brief 指しているテクスチャ（無ければ白）を読み込み、大きさを取る
        void LoadTexture();

        /// @brief 基準点と今のコマから頂点を書き直す
        void UpdateVertexData(const Vector2& pivot);

        /// @brief マテリアルのシェーダで描く
        /// @return 描けなければ false（標準の描き方で描き直す）
        bool RenderWithMaterial(ID3D12GraphicsCommandList* commandList, UploadRing& uploadRing,
            D3D12_GPU_VIRTUAL_ADDRESS transformAddress);

        UIRenderer* renderer_ = nullptr;
        mutable RectTransformComponent* rect_ = nullptr;
        bool awoken_ = false;

        AssetRef<TextureAsset> textureAsset_;
        TextureManager::LoadedTexture textureHandle_{};
        Vector2 textureSize_ = { 1.0f, 1.0f };

        Vector4 color_ = { 1.0f, 1.0f, 1.0f, 1.0f };
        std::unique_ptr<UIMaterialInstance> material_;
        ShaderMaterialSlot materialSlot_;

        int columns_ = 1;
        int rows_ = 1;
        int frame_ = 0;

        /// 4 頂点の矩形（GPU へは描くたびに、そのフレームの UploadRing に置く）
        std::array<VertexData, 4> vertices_{};
        Microsoft::WRL::ComPtr<ID3D12Resource> indexResource_;
        D3D12_INDEX_BUFFER_VIEW indexBufferView_{};

        /// 頂点を組んだときの基準点
        Vector2 builtPivot_ = { -1.0f, -1.0f };
        /// コマを変えたので頂点を組み直す
        bool verticesDirty_ = true;

        std::function<void()> onClick_;
        std::function<void()> onHover_;
        bool interactable_ = false;
    };
}
