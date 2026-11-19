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
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <wrl.h>

namespace CoreEngine
{
    class RectTransformComponent;
    class UIRenderer;
    class UploadRing;

    /// @brief UI 画像の描き方
    enum class UIImageType
    {
        Simple, ///< 四角 1 枚に画像を伸ばして貼る
        Sliced, ///< 9 スライス（角は伸ばさず、辺と真ん中だけ伸ばす）
        Filled, ///< 塗る量（横・縦・円の向きに、割合だけ描く）
    };

    /// @brief 塗る量の向き
    enum class UIFillMethod
    {
        Horizontal, ///< 横
        Vertical,   ///< 縦
        Radial360,  ///< 円（360 度）
    };

    /// @brief 塗り始める側（横は左か右、縦は下か上、円はその向きから）
    enum class UIFillOrigin
    {
        Left,
        Right,
        Bottom,
        Top,
    };

    /// @brief テクスチャを貼った矩形を UI パスで描くコンポーネント
    /// @details 位置・大きさ・回転は兄弟の `RectTransformComponent` から取る（無ければ Awake で足す）。
    ///          テクスチャを指していなければ白い矩形を描く。
    ///          テクスチャを横と縦のコマ数で格子に分け、そのうち 1 コマを描く。
    ///          描き方は、四角 1 枚・9 スライス・塗る量（ゲージ）から選ぶ。
    class UIImageComponent : public IComponent, public IRenderableComponent
    {
    public:
        UIImageComponent() = default;
        ~UIImageComponent() override;

        const char* GetTypeName() const override { return "UIImage"; }

        /// @brief 描き方の名前（`UIImageType` の並び）
        static constexpr const char* kImageTypeNames[] = { "単純", "9 スライス", "塗る量" };

        /// @brief 塗る向きの名前（`UIFillMethod` の並び）
        static constexpr const char* kFillMethodNames[] = { "横", "縦", "円" };

        /// @brief 塗り始める側の名前（`UIFillOrigin` の並び）
        static constexpr const char* kFillOriginNames[] = { "左", "右", "下", "上" };

        /// @brief 1 枚の画像を描く四角の数の上限（9 スライスの 9 枚）
        static constexpr uint32_t kMaxQuads = 9;

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
            REFLECT_ENUM_ACCESSOR("imageType", "描き方", GetImageType, SetImageType, kImageTypeNames,
                p.tooltip = "単純：四角 1 枚に伸ばす／9 スライス：角は伸ばさず辺と真ん中だけ伸ばす／塗る量：割合だけ描く（ゲージ）",
                p.saveWhen = &Self::IsNotSimple)
            REFLECT_ACCESSOR("fillCenter", "真ん中を描く", IsFillCenter, SetFillCenter,
                p.tooltip = "外すと枠だけを描く",
                p.saveWhen = &Self::IsSlicedImage, p.visibleWhen = &Self::IsSlicedImage)
            REFLECT_ACCESSOR("overrideBorder", "枠の幅を上書き", IsBorderOverridden, SetBorderOverridden,
                p.tooltip = "外すと、テクスチャの設定（.meta）の枠の幅を使う",
                p.saveWhen = &Self::IsSlicedImage, p.visibleWhen = &Self::IsSlicedImage)
            REFLECT_ACCESSOR("border", "枠の幅", GetBorderOverride, SetBorderOverride,
                p.range = Range(0.0f, 4096.0f, 1.0f), p.tooltip = "左・上・右・下（テクスチャの px）",
                p.saveWhen = &Self::UsesBorderOverride, p.visibleWhen = &Self::UsesBorderOverride)
            REFLECT_ENUM_ACCESSOR("fillMethod", "塗る向き", GetFillMethod, SetFillMethod, kFillMethodNames,
                p.saveWhen = &Self::IsFilledImage, p.visibleWhen = &Self::IsFilledImage)
            REFLECT_ACCESSOR("fillAmount", "塗る量", GetFillAmount, SetFillAmount,
                p.range = Range(0.0f, 1.0f, 0.005f), p.tooltip = "0 で何も描かず、1 で全部描く",
                p.saveWhen = &Self::IsFilledImage, p.visibleWhen = &Self::IsFilledImage)
            REFLECT_ENUM_ACCESSOR("fillOrigin", "塗り始め", GetFillOrigin, SetFillOrigin, kFillOriginNames,
                p.tooltip = "横は左か右から、縦は下か上から、円はその向き（上 = 12 時）から塗る",
                p.saveWhen = &Self::IsFilledImage, p.visibleWhen = &Self::IsFilledImage)
            REFLECT_ACCESSOR("fillClockwise", "時計回り", IsFillClockwise, SetFillClockwise,
                p.saveWhen = &Self::IsRadialImage, p.visibleWhen = &Self::IsRadialImage)
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

        /// @brief テクスチャを指しているか（指していなければ白い矩形を描く）
        bool HasTexture() const { return textureAsset_.IsSet(); }

