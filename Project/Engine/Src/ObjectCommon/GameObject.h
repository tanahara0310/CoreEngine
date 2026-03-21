#pragma once

#include "Graphics/Render/RenderPassType.h"
#include "Graphics/Pipeline/PipelineStateManager.h"
#include "Math/Vector/Vector3.h"
#include "WorldTransform/WorldTransform.h"
#include "Graphics/Texture/TextureManager.h"
#include "Graphics/Model/Model.h"
#include "Collider/Collider.h"
#include "Collider/SphereCollider.h"
#include <functional>
#include "Collider/AABBCollider.h"

#include <memory>
#include <optional>
#include <string>

#ifdef _DEBUG
#include "Graphics/Material/Debug/MaterialDebugUI.h"
#endif

// Forward declaration
namespace CoreEngine {
    class ICamera;
    class EngineSystem;
}

/// @brief すべてのゲームオブジェクトの共通基底クラス

namespace CoreEngine
{
    class GameObject {
    public:
        virtual ~GameObject() = default;

        /// @brief 初期化処理（エンジンシステムを設定）
        /// @param engine エンジンシステムへのポインタ
        static void Initialize(EngineSystem* engine);

        /// @brief 更新処理（派生クラスでオーバーライド可能）
        virtual void Update() {}

        /// @brief 描画処理（派生クラスでオーバーライド可能）
        /// @param camera カメラオブジェクト（2Dオブジェクトの場合は nullptr でも可）
        virtual void Draw(const ICamera* camera) { (void)camera; }

        /// @brief シャドウマップ用の描画処理（派生クラスでオーバーライド可能）
        /// @param cmdList コマンドリスト
        virtual void DrawShadow(ID3D12GraphicsCommandList* cmdList);

        /// @brief アクティブ状態を設定
        void SetActive(bool active) { isActive_ = active; }

        /// @brief アクティブ状態を取得
        bool IsActive() const { return isActive_; }

        /// @brief 表示状態を設定（falseにすると描画のみスキップ、更新は継続）
        void SetVisible(bool visible) { isVisible_ = visible; }

        /// @brief 表示状態を取得
        bool IsVisible() const { return isVisible_; }

        /// @brief オブジェクトを削除マーク（フレーム終了時に自動削除）
        void Destroy() { markedForDestroy_ = true; }

        /// @brief 削除マークされているか確認
        bool IsMarkedForDestroy() const { return markedForDestroy_; }

        /// @brief 自動更新を設定（falseにすると手動更新が必要）
        /// @param autoUpdate 自動更新フラグ（true: GameObjectManagerが自動更新、false: 手動更新）
        void SetAutoUpdate(bool autoUpdate) { autoUpdate_ = autoUpdate; }

        /// @brief 自動更新が有効か確認
        /// @return 自動更新フラグ
        bool IsAutoUpdate() const { return autoUpdate_; }

        /// @brief 描画順序を設定（小さいほど先に描画、RenderPassType の優先度より優先される）
        /// @param order 描画順序
        void SetRenderOrder(int order) { renderOrder_ = order; }

        /// @brief 描画順序を取得（未設定の場合は std::nullopt）
        /// @return 描画順序（未設定の場合は std::nullopt）
        std::optional<int> GetRenderOrder() const { return renderOrder_; }

        /// @brief 描画順序のオーバーライドをリセット（RenderPassType の優先度に戻る）
        void ResetRenderOrder() { renderOrder_ = std::nullopt; }

        /// @brief 描画パスタイプを取得（派生クラスでオーバーライド）
        /// @return 描画パスタイプ（デフォルトは3Dモデル）
        virtual RenderPassType GetRenderPassType() const { return RenderPassType::Model; }

        /// @brief ブレンドモードを取得（派生クラスでオーバーライド可能）
        /// @return ブレンドモード（デフォルトは kBlendModeNone）
        virtual BlendMode GetBlendMode() const { return BlendMode::kBlendModeNone; }

        /// @brief ブレンドモードを設定（派生クラスでオーバーライド可能）
        /// @param blendMode 設定するブレンドモード
        virtual void SetBlendMode(BlendMode blendMode) { (void)blendMode; }

        /// @brief エンジンシステムを取得
        /// @return エンジンシステムへのポインタ
        EngineSystem* GetEngineSystem() const;

        /// @brief 衝突開始イベント（派生クラスでオーバーライド可能）
        /// @param other 衝突相手のオブジェクト
        virtual void OnCollisionEnter(GameObject* other) { (void)other; }

        /// @brief 衝突中イベント（派生クラスでオーバーライド可能）
        /// @param other 衝突相手のオブジェクト
        virtual void OnCollisionStay(GameObject* other) { (void)other; }

        /// @brief 衝突終了イベント（派生クラスでオーバーライド可能）
        /// @param other 衝突相手のオブジェクト
        virtual void OnCollisionExit(GameObject* other) { (void)other; }

        /// @brief ワールド座標での位置を取得（Collider用）
        virtual Vector3 GetWorldPosition() const { return transform_.GetWorldPosition(); }

        // === コライダー簡易設定API ===

        /// @brief 球体コライダーを追加
        /// @param radius 球の半径
        /// @param layer コリジョンレイヤー（デフォルト: Default）
        /// @return 追加されたコライダーへの参照（チェーン呼び出し用）
        Collider& AddSphereCollider(float radius, CollisionLayer layer = CollisionLayer::Default);

        /// @brief AABBコライダーを追加
        /// @param size AABBのサイズ（幅, 高さ, 奥行き）
        /// @param layer コリジョンレイヤー（デフォルト: Default）
        /// @return 追加されたコライダーへの参照（チェーン呼び出し用）
        Collider& AddAABBCollider(const Vector3& size, CollisionLayer layer = CollisionLayer::Default);

