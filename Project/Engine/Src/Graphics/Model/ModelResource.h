#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <string>
#include <map>
#include <optional>

#include "Graphics/Model/MaterialAsset.h"
#include "Graphics/Model/ModelData.h"
#include "Graphics/Model/Node.h"
#include "Graphics/Material/MaterialInstance.h"
#include "Animation/Animation.h"
#include "Skeleton/Skeleton.h"
#include "Math/Geometry/Shapes.h"
#include <memory>
#include <vector>

namespace CoreEngine
{
    // 前方宣言
    class DirectXCommon;
    class ResourceFactory;
    class TextureManager;

    /// @brief モデルの共有リソースを管理するクラス
    /// モデルファイル1個分のメッシュデータとGPUリソースを保持
    /// 複数のModelInstanceから参照される
    class ModelResource {
    public:
        /// @brief デフォルトコンストラクタ
        ModelResource() = default;

        /// @brief デストラクタ
        ~ModelResource() = default;

        /// @brief 初期化
        /// @param dxCommon DirectXCommonのポインタ
        /// @param factory リソースファクトリのポインタ
        /// @param textureMg テクスチャマネージャーのポインタ
        void Initialize(DirectXCommon* dxCommon, ResourceFactory* factory, TextureManager* textureMg);

        /// @brief モデルファイルの読み込みとGPU転送（OBJ、glTF、FBXなど対応）
        /// @param directoryPath ディレクトリパス
        /// @param filename ファイル名
        void LoadFromFile(const std::string& directoryPath, const std::string& filename);

        /// @brief プログラム生成されたモデルデータからGPUリソースを作成する
        /// @param data 頂点・インデックス・サブメッシュを含むモデルデータ
        /// @param name デバッグ用の識別名
        void LoadFromModelData(ModelData&& data, const std::string& name = "");

        /// @brief GPUリソースが作成されているか確認
        /// @return リソースが有効ならtrue
        bool IsLoaded() const { return isLoaded_; }

        /// @brief ファイルパスを取得（デバッグ用）
        /// @return ファイルパス
        const std::string& GetFilePath() const { return filePath_; }

        /// @brief 頂点数を取得
        /// @return 頂点数
        UINT GetVertexCount() const { return vertexCount_; }

        /// @brief RootNodeを取得
        /// @return RootNode
        const Node& GetRootNode() const { return rootNode_; }

        /// @brief Skeletonを取得
        /// @return Skeleton（存在しない場合はnullopt）
        const std::optional<Skeleton>& GetSkeleton() const { return skeleton_; }

        /// @brief ModelDataを取得
        /// @return ModelData
        const ModelData& GetModelData() const { return modelData_; }

        /// @brief アニメーションを持っているか確認
        /// @return アニメーションがあればtrue
        bool HasAnimation() const { return !animations_.empty(); }

        /// @brief アニメーションを取得
        /// @param name アニメーション名（空文字列の場合は最初のアニメーション）
        /// @return アニメーションのポインタ（存在しない場合はnullptr）
        const Animation* GetAnimation(const std::string& name = "") const;

        /// @brief 全てのアニメーションを取得
        /// @return アニメーションマップ
        const std::map<std::string, Animation>& GetAnimations() const { return animations_; }

        /// @brief アニメーションを追加
        /// @param name アニメーション名
        /// @param animation アニメーションデータ
        void AddAnimation(const std::string& name, const Animation& animation);

        /// @brief ローカル空間のバウンディングボックスを取得
        /// @return ローカル空間AABB
        const BoundingBox& GetLocalBoundingBox() const { return localBoundingBox_; }

        /// @brief サブメッシュ単位のローカル AABB を取得（Hi-Z オクルージョンカリングの判定単位）
        /// @details LOD0 のインデックス範囲から算出。簡略化 LOD は同一頂点の部分集合のため
        ///          LOD0 の AABB で保守的に覆える。
        ///          範囲外はサブメッシュとの対応ズレ（バグ）なので Debug では assert で停止し、
        ///          Release ではモデル全体の AABB へフォールバック（初回のみ警告ログ）する。
        /// @param subMeshIndex サブメッシュインデックス
        const BoundingBox& GetSubMeshLocalBounds(uint32_t subMeshIndex) const;

        /// @brief サブメッシュ情報を取得
        /// @return サブメッシュデータのベクター
        const std::vector<SubMeshData>& GetSubMeshes() const { return modelData_.subMeshes; }

        /// @brief マテリアルデータを取得
        /// @return マテリアルデータのベクター
        const std::vector<MaterialAsset>& GetMaterials() const { return modelData_.materials; }

        /// @brief 指定マテリアルのPBRテクスチャハンドルを取得
        /// @param materialIndex マテリアルインデックス
        /// @return PBRテクスチャハンドル構造体
        struct PBRTextureHandles {
            D3D12_GPU_DESCRIPTOR_HANDLE baseColor = {};
            D3D12_GPU_DESCRIPTOR_HANDLE metallicRoughness = {};
            D3D12_GPU_DESCRIPTOR_HANDLE normal = {};
            D3D12_GPU_DESCRIPTOR_HANDLE occlusion = {};
            D3D12_GPU_DESCRIPTOR_HANDLE emissive = {};

            /// @brief 実テクスチャが存在するか（falseの場合はフォールバック白テクスチャ）
            bool hasMetallicRoughness = false;
            bool hasNormal = false;
            bool hasOcclusion = false;
            bool hasEmissive = false;
        };
        const PBRTextureHandles& GetMaterialTextures(uint32_t materialIndex) const;

