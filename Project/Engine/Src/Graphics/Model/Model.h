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
#include "Graphics/Render/DrawViewInfo.h"
#include "Graphics/Render/Model/ModelDrawPacket.h"
#include "Graphics/Render/Culling/ModelVisibility.h"
#include "Animation/AnimationPlayer.h"

// 前方宣言
namespace CoreEngine {
    class Camera;
    class GraphicsCore;
    class ResourceFactory;
    class LightBase;
    class ICustomShaderProvider;
    class CustomShaderPipeline;
    struct Skeleton;
}

namespace CoreEngine
{
    /// @brief 配置された3Dモデルのインスタンスクラス
    /// ModelResourceへの参照と、個別のトランスフォーム・マテリアルを持つ
    class Model {
    public:
        /// @brief デフォルトコンストラクタ
        Model() = default;

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
        /// @param view ビュー/パス情報（カメラ・ビュー種別・GBufferパスか）。
        ///             Hi-Z 適用可否やモーションベクター履歴の更新可否はこの情報だけで決まる
        /// @param textureHandle テクスチャハンドル（省略時はモデル組み込みテクスチャを使用）
        void Draw(const WorldTransform& transform, const DrawViewInfo& view,
            D3D12_GPU_DESCRIPTOR_HANDLE textureHandle = {});

        /// @brief 初期化されているか確認
        /// @return 初期化済みならtrue
        bool IsInitialized() const;

        /// @brief マテリアルインスタンスを取得（パラメータの直接操作用。範囲外は nullptr）
        /// @details Copy-on-Write。未オーバーライドのスロットは初回呼び出し時に
        ///          ModelResource の共有デフォルトから複製する。
        MaterialInstance* GetMaterial(size_t materialIndex = 0);

        /// @brief マテリアルインスタンスを取得（読み取り専用。未オーバーライドならリソース共有のデフォルトを返す）
        const MaterialInstance* GetMaterial(size_t materialIndex = 0) const {
            if (materialIndex >= materialInstances_.size()) return nullptr;
            if (materialInstances_[materialIndex]) return materialInstances_[materialIndex].get();
            return resource_ ? resource_->GetDefaultMaterial(static_cast<uint32_t>(materialIndex)) : nullptr;
        }

        /// @brief マテリアルスロット数を取得
        size_t GetMaterialCount() const { return materialInstances_.size(); }

        /// @brief 全マテリアルスロットに対して処理を実行する（モデル全体のティントや IBL 設定用）
        /// @note fn は書き込み前提のため、全スロットを GetMaterial() 経由で materialize してから渡す
        void ForEachMaterial(const std::function<void(MaterialInstance*)>& fn) {
            for (size_t i = 0; i < materialInstances_.size(); ++i) {
                if (MaterialInstance* mat = GetMaterial(i)) fn(mat);
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

        // WVP バッファのリングサイズ（スワップチェーンのバックバッファ数と一致させる。
        // ModelManager が InstanceBatchManager に渡すフレーム数と同じ値）
        static constexpr size_t kFrameBufferCount = 3;

        // スキニングモデルの即時描画（Draw）用 WVP バッファ。
        // フレームインデックスでリングバッファ化し、CPU が複数フレーム先行して
        // 書き込んでも GPU がまだ参照中の前フレームのデータを上書きしないようにする。
        std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kFrameBufferCount> gameTransformBuffers_;
        Matrix4x4 prevGameWVP_{}; // 前フレームのWVP行列（モーションベクター計算用）
        bool prevGameWVPInitialized_ = false; // false = 未初期化（初回フレームはMV=0にする）

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

        // 可視性評価（LOD選択・Hi-Zオクルージョン判定）。最適化の実装詳細はこちらが持つ
        ModelVisibility visibility_;

        // 内部ヘルパーメソッド
        /// @brief 即時描画（スキニングモデル）用の WVP 行列データを更新する
        void UpdateTransformationMatrix(const WorldTransform& transform, const DrawViewInfo& view);

        /// @brief SkinCluster のマトリックスパレットを指定スケルトンの姿勢で更新する
        void UpdateSkinCluster(const Skeleton& skeleton);

        /// @brief GPUスキニング(CS)がまだ実行されていなければ実行する
        /// @details 同一フレーム内でForward/GBuffer/Shadowが同じモデルを描画する際、
        ///          スキニング計算を1回に統合するためのガード。
        /// @param cmdList コマンドリスト
        /// @param restorePSO Dispatch後に復元するグラフィックスPSO（CSのDispatchでPSOスロットが上書きされるため）
        void EnsureGPUSkinning(ID3D12GraphicsCommandList* cmdList, ID3D12PipelineState* restorePSO);

        /// @brief 現在フレームに対応する Game 用 WVP バッファを取得（リングバッファから解決）
        ID3D12Resource* GetGameTransformBuffer() const;

        /// @brief サブメッシュのマテリアルスロットに対応するマテリアル定数バッファのGPUアドレスを取得（範囲外はスロット0）
        /// @details オーバーライド未発生のスロットは ModelResource 共有のデフォルトマテリアルのアドレスを返す。
        D3D12_GPU_VIRTUAL_ADDRESS MaterialCBVForSlot(uint32_t materialIndex) const;

        /// @brief スキニングモデル用の ModelDrawPacket を組み立てる
        ModelDrawPacket BuildSkinningDrawPacket(const SubMeshData& subMesh,
            D3D12_GPU_DESCRIPTOR_HANDLE baseColorTexture,
            D3D12_GPU_DESCRIPTOR_HANDLE normalTexture,
            D3D12_GPU_DESCRIPTOR_HANDLE metallicRoughnessTexture,
            D3D12_GPU_DESCRIPTOR_HANDLE occlusionTexture,
            D3D12_GPU_DESCRIPTOR_HANDLE emissiveTexture) const;
    };
}
