#pragma once

#include "GameObject/Component/Core/IComponent.h"
#include "GameObject/Component/Render/IRenderableComponent.h"
#include "Graphics/Material/SpriteMaterialInstance.h"
#include "Graphics/Model/VertexData.h"
#include "Graphics/Texture/TextureManager.h"
#include "Math/EulerTransform.h"
#include "Math/MathCore.h"
#include "Math/Matrix/Matrix4x4.h"
#include "Math/Vector/Vector2.h"
#include "Math/Vector/Vector3.h"
#include "Math/Vector/Vector4.h"
#include "Reflection/Reflect.h"

#include <d3d12.h>
#include <array>
#include <memory>
#include <string>
#include <wrl.h>

namespace CoreEngine
{
    class SpriteRenderer;
    class TransformComponent;

    /// @brief テクスチャのサンプリングのしかた
    enum class SpriteFilter {
        Linear, ///< 線形補間（拡大するとなめらかにぼける）
        Point   ///< 最も近い画素（ドット絵向け）
    };

    /// @brief テクスチャを貼った板をワールドに置いて描くコンポーネント
    /// @details 位置・回転・スケールは兄弟の `TransformComponent` から取り、親子関係も効く。
    ///          描く範囲（UV）を横と縦のコマ数で格子に分け、そのうち 1 コマを描く。
    ///          板の大きさは「1 コマのピクセル数 ÷ 1 単位のピクセル数 × スケール」。
    class SpriteRendererComponent : public IComponent, public IRenderableComponent
    {
    public:
        SpriteRendererComponent() = default;

        /// @brief テクスチャを指定して作る（読み込みは Awake で行う）
        explicit SpriteRendererComponent(std::string texturePath);

        ~SpriteRendererComponent() override;

        const char* GetTypeName() const override { return "SpriteRenderer"; }

        REFLECT_DECLARE(SpriteRendererComponent)

        /// @brief トランスフォーム（TransformComponent）を使う
        bool RequiresComponent(const IComponent& other) const override;

        // ===== ライフサイクル =====

        /// @brief トランスフォームを確保し、レンダラー・インデックスバッファ・マテリアル・テクスチャを用意する
        void Awake() override;

        // ===== IRenderableComponent =====

        RenderPassType GetRenderPassType() const override { return RenderPassType::Sprite; }
        RenderItemKind GetRenderItemKind() const override { return RenderItemKind::Transparent; }
        BlendMode GetBlendMode() const override { return blendMode_; }
        void SetBlendMode(BlendMode blendMode) override { blendMode_ = blendMode; }
        int GetSortingLayer() const override { return sortingLayer_; }
        int GetOrderInLayer() const override { return orderInLayer_; }

        /// @brief ビューのカメラで板を描く（頂点の変更があれば描く前に書き直す）
        void Render(const DrawViewInfo& view) override;

        // ===== テクスチャ =====

        /// @brief テクスチャを差し替える（サイズもテクスチャから取り直す）
        void SetTexture(const std::string& texturePath);
        const std::string& GetTexturePath() const { return texturePath_; }

        /// @brief テクスチャの実際のサイズ（ピクセル）
        Vector2 GetTextureSize() const { return textureSize_; }

        /// @brief テクスチャの何ピクセルを 1 単位にするか
        float GetPixelsPerUnit() const { return pixelsPerUnit_; }
        void SetPixelsPerUnit(float pixelsPerUnit);

        /// @brief 板の大きさ（1 コマぶん・スケールを掛ける前・ワールドの長さ）
        Vector2 GetSize() const;

        // ===== コマ割り =====

        /// @brief 描く範囲を横に何コマへ分けるか（1 以上）
        int GetColumns() const { return columns_; }
        void SetColumns(int columns);

        /// @brief 描く範囲を縦に何コマへ分けるか（1 以上）
        int GetRows() const { return rows_; }
        void SetRows(int rows);

        /// @brief 描くコマ（左上から右へ、行の終わりで次の行へ数えて 0 から。範囲外は丸める）
        int GetFrame() const { return frame_; }
        void SetFrame(int frame);

        /// @brief コマの数（横のコマ数 × 縦のコマ数）
        int GetFrameCount() const { return columns_ * rows_; }

        /// @brief テクスチャのサンプリングのしかた
        SpriteFilter GetFilter() const { return filter_; }
        void SetFilter(SpriteFilter filter);

        // ===== 色・UV =====

        void SetColor(const Vector4& color);
        Vector4 GetColor() const { return color_; }

        /// @brief UV 変換行列を直接設定する
        void SetUVTransform(const Matrix4x4& uvTransform);
        Matrix4x4 GetUVTransform() const { return uvMatrix_; }

        /// @brief テクスチャのピクセル範囲で描く範囲を設定する
        void SetTextureRect(float texLeft, float texTop, float texWidth, float texHeight, const std::string& texturePath);

        /// @brief 正規化された UV で描く範囲を設定する
        void SetUVRect(float uvLeft, float uvTop, float uvRight, float uvBottom);

        /// @brief UV 座標をオフセット（移動）する
        void SetUVOffset(float offsetX, float offsetY);

        /// @brief UV 座標をスケールする
        void SetUVScale(float scaleX, float scaleY);

