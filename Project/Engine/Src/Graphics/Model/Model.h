#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <array>
#include <memory>
#include <vector>
#include <optional>

#include "ModelResource.h"
#include "ModelRenderContext.h"
#include "WorldTransform/WorldTransform.h"
#include "Graphics/Material/MaterialInstance.h"
#include "Graphics/Model/TransformationMatrix.h"
#include "Graphics/Model/Skeleton/SkinCluster.h"
#include "Graphics/Render/Model/ModelDrawPacket.h"
#include "Animation/IAnimationController.h"
#include "Animation/IAnimationControllerFactory.h"
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

        /// @brief IBLテクスチャ（Irradiance/Prefiltered/BRDF LUT）がレンダラーに全て設定済みか確認
        bool IsIBLAvailable() const;

        /// @brief モデルリソースに法線マップテクスチャがあるか確認
        bool HasNormalMap() const;

        /// @brief モデルリソースに MetallicRoughness テクスチャがあるか確認
        bool HasMetallicRoughnessMap() const;

        /// @brief モデルリソースに AO（陰影）テクスチャがあるか確認
        bool HasOcclusionMap() const;

        /// @brief 初期化（アニメーションコントローラーなし）
        /// @param resource 共有するModelResourceのポインタ
        /// @param ctx 描画依存コンテキスト
        void Initialize(ModelResource* resource, const ModelRenderContext& ctx);

        /// @brief 初期化（アニメーションコントローラーあり）
        /// @param resource 共有するModelResourceのポインタ
        /// @param controller アニメーションコントローラー
        /// @param ctx 描画依存コンテキスト
        void Initialize(ModelResource* resource, std::unique_ptr<IAnimationController> controller, const ModelRenderContext& ctx);

        /// @brief モデルを描画（スキニングモデルか通常モデルかは内部で自動判別）
        /// @param transform ワールドトランスフォーム
        /// @param camera カメラ（ICamera インターフェース）
        /// @param textureHandle テクスチャハンドル（省略時はモデル組み込みテクスチャを使用）
        /// @param slot 使用する WVP バッファスロット（Game=通常/GBuffer, Scene=エディタ）
        void Draw(const WorldTransform& transform, const CoreEngine::ICamera* camera,
            D3D12_GPU_DESCRIPTOR_HANDLE textureHandle = {},
            TransformBufferSlot slot = TransformBufferSlot::Game);

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

        /// @brief アニメーションコントローラーファクトリーを設定（スケルトンモデルのみ必須）
        /// SwitchAnimation() / SwitchAnimationWithBlend() を呼び出す場合は ModelManager が事前に設定する
        void SetAnimationControllerFactory(std::unique_ptr<IAnimationControllerFactory> factory);

    private:
        // 描画に必要な固定依存（ModelManager から注入される）
        ModelRenderContext renderContext_;

        // 参照するModelResource
        ModelResource* resource_ = nullptr;

        // インスタンス固有のマテリアル
        std::unique_ptr<MaterialInstance> materialInstance_;

        static constexpr size_t kTransformBufferCount = 3;

        // WVP行列用のリソース（ビュー/パスごとに分離）
        std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kTransformBufferCount> wvpResources_;

        // Skeleton（スケルトンアニメーターから同期される）
        std::optional<Skeleton> skeleton_;

        // SkinCluster（存在する場合）
        std::optional<SkinCluster> skinCluster_;

        // アニメーションコントローラー
        std::unique_ptr<IAnimationController> animationController_;

        // スケルトンアニメーター等の生成ファクトリー（スケルトンモデルのみ設定される）
        std::unique_ptr<IAnimationControllerFactory> animationFactory_;

        // 内部ヘルパーメソッド
        /// @brief WVP行列データを更新（slot で使用バッファを指定）
        void UpdateTransformationMatrix(const WorldTransform& transform, const ICamera* camera,
            TransformBufferSlot slot);

        /// @brief SkinClusterを更新（スケルトンアニメーションの場合のみ）
        void UpdateSkinCluster();

        /// @brief 指定スロット用行列バッファを取得
        ID3D12Resource* GetTransformBuffer(TransformBufferSlot slot) const;

        /// @brief 通常モデル用の ModelDrawPacket を組み立てる
        ModelDrawPacket BuildNormalDrawPacket(const SubMeshData& subMesh,
            D3D12_GPU_DESCRIPTOR_HANDLE baseColorTexture,
            D3D12_GPU_DESCRIPTOR_HANDLE normalTexture,
            D3D12_GPU_DESCRIPTOR_HANDLE metallicRoughnessTexture,
            D3D12_GPU_DESCRIPTOR_HANDLE occlusionTexture,
            TransformBufferSlot slot) const;

        /// @brief スキニングモデル用の ModelDrawPacket を組み立てる
        ModelDrawPacket BuildSkinningDrawPacket(const SubMeshData& subMesh,
            D3D12_GPU_DESCRIPTOR_HANDLE baseColorTexture,
            D3D12_GPU_DESCRIPTOR_HANDLE normalTexture,
            D3D12_GPU_DESCRIPTOR_HANDLE metallicRoughnessTexture,
            D3D12_GPU_DESCRIPTOR_HANDLE occlusionTexture,
            TransformBufferSlot slot) const;
    };
}
