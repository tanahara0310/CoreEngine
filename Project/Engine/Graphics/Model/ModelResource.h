#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <string>
#include <map>
#include <optional>

#include "Engine/Graphics/Model/MaterialAsset.h"
#include "Engine/Graphics/Model/ModelData.h"
#include "Engine/Graphics/Model/Node.h"
#include "Animation/Animation.h"
#include "Skeleton/Skeleton.h"

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
        D3D12_GPU_DESCRIPTOR_HANDLE baseColor;
        D3D12_GPU_DESCRIPTOR_HANDLE metallicRoughness;
        D3D12_GPU_DESCRIPTOR_HANDLE normal;
        D3D12_GPU_DESCRIPTOR_HANDLE occlusion;
        D3D12_GPU_DESCRIPTOR_HANDLE emissive;
    };
    const PBRTextureHandles& GetMaterialTextures(uint32_t materialIndex) const;

private:
    friend class Model;
    friend class ModelParticleRenderer;

    Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer_;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
    UINT vertexCount_ = 0;
    
    Microsoft::WRL::ComPtr<ID3D12Resource> indexBuffer_;
    D3D12_INDEX_BUFFER_VIEW indexBufferView_{};
    UINT indexCount_ = 0;
    
    ModelData modelData_;
    Node rootNode_;
    std::optional<Skeleton> skeleton_;

    DirectXCommon* dxCommon_ = nullptr;
    ResourceFactory* resourceFactory_ = nullptr;
    TextureManager* textureManager_ = nullptr;

    std::string filePath_;
    bool isLoaded_ = false;

    std::map<std::string, Animation> animations_;
    
    // マテリアルごとのPBRテクスチャハンドル
    std::vector<PBRTextureHandles> materialTextureHandles_;
};
}
