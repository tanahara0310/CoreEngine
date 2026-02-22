#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <memory>
#include <vector>
#include <optional>

#include "ModelResource.h"
#include "Engine/WorldTransfom/WorldTransform.h"
#include "Engine/Graphics/Material/MaterialManager.h"
#include "Engine/Graphics/Structs/TransformationMatrix.h"
#include "Engine/Graphics/Structs/SkinCluster.h"
#include "Animation/IAnimationController.h"
#include "Skeleton/Skeleton.h"

// 前方宣言
namespace CoreEngine {
    class ICamera;
    class DirectXCommon;
    class ResourceFactory;
    class LightBase;
    class ShadowMapManager;
}

/// @brief 配置された3Dモデルのインスタンスクラス
/// ModelResourceへの参照と、個別のトランスフォーム・マテリアルを持つ

namespace CoreEngine
{
    class Model {
    public:
        /// @brief モデルの描画タイプ
        enum class RenderType {
            Normal,   // 通常モデル
            Skinning  // スキニングモデル
        };

        /// @brief デフォルトコンストラクタ
        Model() = default;

        /// @brief デストラクタ
        ~Model() = default;

        /// @brief 静的初期化（全Modelインスタンス共通のリソースを初期化）
        /// @param dxCommon DirectXCommonのポインタ
        /// @param factory リソースファクトリのポインタ
        static void Initialize(CoreEngine::DirectXCommon* dxCommon, CoreEngine::ResourceFactory* factory);

        /// @brief ShadowMapManagerを設定（ライトVP行列の一元管理）
        /// @param shadowMapManager ShadowMapManagerのポインタ
        static void SetShadowMapManager(ShadowMapManager* shadowMapManager);

        /// @brief ModelRendererを設定（ルートパラメータインデックス取得用）
        /// @param modelRenderer ModelRendererのポインタ
        static void SetModelRenderer(class ModelRenderer* modelRenderer);

        /// @brief SkinnedModelRendererを設定（ルートパラメータインデックス取得用）
        /// @param skinnedModelRenderer SkinnedModelRendererのポインタ
        static void SetSkinnedModelRenderer(class SkinnedModelRenderer* skinnedModelRenderer);

        /// @brief ShadowMapRendererを設定（ルートパラメータインデックス取得用）
        /// @param shadowMapRenderer ShadowMapRendererのポインタ
        static void SetShadowMapRenderer(class ShadowMapRenderer* shadowMapRenderer);

        /// @brief 初期化（アニメーションコントローラーなし）
        /// @param resource 共有するModelResourceのポインタ
        void Initialize(ModelResource* resource);

        /// @brief 初期化（アニメーションコントローラーあり）
        /// @param resource 共有するModelResourceのポインタ
        /// @param controller アニメーションコントローラー
        void Initialize(ModelResource* resource, std::unique_ptr<IAnimationController> controller);

        /// @brief モデルを描画（スキニングモデルか通常モデルかは内部で自動判別）
        /// @param transform ワールドトランスフォーム
        /// @param camera カメラ（ICamera インターフェース）
        /// @param textureHandle テクスチャハンドル（省略時はモデル組み込みテクスチャを使用）
        void Draw(const WorldTransform& transform, const CoreEngine::ICamera* camera,
            D3D12_GPU_DESCRIPTOR_HANDLE textureHandle = {});

        /// @brief シャドウマップ用の描画（深度のみ）
        /// @param transform ワールドトランスフォーム
        /// @param cmdList コマンドリスト
        void DrawShadow(const WorldTransform& transform, ID3D12GraphicsCommandList* cmdList);

        /// @brief 初期化されているか確認
        /// @return 初期化済みならtrue
        bool IsInitialized() const { return resource_ != nullptr && materialManager_ != nullptr; }

        /// @brief マテリアルマネージャーを取得
        /// @return マテリアルマネージャー
        MaterialManager* GetMaterialManager() { return materialManager_.get(); }
        const MaterialManager* GetMaterialManager() const { return materialManager_.get(); }

        /// @brief UV変換行列を設定
        /// @param uvTransform UV変換行列
        void SetUVTransform(const Matrix4x4& uvTransform);

        /// @brief UV変換行列を取得
        /// @return UV変換行列
        Matrix4x4 GetUVTransform() const;

        /// @brief Skeletonを取得（スケルトンアニメーションから同期）
        /// @return Skeleton（存在しない場合はnullopt）
        const std::optional<Skeleton>& GetSkeleton() const { return skeleton_; }

        /// @brief SkinClusterを持っているか確認
        /// @return SkinClusterがあればtrue
        bool HasSkinCluster() const { return skinCluster_.has_value(); }

        /// @brief アニメーションコントローラーを持っているか確認
        /// @return コントローラーがあればtrue
        bool HasAnimationController() const { return animationController_ != nullptr; }

        /// @brief アニメーションを更新
        /// @param deltaTime デルタタイム（秒）
        void UpdateAnimation(float deltaTime);

        /// @brief アニメーションをリセット
        void ResetAnimation();