        /// @brief 1 コマの画素数（テクスチャの大きさをコマ数で割った大きさ）
        Vector2 GetFrameSize() const;

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

        // ===== 描き方 =====

        UIImageType GetImageType() const { return imageType_; }
        void SetImageType(UIImageType type);

        /// @brief 9 スライスの真ん中を描くか
        bool IsFillCenter() const { return fillCenter_; }
        void SetFillCenter(bool fill);

        /// @brief 9 スライスの枠の幅を、テクスチャの設定ではなくこの画像の値にするか
        bool IsBorderOverridden() const { return overrideBorder_; }
        void SetBorderOverridden(bool overridden);

        /// @brief この画像の 9 スライスの枠の幅（左・上・右・下。テクスチャの px）
        Vector4 GetBorderOverride() const { return borderOverride_; }
        void SetBorderOverride(const Vector4& border);

        /// @brief 今の 9 スライスの枠の幅（上書きしていなければテクスチャの設定）
        Vector4 GetBorder() const;

        UIFillMethod GetFillMethod() const { return fillMethod_; }
        void SetFillMethod(UIFillMethod method);

        /// @brief 塗る量（0〜1）
        float GetFillAmount() const { return fillAmount_; }
        void SetFillAmount(float amount);

        UIFillOrigin GetFillOrigin() const { return fillOrigin_; }
        void SetFillOrigin(UIFillOrigin origin);

        /// @brief 円で塗るとき時計回りに塗るか
        bool IsFillClockwise() const { return fillClockwise_; }
        void SetFillClockwise(bool clockwise);

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
        static bool IsNotSimple(const void* o) { return static_cast<const UIImageComponent*>(o)->imageType_ != UIImageType::Simple; }
        static bool IsSlicedImage(const void* o) { return static_cast<const UIImageComponent*>(o)->imageType_ == UIImageType::Sliced; }
        static bool IsFilledImage(const void* o) { return static_cast<const UIImageComponent*>(o)->imageType_ == UIImageType::Filled; }
        static bool IsRadialImage(const void* o)
        {
            const auto* self = static_cast<const UIImageComponent*>(o);
            return self->imageType_ == UIImageType::Filled && self->fillMethod_ == UIFillMethod::Radial360;
        }
        static bool UsesBorderOverride(const void* o)
        {
            const auto* self = static_cast<const UIImageComponent*>(o);
            return self->imageType_ == UIImageType::Sliced && self->overrideBorder_;
        }

        /// @brief RenderManager から UI パスのレンダラーを取る
        void ResolveRenderer();

        /// @brief 頂点・インデックスバッファとマテリアルを作る
        void CreateGpuResources();

        /// @brief 指しているテクスチャ（無ければ白）を読み込み、大きさを取る
        void LoadTexture();

        /// @brief 基準点と今のコマから頂点を書き直す
        void UpdateVertexData(const Vector2& pivot);

        /// @brief 9 スライスの頂点を組む（自分の座標の px）
        void BuildSlicedVertices(const Vector2& pivot, const Vector2& size);

        /// @brief 塗る量の頂点を組む（大きさ 1 の四角の中）
        void BuildFilledVertices(const Vector2& pivot);

        /// @brief 四角 1 枚ぶんの頂点を書く（左下・左上・右下・右上の順）
        void WriteQuad(uint32_t quad, const Vector2& leftTop, const Vector2& rightBottom,
            const Vector2& uvLeftTop, const Vector2& uvRightBottom);

        /// @brief テクスチャの設定の枠の幅を読み直す
        void RefreshTextureBorder();

        /// @brief 今のコマの UV の範囲
        void GetFrameUv(Vector2& outMin, Vector2& outMax) const;

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

        /// 四角ごとに 4 頂点（GPU へは描くたびに、そのフレームの UploadRing に置く）
        std::array<VertexData, kMaxQuads * 4> vertices_{};
        /// 描く四角の数
        uint32_t quadCount_ = 1;

        /// 頂点を組んだときの基準点と大きさ
        Vector2 builtPivot_ = { -1.0f, -1.0f };
        Vector2 builtSize_ = { -1.0f, -1.0f };
        /// 頂点を組み直すか（コマ・描き方を変えたときに立てる）
        bool verticesDirty_ = true;

        UIImageType imageType_ = UIImageType::Simple;
        bool fillCenter_ = true;
        bool overrideBorder_ = false;
        Vector4 borderOverride_ = { 0.0f, 0.0f, 0.0f, 0.0f };
        /// テクスチャの設定の枠の幅と、それを読んだときの番号
        Vector4 textureBorder_ = { 0.0f, 0.0f, 0.0f, 0.0f };
        uint32_t textureBorderRevision_ = UINT32_MAX;
        UIFillMethod fillMethod_ = UIFillMethod::Horizontal;
        float fillAmount_ = 1.0f;
        UIFillOrigin fillOrigin_ = UIFillOrigin::Left;
        bool fillClockwise_ = true;

        std::function<void()> onClick_;
        std::function<void()> onHover_;
        bool interactable_ = false;
    };
}
