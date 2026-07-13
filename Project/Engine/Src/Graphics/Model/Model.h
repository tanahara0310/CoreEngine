#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <array>
#include <functional>
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
#include "Animation/AnimationPlayer.h"

// 前方宣言
namespace CoreEngine {
    class ICamera;
    class DirectXCommon;
    class ResourceFactory;
    class LightBase;
    class ShadowMapManager;
    class ICustomShaderProvider;
    class CustomShaderPipeline;
    struct Skeleton;
}

/// @brief 配置された3Dモデルのインスタンスクラス
/// ModelResourceへの参照と、個別のトランスフォーム・マテリアルを持つ

namespace CoreEngine
{
    class Model {
    public:
        /// @brief デフォルトコンストラクタ
        Model() = default;

        /// @brief デストラクタ
        ~Model() = default;

        /// @brief IBLテクスチャ（Irradiance/Prefiltered/BRDF LUT）がレンダラーに全て設定済みか確認
        bool IsIBLAvailable() const;

        /// @brief 指定マテリアルスロットに法線マップテクスチャがあるか確認
        bool HasNormalMap(size_t materialIndex = 0) const;

        /// @brief 指定マテリアルスロットに MetallicRoughness テクスチャがあるか確認
        bool HasMetallicRoughnessMap(size_t materialIndex = 0) const;

        /// @brief 指定マテリアルスロットに AO（陰影）テクスチャがあるか確認
        bool HasOcclusionMap(size_t materialIndex = 0) const;

        /// @brief 初期化
        /// @param resource 共有するModelResourceのポインタ
        /// @param ctx 描画依存コンテキスト
        void Initialize(ModelResource* resource, const ModelRenderContext& ctx);

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
        /// @param materialIndex マテリアルスロットインデックス（サブメッシュの materialIndex に対応）
        /// @return MaterialInstance へのポインタ（範囲外は nullptr）
        MaterialInstance* GetMaterial(size_t materialIndex = 0) {
            return materialIndex < materialInstances_.size() ? materialInstances_[materialIndex].get() : nullptr;
        }
        const MaterialInstance* GetMaterial(size_t materialIndex = 0) const {
            return materialIndex < materialInstances_.size() ? materialInstances_[materialIndex].get() : nullptr;
        }

        /// @brief マテリアルスロット数を取得
        size_t GetMaterialCount() const { return materialInstances_.size(); }

        /// @brief 全マテリアルスロットに対して処理を実行する（モデル全体のティントや IBL 設定用）
        void ForEachMaterial(const std::function<void(MaterialInstance*)>& fn) {
            for (auto& mat : materialInstances_) {
                if (mat) fn(mat.get());
            }
        }

        /// @brief SkinClusterを持っているか確認
        /// @return SkinClusterがあればtrue
        bool HasSkinCluster() const;

        /// @brief アニメーションプレイヤーを設定する（ModelManager::CreateSkeletonModel が注入する）
        void SetAnimationPlayer(std::unique_ptr<AnimationPlayer> player);

        /// @brief アニメーションプレイヤーを取得（切り替え・ブレンド等の操作用）
        /// @return AnimationPlayer へのポインタ（アニメーションを持たない場合は nullptr）
        AnimationPlayer* GetAnimationPlayer() const { return animationPlayer_.get(); }

        /// @brief アニメーションを更新し、スケルトンの姿勢を SkinCluster に反映する
        /// @param deltaTime デルタタイム（秒）
        void UpdateAnimation(float deltaTime);

        /// @brief ModelResourceを取得
        /// @return ModelResourceへのポインタ（nullptrの場合は未初期化）
        ModelResource* GetModelResource();

        /// @brief ModelResourceを取得（const版）
        /// @return ModelResourceへのconstポインタ（nullptrの場合は未初期化）
        const ModelResource* GetModelResource() const;

        /// @brief 描画システムが使用する WVP バッファスロットをグローバルに設定する
        /// BaseScene::Draw() が各パスの直前に呼び出し、
        /// 明示的にスロットを指定しない全モデルの Draw() に反映される。
        static void SetCurrentRenderSlot(TransformBufferSlot slot) { s_currentRenderSlot_ = slot; }

        /// @brief 現在設定されているグローバルレンダースロットを取得する
        static TransformBufferSlot GetCurrentRenderSlot() { return s_currentRenderSlot_; }

        /// @brief カスタムシェーダー用フォワード PSO を設定する（nullptr = 既定シェーダーを使用）
        /// @note ModelGameObject::Initialize() 内部から呼び出される。直接呼ぶ必要はない。
        void SetCustomForwardPSO(ID3D12PipelineState* pso) { customForwardPSO_ = pso; }

        /// @brief カスタムシェーダー用 RootSignature を設定する（nullptr = 既定 RS を使用）
        /// @note ModelGameObject::Initialize() 内部から呼び出される。直接呼ぶ必要はない。
        void SetCustomRootSignature(ID3D12RootSignature* rs) { customRootSignature_ = rs; }

        /// @brief カスタムパイプラインオブジェクトを設定する（BindCustomResources に渡される）
        /// @note ModelGameObject::Initialize() 内部から呼び出される。直接呼ぶ必要はない。
        void SetCustomPipeline(const CustomShaderPipeline* pipeline) { customPipeline_ = pipeline; }

