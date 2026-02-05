#pragma once

#include "Engine/ObjectCommon/GameObject.h"
#include "Engine/WorldTransfom/WorldTransform.h"
#include "Engine/Graphics/Render/Sprite/SpriteRenderer.h"
#include "Engine/Graphics/TextureManager.h"
#include <memory>
#include <string>
#include <d3d12.h>
#include <wrl.h>

using Microsoft::WRL::ComPtr;

/// @brief スプライトオブジェクト - GameObject基底クラスを継承してRenderManager対応

namespace CoreEngine
{
class SpriteObject : public GameObject {
public:
    /// @brief コンストラクタ
    SpriteObject() = default;
    
    /// @brief デストラクタ
    ~SpriteObject() override = default;
    
    /// @brief 初期化（テクスチャ指定版 - 推奨）
    /// @param textureFilePath テクスチャファイルパス
    /// @param name オブジェクト名（ImGui表示用、省略可）
    void Initialize(const std::string& textureFilePath, const std::string& name = "");
    
    /// @brief 更新
    void Update() override;
    
    /// @brief 描画処理
    /// @param camera カメラ
    void Draw(const ICamera* camera) override;
    
    /// @brief 2D専用描画（内部実装）
    /// @param camera 2D用カメラ
    void Draw2D(const ICamera* camera);
    
#ifdef _DEBUG
    /// @brief ImGuiデバッグUI描画（Sprite独自実装）
    /// @return 変更があった場合 true
    bool DrawImGui() override;
    
    /// @brief オブジェクト名を取得
    /// @return オブジェクト名
    const char* GetObjectName() const override { return "Sprite"; }
#endif
    
    /// @brief このオブジェクトの描画パスタイプを取得
    /// @return 描画パスタイプ（Sprite）
    RenderPassType GetRenderPassType() const override { return RenderPassType::Sprite; }
    
    /// @brief テクスチャハンドルを設定
    /// @param textureFilePath テクスチャファイルパス
    void SetTexture(const std::string& textureFilePath);
    
    /// @brief 色を設定
    void SetColor(const Vector4& color) { color_ = color; }
    Vector4 GetColor() const { return color_; }
    
    /// @brief UV変換行列を設定
    void SetUVTransform(const Matrix4x4& uvTransform) { uvTransform_ = uvTransform; }
    Matrix4x4 GetUVTransform() const { return uvTransform_; }
    
    /// @brief アンカーポイントを設定
    /// @param anchor アンカーポイント (0.0f, 0.0f)=左上、(0.5f, 0.5f)=中央、(1.0f, 1.0f)=右下
    void SetAnchor(const Vector2& anchor);
    Vector2 GetAnchor() const { return anchorPoint_; }
    
    /// @brief テクスチャの実際のサイズを取得（ピクセル）
    Vector2 GetTextureSize() const { return textureSize_; }
    
    /// @brief 実際の描画サイズを取得（テクスチャサイズ × スケール)
    Vector2 GetActualSize() const { return { textureSize_.x * transform_.scale.x, textureSize_.y * transform_.scale.y }; }
    
    /// @brief テクスチャ範囲を設定
    void SetTextureRect(float texLeft, float texTop, float texWidth, float texHeight, const std::string& textureFilePath);
    
    /// @brief 正規化されたUV座標範囲を直接設定
    void SetUVRect(float uvLeft, float uvTop, float uvRight, float uvBottom);
    
    /// @brief UV座標をオフセット（移動）
    void SetUVOffset(float offsetX, float offsetY);
    
    /// @brief UV座標をスケール
    void SetUVScale(float scaleX, float scaleY);
    
    /// @brief UV座標を回転
    void SetUVRotation(float rotation);
    
    /// @brief UV変換をリセット
    void ResetUVTransform();
    
    /// @brief デフォルト値にリセット
    void Reset();
    
    /// @brief ブレンドモードを取得
    BlendMode GetBlendMode() const override { return blendMode_; }
    
    /// @brief ブレンドモードを設定
    void SetBlendMode(BlendMode blendMode) override { blendMode_ = blendMode; }
    
    /// @brief スプライト用トランスフォームを取得
    /// @note SpriteObject は EulerTransform を使用
    EulerTransform& GetSpriteTransform() { return transform_; }
    const EulerTransform& GetSpriteTransform() const { return transform_; }
    
private:
    /// @brief テクスチャサイズを自動設定
    void SetSizeFromTexture(const std::string& textureFilePath);
    
    /// @brief 頂点バッファを作成
    void CreateVertexBuffer();
    
    /// @brief 頂点データを更新
    void UpdateVertexData();
    
    /// @brief UV変換行列を計算
    void UpdateUVTransformMatrix(const EulerTransform& uvTransform);
    
    /// @brief アンカーポイントを変更
    /// @param newAnchor 新しいアンカーポイント
    void ChangeAnchorKeepingPosition(const Vector2& newAnchor);
    
private:
    /// @brief トランスフォーム
    EulerTransform transform_;
    
    /// @brief スプライトレンダラー
    SpriteRenderer* spriteRenderer_ = nullptr;
    
    /// @brief テクスチャハンドル
    TextureManager::LoadedTexture textureHandle_;
    
    /// @brief テクスチャの実際のサイズ（ピクセル）
    Vector2 textureSize_ = { 1.0f, 1.0f };
    
    /// @brief マテリアル
    Vector4 color_ = { 1.0f, 1.0f, 1.0f, 1.0f };
    Matrix4x4 uvTransform_ = MathCore::Matrix::Identity();
    
    /// @brief アンカーポイント（0.0f, 0.0f = 左上、0.5f, 0.5f = 中央、1.0f, 1.0f = 右下）
    Vector2 anchorPoint_ = { 0.5f, 0.5f };  // デフォルトを中央に変更
    
    /// @brief 頂点バッファ
    ComPtr<ID3D12Resource> vertexResource_;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
    ComPtr<ID3D12Resource> indexResource_;
    D3D12_INDEX_BUFFER_VIEW indexBufferView_;
    
    /// @brief UV座標範囲
    Vector2 uvMin_ = { 0.0f, 0.0f };
    Vector2 uvMax_ = { 1.0f, 1.0f };
    
    /// @brief ブレンドモード（デフォルトはアルファブレンド）
    BlendMode blendMode_ = BlendMode::kBlendModeNormal;
    
    /// @brief 頂点データ更新フラグ（Dirty Flag パターン）
    bool vertexDataDirty_ = false;
};
}