        /// @brief アニメーション時刻を取得
        /// @return 現在のアニメーション時刻（秒）
        float GetAnimationTime() const;

        /// @brief アニメーションが終了したか確認
        /// @return アニメーションが終了していればtrue
        bool IsAnimationFinished() const;

        /// @brief アニメーションを切り替える（スケルトンアニメーション専用）
        /// @param animationName 切り替えるアニメーション名
        /// @param loop ループ再生するか
        /// @return 成功したらtrue
        bool SwitchAnimation(const std::string& animationName, bool loop = true);

        /// @brief アニメーションをブレンドしながら切り替える
        /// @param animationName 切り替えるアニメーション名
        /// @param blendDuration ブレンド時間（秒）
        /// @param loop ループ再生するか
        /// @return 成功したらtrue
        bool SwitchAnimationWithBlend(const std::string& animationName, float blendDuration = 0.3f, bool loop = true);

        /// @brief 描画タイプを取得（スキニングか通常かを判別）
        /// @return 描画タイプ
        RenderType GetRenderType() const {
            return HasSkinCluster() ? RenderType::Skinning : RenderType::Normal;
        }

        /// @brief マテリアルカラーを設定
        /// @param color カラー（RGBA）
        void SetMaterialColor(const Vector4& color) {
            if (materialManager_) {
                materialManager_->GetConstants()->color = color;
            }
        }

        /// @brief マテリアルカラーを取得
        /// @return 現在のカラー
        Vector4 GetMaterialColor() const {
            if (materialManager_) {
                return materialManager_->GetConstants()->color;
            }
            return Vector4{ 1.0f, 1.0f, 1.0f, 1.0f };
        }

        /// @brief 環境マップを有効/無効にする
        /// @param enable true: 有効, false: 無効
        void SetEnableEnvironmentMap(bool enable) {
            if (materialManager_) {
                materialManager_->GetConstants()->enableEnvironmentMap = enable ? 1 : 0;
            }
        }

        /// @brief 環境マップが有効かどうかを取得
        /// @return true: 有効, false: 無効
        bool IsEnableEnvironmentMap() const {
            if (materialManager_) {
                return materialManager_->GetConstants()->enableEnvironmentMap != 0;
            }
            return false;
        }

        /// @brief 環境マップの反射強度を設定
        /// @param intensity 反射強度 (0.0-1.0)
        void SetEnvironmentMapIntensity(float intensity) {
            if (materialManager_) {
                materialManager_->GetConstants()->environmentMapIntensity = intensity;
            }
        }

        /// @brief 環境マップの反射強度を取得
        /// @return 現在の反射強度
        float GetEnvironmentMapIntensity() const {
            if (materialManager_) {
                return materialManager_->GetConstants()->environmentMapIntensity;
            }
            return 0.0f;
        }

        /// @brief ModelResourceを取得
        /// @return ModelResourceへのポインタ（nullptrの場合は未初期化）
        ModelResource* GetModelResource() { return resource_; }

        /// @brief ModelResourceを取得（const版）
        /// @return ModelResourceへのconstポインタ（nullptrの場合は未初期化）
        const ModelResource* GetModelResource() const { return resource_; }

        void SetModelResource(ModelResource* resource);

    private:
        // 参照するModelResource
        ModelResource* resource_ = nullptr;

        // インスタンス固有のマテリアル
        std::unique_ptr<MaterialManager> materialManager_;

        // WVP行列用のリソース（1インスタンスにつき1つのみ）
        Microsoft::WRL::ComPtr<ID3D12Resource> wvpResource_;

        // Skeleton（スケルトンアニメーターから同期される）
        std::optional<Skeleton> skeleton_;

        // SkinCluster（存在する場合）
        std::optional<SkinCluster> skinCluster_;

        // アニメーションコントローラー
        std::unique_ptr<IAnimationController> animationController_;

        // 内部ヘルパーメソッド
        /// @brief WVP行列データを更新
        void UpdateTransformationMatrix(const WorldTransform& transform, const ICamera* camera);

        /// @brief SkinClusterを更新（スケルトンアニメーションの場合のみ）
        void UpdateSkinCluster();

        /// @brief 通常モデルの描画コマンドを設定
        void SetupNormalDrawCommands(ID3D12GraphicsCommandList* cmdList,
            D3D12_GPU_DESCRIPTOR_HANDLE baseColorTexture,
            D3D12_GPU_DESCRIPTOR_HANDLE normalTexture = {},
            D3D12_GPU_DESCRIPTOR_HANDLE metallicRoughnessTexture = {},
            D3D12_GPU_DESCRIPTOR_HANDLE occlusionTexture = {});

        /// @brief スキニングモデルの描画コマンドを設定
        void SetupSkinningDrawCommands(ID3D12GraphicsCommandList* cmdList,
            D3D12_GPU_DESCRIPTOR_HANDLE baseColorTexture,
            D3D12_GPU_DESCRIPTOR_HANDLE normalTexture = {},
            D3D12_GPU_DESCRIPTOR_HANDLE metallicRoughnessTexture = {},
            D3D12_GPU_DESCRIPTOR_HANDLE occlusionTexture = {});
    };
}