        /// @brief カスタムリソースバインドプロバイダを設定する（nullptr = なし）
        /// @note SetCustomForwardPSO() と合わせて ModelGameObject::Initialize() 内部から呼び出される。
        void SetCustomShaderProvider(const ICustomShaderProvider* provider) { customProvider_ = provider; }

    private:
        // 描画に必要な固定依存（ModelManager から注入される）
        ModelRenderContext renderContext_;

        // 参照するModelResource
        ModelResource* resource_ = nullptr;

        // インスタンス固有のマテリアル（マテリアルスロット数分。サブメッシュの materialIndex で参照）
        std::vector<std::unique_ptr<MaterialInstance>> materialInstances_;

        static constexpr size_t kTransformBufferCount = 3;

        // 描画システムが制御するグローバルスロット
        // BaseScene::Draw() によってパス開始前に設定される
        inline static TransformBufferSlot s_currentRenderSlot_ = TransformBufferSlot::Game;

        // WVP行列用のリソース（ビュー/パスごとに分離）
        std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kTransformBufferCount> wvpResources_;

        // 前フレームのWVP行列（モーションベクター計算用）
        // false = 未初期化（初回フレームはprevWVP=currentWVPとして扱い、MV=0にする）
        std::array<Matrix4x4, kTransformBufferCount> prevWVP_{};
        std::array<bool, kTransformBufferCount> prevWVPInitialized_{};

        // SkinCluster（存在する場合）
        std::optional<SkinCluster> skinCluster_;

        // アニメーションプレイヤー（スケルトンアニメーションを持つモデルのみ設定される）
        // スケルトンの実体はプレイヤー内のコントローラーが所有し、Model はコピーを持たない
        std::unique_ptr<AnimationPlayer> animationPlayer_;

        // カスタムシェーダー用フォワード PSO（nullptr = 既定 ModelRenderer の PSO を使用）
        ID3D12PipelineState* customForwardPSO_ = nullptr;

        // カスタムシェーダー用 RootSignature（nullptr = 既定 ModelRenderer の RS を使用）
        ID3D12RootSignature* customRootSignature_ = nullptr;

        // カスタムパイプラインオブジェクト（BindCustomResources に渡される）
        const CustomShaderPipeline* customPipeline_ = nullptr;

        // カスタムリソースバインドプロバイダ（nullptr = 追加バインドなし）
        const ICustomShaderProvider* customProvider_ = nullptr;

        // 内部ヘルパーメソッド
        /// @brief WVP行列データを更新（slot で使用バッファを指定）
        void UpdateTransformationMatrix(const WorldTransform& transform, const ICamera* camera,
            TransformBufferSlot slot);

        /// @brief SkinCluster のマトリックスパレットを指定スケルトンの姿勢で更新する
        void UpdateSkinCluster(const Skeleton& skeleton);

        /// @brief GPUスキニング(CS)がまだ実行されていなければ実行する
        /// @details 同一フレーム内でForward/GBuffer/Shadowが同じモデルを描画する際、
        ///          スキニング計算を1回に統合するためのガード。
        /// @param cmdList コマンドリスト
        /// @param restorePSO Dispatch後に復元するグラフィックスPSO（CSのDispatchでPSOスロットが上書きされるため）
        void EnsureGPUSkinning(ID3D12GraphicsCommandList* cmdList, ID3D12PipelineState* restorePSO);

        /// @brief 指定スロット用行列バッファを取得
        ID3D12Resource* GetTransformBuffer(TransformBufferSlot slot) const;

        /// @brief サブメッシュのマテリアルスロットに対応する MaterialInstance を取得（範囲外はスロット0）
        MaterialInstance* MaterialForSlot(uint32_t materialIndex) const;

        /// @brief 通常モデル用の ModelDrawPacket を組み立てる
        ModelDrawPacket BuildNormalDrawPacket(const SubMeshData& subMesh,
            D3D12_GPU_DESCRIPTOR_HANDLE baseColorTexture,
            D3D12_GPU_DESCRIPTOR_HANDLE normalTexture,
            D3D12_GPU_DESCRIPTOR_HANDLE metallicRoughnessTexture,
            D3D12_GPU_DESCRIPTOR_HANDLE occlusionTexture,
            D3D12_GPU_DESCRIPTOR_HANDLE emissiveTexture,
            TransformBufferSlot slot) const;

        /// @brief スキニングモデル用の ModelDrawPacket を組み立てる
        ModelDrawPacket BuildSkinningDrawPacket(const SubMeshData& subMesh,
            D3D12_GPU_DESCRIPTOR_HANDLE baseColorTexture,
            D3D12_GPU_DESCRIPTOR_HANDLE normalTexture,
            D3D12_GPU_DESCRIPTOR_HANDLE metallicRoughnessTexture,
            D3D12_GPU_DESCRIPTOR_HANDLE occlusionTexture,
            D3D12_GPU_DESCRIPTOR_HANDLE emissiveTexture,
            TransformBufferSlot slot) const;
    };
}
