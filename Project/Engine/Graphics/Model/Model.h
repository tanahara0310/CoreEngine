#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <memory>
#include <vector>
#include <optional>

#include "ModelResource.h"
#include "Engine/WorldTransform/WorldTransform.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Graphics/Model/TransformationMatrix.h"
#include "Engine/Graphics/Model/Skeleton/SkinCluster.h"
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
        static void Initialize(CoreEngine::DirectXCommon* dxCommon);

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
        bool IsInitialized() const;

        /// @brief マテリアルインスタンスを取得（パラメータの直接操作用）
        /// @return MaterialInstance へのポインタ
        MaterialInstance* GetMaterial() { return materialInstance_.get(); }
        const MaterialInstance* GetMaterial() const { return materialInstance_.get(); }

        /// @brief Skeletonを取得（スケルトンアニメーションから同期）
        /// @return Skeleton（存在しない場合はnullopt）
        const std::optional<Skeleton>& GetSkeleton() const;

        /// @brief SkinClusterを持っているか確認
        /// @return SkinClusterがあればtrue
        bool HasSkinCluster() const;

        /// @brief アニメーションコントローラーを持っているか確認
        /// @return コントローラーがあればtrue
        bool HasAnimationController() const;

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
        RenderType GetRenderType() const;

        /// @brief ModelResourceを取得
        /// @return ModelResourceへのポインタ（nullptrの場合は未初期化）
        ModelResource* GetModelResource();

        /// @brief ModelResourceを取得（const版）
        /// @return ModelResourceへのconstポインタ（nullptrの場合は未初期化）
        const ModelResource* GetModelResource() const;

        void SetModelResource(ModelResource* resource);

    private:
        // 参照するModelResource
        ModelResource* resource_ = nullptr;

        // インスタンス固有のマテリアル
        std::unique_ptr<MaterialInstance> materialInstance_;

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
