#pragma once

#include "Engine/Graphics/Render/RenderPassType.h"
#include "Engine/Graphics/PipelineStateManager.h"
#include "Engine/Math/Vector/Vector3.h"
#include "Engine/WorldTransfom/WorldTransform.h"
#include "Engine/Graphics/TextureManager.h"
#include "Engine/Graphics/Model/Model.h"

#include <memory>
#include <string>

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

#ifdef _DEBUG
        /// @brief ImGuiデバッグUI描画（基本パラメータ：Transform、Active）
        /// @return ImGuiで変更があった場合 true
        virtual bool DrawImGui();

        /// @brief ImGui拡張UI描画（派生クラスでオーバーライドして追加パラメータを表示）
        /// @return ImGuiで変更があった場合 true
        virtual bool DrawImGuiExtended() { return false; }

        /// @brief オブジェクト名を取得（派生クラスでオーバーライド推奨）
        /// @return オブジェクト名
        virtual const char* GetObjectName() const { return "GameObject"; }
#endif

    protected:
        // === 共通描画リソース ===

        /// @brief 3Dモデル
        std::unique_ptr<Model> model_;

        /// @brief ワールドトランスフォーム（位置・回転・スケール）
        WorldTransform transform_;

        /// @brief テクスチャハンドル
        TextureManager::LoadedTexture texture_;

        /// @brief オブジェクト名（ImGui表示用）
        std::string name_;

        /// @brief アクティブ状態フラグ
        bool isActive_ = true;

        /// @brief 削除マークフラグ（フレーム終了時に自動削除される）
        bool markedForDestroy_ = false;

        /// @brief 自動更新フラグ（true: GameObjectManagerが自動で更新、false: 手動更新）
        bool autoUpdate_ = true;
    };
}