        /// @brief UV 座標を回転する
        void SetUVRotation(float rotation);

        /// @brief UV 変換を単位行列へ戻す
        void ResetUVTransform();

        // ===== 形 =====

        /// @brief アンカーポイントを設定する
        /// @param anchor (0,0)=左上 / (0.5,0.5)=中央 / (1,1)=右下
        void SetAnchor(const Vector2& anchor);
        Vector2 GetAnchor() const { return anchorPoint_; }

        /// @brief X 軸方向のフリップを設定する
        void SetFlipX(bool flip);
        bool GetFlipX() const { return flipX_; }

        /// @brief Y 軸方向のフリップを設定する
        void SetFlipY(bool flip);
        bool GetFlipY() const { return flipY_; }

        /// @brief 板の 4 隅のワールド座標（左下・左上・右下・右上）
        std::array<Vector3, 4> GetWorldCorners() const;

        // ===== 描画順 =====

        /// @brief ソートレイヤーを設定する（大きいほど手前）
        void SetSortingLayer(int layer) { sortingLayer_ = layer; }

        /// @brief レイヤー内の描画順を設定する（大きいほど手前）
        void SetOrderInLayer(int order) { orderInLayer_ = order; }

        /// @brief 色・UV・アンカー・フリップ・コマ割りを既定へ戻す
        void Reset();

        // ===== 型記述子とやり取りする値 =====

        /// @brief 指しているテクスチャ
        Reflection::AssetRefValue GetTextureAsset() const;
        void SetTextureAsset(const Reflection::AssetRefValue& value);

        /// @brief 描く範囲（左上・右下の UV）
        Vector2 GetUVMin() const { return uvMin_; }
        void SetUVMin(const Vector2& value) { SetUVRect(value.x, value.y, uvMax_.x, uvMax_.y); }
        Vector2 GetUVMax() const { return uvMax_; }
        void SetUVMax(const Vector2& value) { SetUVRect(uvMin_.x, uvMin_.y, value.x, value.y); }

        /// @brief UV の移動量
        Vector2 GetUVOffset() const { return { uvTransform_.translate.x, uvTransform_.translate.y }; }
        void SetUVOffset(const Vector2& value) { SetUVOffset(value.x, value.y); }

        /// @brief UV の倍率
        Vector2 GetUVScale() const { return { uvTransform_.scale.x, uvTransform_.scale.y }; }
        void SetUVScale(const Vector2& value) { SetUVScale(value.x, value.y); }

        /// @brief UV の回転 [rad]
        float GetUVRotation() const { return uvTransform_.rotate.z; }

    private:
        /// @brief RenderManager から Sprite パスのレンダラーを取る
        void ResolveRenderer();

        /// @brief インデックスバッファとマテリアルを作る
        void CreateGpuResources();

        /// @brief テクスチャを読み込み、サイズを取る
        void LoadTexture(const std::string& texturePath);

        /// @brief 今のコマの UV の範囲（左上・右下）
        void GetFrameUVRect(Vector2& outMin, Vector2& outMax) const;

        /// @brief アンカー・今のコマの UV の範囲・フリップから頂点を書き直す
        void UpdateVertexData();

        /// @brief UV 変換のパラメータから行列を作ってマテリアルへ渡す
        void UpdateUVTransformMatrix();

        /// @brief 頂点（アンカー基準の 1×1 の板）を、大きさとトランスフォームでワールドへ移す行列
        Matrix4x4 GetQuadWorldMatrix() const;

        /// @brief 兄弟のトランスフォーム
        TransformComponent* GetTransform() const;

        SpriteRenderer* spriteRenderer_ = nullptr;
        mutable TransformComponent* transform_ = nullptr;
        bool awoken_ = false;

        std::string texturePath_;
        TextureManager::LoadedTexture textureHandle_{};
        Vector2 textureSize_ = { 1.0f, 1.0f };
        float pixelsPerUnit_ = 100.0f;
        SpriteFilter filter_ = SpriteFilter::Linear;

        std::unique_ptr<SpriteMaterialInstance> material_;
        Vector4 color_ = { 1.0f, 1.0f, 1.0f, 1.0f };
        Matrix4x4 uvMatrix_ = MathCore::Matrix::Identity();
        /// UV 変換のパラメータ（インスペクタと保存に使う）
        EulerTransform uvTransform_ = { { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } };

        Vector2 anchorPoint_ = { 0.5f, 0.5f };
        Vector2 uvMin_ = { 0.0f, 0.0f };
        Vector2 uvMax_ = { 1.0f, 1.0f };
        bool flipX_ = false;
        bool flipY_ = false;
        int columns_ = 1;
        int rows_ = 1;
        int frame_ = 0;
        bool vertexDataDirty_ = true;

        BlendMode blendMode_ = BlendMode::kBlendModeNormal;
        int sortingLayer_ = 0;
        int orderInLayer_ = 0;

        /// 4 頂点の板（GPU へは描くたびに、そのフレームの UploadRing に置く）
        std::array<VertexData, 4> vertices_{};
        Microsoft::WRL::ComPtr<ID3D12Resource> indexResource_;
        D3D12_INDEX_BUFFER_VIEW indexBufferView_{};
    };
}
