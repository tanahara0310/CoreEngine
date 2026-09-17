#pragma once

#include "GameObject/Component/Core/IComponent.h"
#include "GameObject/Component/Render/IRenderableComponent.h"
#include "GameObject/Sprite/SpriteAnimator.h"
#include "Graphics/Material/SpriteMaterialInstance.h"
#include "Graphics/Texture/TextureManager.h"
#include "Math/EulerTransform.h"
#include "Math/MathCore.h"
#include "Math/Matrix/Matrix4x4.h"
#include "Math/Vector/Vector2.h"
#include "Math/Vector/Vector4.h"

#include <d3d12.h>
#include <memory>
#include <string>
#include <wrl.h>

namespace CoreEngine
{
    class Camera;
    class ITransformSource;
    class SpriteRenderer;

    /// @brief テクスチャを貼った矩形を Sprite パス（2D）で描くコンポーネント
    /// @details 位置・回転・スケールは兄弟の `ITransformSource` から取る（無ければ Awake で
    ///          `EulerTransformComponent` を足す）。スケールはテクスチャのピクセルサイズに掛ける倍率で、
    ///          座標は 2D カメラの空間（画面中央が原点・Y 上正）。
    class SpriteRendererComponent : public IComponent, public IRenderableComponent
    {
    public:
        SpriteRendererComponent() = default;

        /// @brief テクスチャを指定して作る（読み込みは Awake で行う）
        explicit SpriteRendererComponent(std::string texturePath);

        ~SpriteRendererComponent() override;

        const char* GetTypeName() const override { return "SpriteRenderer"; }

        /// @brief トランスフォーム（ITransformSource）を使う
        bool RequiresComponent(const IComponent& other) const override;

#ifdef USE_IMGUI
        /// @brief テクスチャ・色・UV・ブレンド・描画順・フリップ・アンカーの編集 UI
        /// @return 値が変更されたら true
        bool DrawEditorUI();
#endif

        // ===== ライフサイクル =====

        /// @brief トランスフォームを確保し、レンダラー・頂点バッファ・マテリアル・テクスチャを用意する
        void Awake() override;

        /// @brief アニメーターを進める
        void Update() override;

        // ===== IRenderableComponent =====

        RenderPassType GetRenderPassType() const override { return RenderPassType::Sprite; }
        BlendMode GetBlendMode() const override { return blendMode_; }
        void SetBlendMode(BlendMode blendMode) override { blendMode_ = blendMode; }

        /// @brief ビューのカメラで矩形を描く（頂点の変更があれば描く前に書き直す）
        void Render(const DrawViewInfo& view) override;

        // ===== テクスチャ =====

        /// @brief テクスチャを差し替える（サイズもテクスチャから取り直す）
        void SetTexture(const std::string& texturePath);
        const std::string& GetTexturePath() const { return texturePath_; }

        /// @brief テクスチャの実際のサイズ（ピクセル）
        Vector2 GetTextureSize() const { return textureSize_; }

        /// @brief 描画サイズ（テクスチャサイズ × スケール）
        Vector2 GetActualSize() const;

        // ===== 色・UV =====

        void SetColor(const Vector4& color);
        Vector4 GetColor() const { return color_; }

        /// @brief UV 変換行列を直接設定する
        void SetUVTransform(const Matrix4x4& uvTransform);
        Matrix4x4 GetUVTransform() const { return uvMatrix_; }

        /// @brief テクスチャのピクセル範囲で UV の範囲を設定する
        void SetTextureRect(float texLeft, float texTop, float texWidth, float texHeight, const std::string& texturePath);

        /// @brief 正規化された UV の範囲を直接設定する
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

        // ===== 描画順 =====

        /// @brief ソートレイヤーを設定する（大きいほど前面。オーナーの描画順を layer * 1000 + order にする）
        void SetSortingLayer(int layer);
        int GetSortingLayer() const { return sortingLayer_; }

        /// @brief レイヤー内の描画順を設定する（大きいほど前面）
        void SetOrderInLayer(int order);
        int GetOrderInLayer() const { return orderInLayer_; }

        /// @brief 色・UV・アンカー・フリップを既定へ戻す
        void Reset();

        // ===== アニメーション =====

        /// @brief アニメーターを取得する（無ければ作る）
        SpriteAnimator& GetAnimator();

        /// @brief アニメーターがあるか
        bool HasAnimator() const { return animator_ != nullptr; }

        /// @brief 兄弟のトランスフォーム（位置・回転・スケールの出どころ）
        ITransformSource* GetTransformSource() const;

        // ===== シリアライズ =====

        /// @brief テクスチャ・色・アンカー・UV・フリップ・ブレンド・描画順を書き出す
        json OnSerialize() const override;

        /// @brief OnSerialize が書いた値を読む
        void OnDeserialize(const json& j) override;

    private:
        /// @brief RenderManager から Sprite パスのレンダラーを取る
        void ResolveRenderer();

        /// @brief 頂点・インデックスバッファとマテリアルを作る
        void CreateGpuResources();

        /// @brief テクスチャを読み込み、サイズを取る
        void LoadTexture(const std::string& texturePath);

        /// @brief アンカー・UV の範囲・フリップから頂点を書き直す
        void UpdateVertexData();

        /// @brief UV 変換のパラメータから行列を作ってマテリアルへ渡す
        void UpdateUVTransformMatrix();

        /// @brief 描画順を指定済みなら、オーナーの描画順へ反映する
        void ApplyRenderOrder();

        /// @brief カメラの行列で矩形を描く
        void Draw2D(const Camera* camera, ID3D12GraphicsCommandList* commandList);

        SpriteRenderer* spriteRenderer_ = nullptr;
        mutable ITransformSource* transform_ = nullptr;
        bool awoken_ = false;

        std::string texturePath_;
        TextureManager::LoadedTexture textureHandle_{};
        Vector2 textureSize_ = { 1.0f, 1.0f };

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
        bool vertexDataDirty_ = true;

        BlendMode blendMode_ = BlendMode::kBlendModeNormal;
        int sortingLayer_ = 0;
        int orderInLayer_ = 0;
        /// ソートレイヤーかレイヤー内の描画順を指定したか
        bool hasRenderOrder_ = false;

        Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
        D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
        Microsoft::WRL::ComPtr<ID3D12Resource> indexResource_;
        D3D12_INDEX_BUFFER_VIEW indexBufferView_{};

        std::unique_ptr<SpriteAnimator> animator_;
    };
}