        /// @brief 全 Model インスタンスで共有するデフォルトマテリアル（アセット既定値）を取得
        /// @details Model はオーバーライドが発生するまで自前の MaterialInstance を持たず、
        ///          このリソース共有インスタンスの GPU アドレスをそのまま使う（Copy-on-Write）。
        ///          同一モデルの複数配置がインスタンシングバッチとして統合される前提条件になる。
        /// @param materialIndex マテリアルインデックス（範囲外はスロット0にクランプ）
        /// @return 共有 MaterialInstance へのポインタ（未ロードなら nullptr）
        const MaterialInstance* GetDefaultMaterial(uint32_t materialIndex) const;

        /// @brief 頂点バッファビューを取得
        const D3D12_VERTEX_BUFFER_VIEW& GetVertexBufferView() const { return vertexBufferView_; }

        /// @brief インデックスバッファビューを取得
        const D3D12_INDEX_BUFFER_VIEW& GetIndexBufferView() const { return indexBufferView_; }

        /// @brief 総インデックス数を取得
        UINT GetIndexCount() const { return indexCount_; }

        // ===== DXR 加速構造 =====

        /// @brief 頂点バッファリソースを取得（BLAS 構築用）
        ID3D12Resource* GetVertexBuffer() const { return vertexBuffer_.Get(); }

        /// @brief インデックスバッファリソースを取得（BLAS 構築用）
        ID3D12Resource* GetIndexBuffer() const { return indexBuffer_.Get(); }

        /// @brief BLAS インデックスを設定
        void SetBLASIndex(UINT index) { blasIndex_ = index; }

        /// @brief BLAS インデックスを取得（UINT_MAX = 未構築）
        UINT GetBLASIndex() const { return blasIndex_; }

        /// @brief BLAS が構築済みか
        bool HasBLAS() const { return blasIndex_ != UINT_MAX; }

    private:
        /// @brief modelData_.vertices/indices からVB/IB・バッファビュー・ローカルAABBを構築する
        /// LoadFromFile/LoadFromModelDataの共通処理（呼び出し前にmodelData_が設定済みであること）
        void CreateGeometryBuffers();

        /// @brief サブメッシュごとに簡略化インデックス（LOD1/LOD2）を生成し indices 末尾へ追記する
        /// 頂点バッファは共有のため追加しない。ハイポリモデルのみ対象（小物はスキップ）。
        void GenerateSubMeshLods();

        /// @brief meshoptimizer で GPU 向けにジオメトリを再配置する（GB/IB アップロード前に呼ぶ）。
        /// 頂点キャッシュ最適化＋オーバードロー最適化を全 LOD 範囲へ適用（VB 不変で安全）。
        /// 静的モデル（skinClusterData 空）のみ VertexFetch 最適化で VB を並べ替える
        /// （スキンモデルは influence バッファが頂点インデックス参照のため VB 並べ替え不可）。
        void OptimizeGeometryForGpu();

        /// @brief マテリアルスロット数分の共有デフォルト MaterialInstance を作成する
        /// LoadFromFile/LoadFromModelData の末尾（テクスチャハンドル確定後）で呼ぶ。
        void CreateDefaultMaterials();

        Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer_;
        D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
        UINT vertexCount_ = 0;

        Microsoft::WRL::ComPtr<ID3D12Resource> indexBuffer_;
        D3D12_INDEX_BUFFER_VIEW indexBufferView_{};
        UINT indexCount_ = 0;

        // VRAM（DEFAULT ヒープ）へコピーするための中間バッファ。
        // コピーコマンドは記録中のコマンドリストへ積まれるため、GPU 実行が完了するまで
        // 生存させる必要がある（TextureLoadedResource::intermediate と同じ扱い）。
        Microsoft::WRL::ComPtr<ID3D12Resource> vertexUploadBuffer_;
        Microsoft::WRL::ComPtr<ID3D12Resource> indexUploadBuffer_;

        ModelData modelData_;
        Node rootNode_;
        std::optional<Skeleton> skeleton_;

        DirectXCommon* dxCommon_ = nullptr;
        ResourceFactory* resourceFactory_ = nullptr;
        TextureManager* textureManager_ = nullptr;

        std::string filePath_;
        bool isLoaded_ = false;

        // DXR BLAS インデックス（UINT_MAX = 未構築）
        UINT blasIndex_ = UINT_MAX;

        // ローカル空間のバウンディングボックス（頂点データから算出）
        BoundingBox localBoundingBox_;

        // サブメッシュ単位のローカル AABB（LOD0 範囲から算出）
        // SubMeshData 本体でなく別配列で持つのは、値コピーされる SubMeshData のレイアウトを
        // 不変に保つため（サイズ変更×増分ビルド中断でヒープ破損事故の前歴あり）。
        // 不変条件: 添字は modelData_.subMeshes と 1:1 対応し、サイズは
        // CreateGeometryBuffers で必ず一致させる（ズレは GetSubMeshLocalBounds が検出する）
        std::vector<BoundingBox> subMeshLocalBounds_;

        // GetSubMeshLocalBounds の範囲外警告を初回のみ出すためのフラグ（ログスパム防止）
        mutable bool subMeshBoundsRangeWarned_ = false;

        std::map<std::string, Animation> animations_;

        // マテリアルごとのPBRテクスチャハンドル
        std::vector<PBRTextureHandles> materialTextureHandles_;

        // 全 Model インスタンスで共有するデフォルトマテリアル（Copy-on-Write のコピー元）
        std::vector<std::unique_ptr<MaterialInstance>> defaultMaterials_;
    };
}
