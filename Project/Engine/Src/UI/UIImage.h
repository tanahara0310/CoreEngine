#pragma once

#include "UIElement.h"
#include "GameObject/GameObject.h"
#include "Graphics/Material/UIMaterialInstance.h"
#include "Graphics/Texture/TextureManager.h"
#include <memory>
#include <string>
#include <functional>
#include <d3d12.h>
#include <wrl.h>

namespace CoreEngine
{
    class UIRenderer;

    /// @brief 画像表示用 UI 要素
    class UIImage : public GameObject
    {
    public:
        UIImage() = default;
        ~UIImage() override = default;

        /// @brief 初期化（テクスチャ指定）
        /// @param textureFilePath 表示するテクスチャのファイルパス
        /// @param name            デバッグ名（任意）
        void Initialize(const std::string& textureFilePath, const std::string& name = "");

        /// @brief テクスチャを差し替える
        void SetTexture(const std::string& textureFilePath);

        // ===== GameObject インターフェース =====
        RenderPassType GetRenderPassType() const override { return RenderPassType::UI; }
        BlendMode GetBlendMode() const override { return BlendMode::kBlendModeNormal; }
        const char* GetObjectName() const override { return "UIImage"; }
        void Draw(const Camera* camera) override;

        /// @brief ワールド空間での位置を返す（UI はアンカー相対のスクリーン座標を XY に載せる）
        /// @note 3D の当たり判定に使う想定は無い。座標系が違うので混ぜないこと。
        /// @note **UI は意図的に `ITransformSource` コンポーネント化していない**。
        ///       `UILayout` はアンカー・ピボット・サイズ（いずれも Vector2）で位置が決まり、
        ///       3D の translate/rotate/scale（Vector3）とは意味論が違う。無理に載せると
        ///       Vector2 ↔ Vector3 のミラーを同期し続ける必要があり同期漏れバグの温床になる。
        ///       UI の編集は専用の `CanvasViewport`（2D エディタ）が担当する。
        Vector3 GetWorldPosition() const override {
            return { layout_.anchoredPos.x, layout_.anchoredPos.y, 0.0f };
        }

        // ===== UILayout アクセサ =====
        void SetAnchor(UIAnchor anchor) { layout_.anchor = anchor; }
        UIAnchor GetAnchor() const { return layout_.anchor; }

        /// @brief アンカーを変更しつつ、スクリーン上の見た目の位置を維持する
        /// @param newAnchor    新しいアンカー
        /// @param canvasSize   基準解像度サイズ（UIRenderer::GetScreenSize() の値）
        void ChangeAnchorKeepingPosition(UIAnchor newAnchor, const Vector2& canvasSize);

        void SetAnchoredPosition(const Vector2& pos) { layout_.anchoredPos = pos; }
        Vector2 GetAnchoredPosition() const { return layout_.anchoredPos; }

        void SetPivot(const Vector2& pivot) { layout_.pivot = pivot; rebuildVertex_ = true; }
        Vector2 GetPivot() const { return layout_.pivot; }

        void SetSize(const Vector2& size) { layout_.size = size; }
        Vector2 GetSize() const { return layout_.size; }

        void SetUIRotation(float rot) { layout_.rotation = rot; }
        float GetUIRotation() const { return layout_.rotation; }

        void SetSortOrder(int order) { layout_.sortOrder = order; SetRenderOrder(order); }
        int  GetSortOrder() const { return layout_.sortOrder; }

        // ===== カラー / マテリアル =====
        void SetColor(const Vector4& color);
        Vector4 GetColor() const;

        // ===== テクスチャサイズ =====
        Vector2 GetTextureSize() const { return textureSize_; }

        /// @brief テクスチャの GPU ハンドル取得（ImGui プレビュー等で利用）
        D3D12_GPU_DESCRIPTOR_HANDLE GetTextureGpuHandle() const { return textureHandle_.gpuHandle; }

        /// @brief レイアウト情報を取得（ImGui プレビュー用）
        const UILayout& GetLayout() const { return layout_; }

#ifdef USE_IMGUI
        int  GetInspectorTabs(InspectorTabDef* outTabs, int maxTabs) const override;
        bool DrawInspectorTabContent(int tabIndex) override;
#endif

        // ===== インタラクション =====

        /// @brief クリック時のコールバックを登録する
        void SetOnClick(std::function<void()> callback) { onClick_ = std::move(callback); }

        /// @brief ホバー時のコールバックを登録する
        void SetOnHover(std::function<void()> callback) { onHover_ = std::move(callback); }

        /// @brief インタラクション有効フラグを設定する（false の場合ヒット判定を行わない）
        void SetInteractable(bool interactable) { interactable_ = interactable; }
        bool IsInteractable() const { return interactable_; }

        /// @brief クリックコールバックを呼び出す（Canvas ビュー / 実ゲーム共用）
        void InvokeOnClick() { if (onClick_) { onClick_(); } }

        /// @brief ホバーコールバックを呼び出す
        void InvokeOnHover() { if (onHover_) { onHover_(); } }

        /// @brief コールバックが登録されているか
        bool HasOnClick() const { return static_cast<bool>(onClick_); }

    private:
        /// @brief 頂点バッファ生成（pivot を考慮した単位クワッド）
        void CreateVertexBuffer();

        /// @brief 頂点データを更新（pivot 変更時に呼ぶ）
        void UpdateVertexData();

        UIRenderer* renderer_ = nullptr;
        UILayout layout_;
        Vector2 textureSize_ = { 1.0f, 1.0f };

        TextureManager::LoadedTexture textureHandle_;
        std::unique_ptr<UIMaterialInstance> material_;

        Microsoft::WRL::ComPtr<ID3D12Resource>   vertexResource_;
        D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
        Microsoft::WRL::ComPtr<ID3D12Resource>   indexResource_;
        D3D12_INDEX_BUFFER_VIEW indexBufferView_{};

        // 前フレームの pivot を記憶し、変更検知して頂点を再生成
        Vector2 lastPivot_ = { -1.0f, -1.0f };
        bool    rebuildVertex_ = false;

        // インタラクション
        std::function<void()> onClick_;
        std::function<void()> onHover_;
        bool interactable_ = false;
    };

} // namespace CoreEngine