        /// @brief コライダーを持っているか確認
        /// @return コライダーを持っている場合 true
        bool HasCollider() const { return collider_ != nullptr; }

        /// @brief コライダーを取得
        /// @return コライダーへのポインタ（未設定の場合 nullptr）
        Collider* GetCollider() { return collider_.get(); }

        /// @brief コライダーを取得（const版）
        /// @return コライダーへのconstポインタ（未設定の場合 nullptr）
        const Collider* GetCollider() const { return collider_.get(); }

        /// @brief コライダーを削除
        void RemoveCollider() { collider_.reset(); }

        /// @brief トランスフォームを取得
        /// @return トランスフォームへの参照
        WorldTransform& GetTransform() { return transform_; }

        /// @brief トランスフォームを取得（const版）
        /// @return トランスフォームへのconst参照
        const WorldTransform& GetTransform() const { return transform_; }

        /// @brief モデルを取得
        /// @return モデルへのポインタ（nullptrの場合はモデルを持たない）
        Model* GetModel() { return model_.get(); }

        /// @brief モデルを取得（const版）
        /// @return モデルへのconstポインタ（nullptrの場合はモデルを持たない）
        const Model* GetModel() const { return model_.get(); }

        /// @brief オブジェクト名を設定
        /// @param name 設定する名前
        void SetName(const std::string& name) { name_ = name; }

        /// @brief 設定されたオブジェクト名を取得
        /// @return 設定された名前（空の場合はクラス名）
        const std::string& GetName() const { return name_; }

        /// @brief オブジェクト名を取得（派生クラスでオーバーライド推奨）
        /// @return オブジェクト名（シリアライズキーとしても使用）
        virtual const char* GetObjectName() const { return "GameObject"; }

        /// @brief JSON シリアライズ対象かどうかを取得
        bool IsSerializeEnabled() const { return shouldSerialize_; }

        /// @brief JSON シリアライズ対象かどうかを設定
        void SetSerializeEnabled(bool enable) { shouldSerialize_ = enable; }

#ifdef _DEBUG
        /// @brief ImGuiデバッグUI描画（基本パラメータ：Transform、Active）
        /// @return ImGuiで変更があった場合 true
        virtual bool DrawImGui();

        /// @brief ImGui拡張UI描画（派生クラスでオーバーライドして追加パラメータを表示）
        /// @return ImGuiで変更があった場合 true
        virtual bool DrawImGuiExtended() { return false; }

        /// @brief ImGui 編集コミット時コールバック型（Undo/Redo 用）
        using EditCommitCallback = std::function<void(
            GameObject*,
            const Vector3& translateBefore, const Vector3& rotateBefore,
            const Vector3& scaleBefore, bool activeBefore)>;

        /// @brief ImGui 編集コミット時コールバックを設定（Undo/Redo 用）
        void SetEditCommitCallback(EditCommitCallback cb) { onEditCommitted_ = std::move(cb); }

        /// @brief 個別保存リクエストコールバック型
        using SaveRequestCallback = std::function<void(GameObject*)>;

        /// @brief 個別保存リクエストコールバックを設定
        void SetSaveRequestCallback(SaveRequestCallback cb) { onSaveRequested_ = std::move(cb); }
#endif

    protected:
        // === 共通描画リソース ===

        /// @brief 3Dモデル
        std::unique_ptr<Model> model_;

        /// @brief ワールドトランスフォーム（位置・回転・スケール）
        WorldTransform transform_;

        /// @brief テクスチャハンドル
        TextureManager::LoadedTexture texture_;

        /// @brief コライダー（オブジェクトが所有）
        std::unique_ptr<Collider> collider_;

        /// @brief オブジェクト名（ImGui表示用）
        std::string name_;

        /// @brief アクティブ状態フラグ
        bool isActive_ = true;

        /// @brief 表示状態フラグ（falseにすると描画のみスキップ、Active=falseとは異なり更新は継続）
        bool isVisible_ = true;

        /// @brief 削除マークフラグ（フレーム終了時に自動削除される）
        bool markedForDestroy_ = false;

        /// @brief 自動更新フラグ（true: GameObjectManagerが自動で更新、false: 手動更新）
        bool autoUpdate_ = true;

        /// @brief 描画順序オーバーライド（未設定の場合は RenderPassType の優先度を使用）
        std::optional<int> renderOrder_;

        /// @brief JSON シリアライズ対象フラグ（falseにするとシーンデータに保存されない）
        bool shouldSerialize_ = true;

#ifdef _DEBUG
        // ImGui 編集追跡用（操作前スナップショット）
        Vector3 imguiSnapTranslate_ = { 0.0f, 0.0f, 0.0f };
        Vector3 imguiSnapRotate_    = { 0.0f, 0.0f, 0.0f };
        Vector3 imguiSnapScale_     = { 1.0f, 1.0f, 1.0f };
        bool    imguiSnapActive_    = true;
        EditCommitCallback onEditCommitted_;
        SaveRequestCallback onSaveRequested_;

        /// @brief マテリアルデバッグUI（モデルを持つオブジェクト共通）
        std::unique_ptr<MaterialDebugUI> materialDebugUI_;

        /// @brief 個別保存ボタンを描画するヘルパー（DrawImGui 内で使用）
        void DrawSaveButton();

        /// @brief マテリアルUIを描画するヘルパー（モデルがある場合のみ）
        /// @return 変更があった場合 true
        bool DrawMaterialImGui();
#endif
    };
}
