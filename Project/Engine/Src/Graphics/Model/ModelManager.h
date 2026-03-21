#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "ModelResource.h"
#include "Model.h"
#include "Animation/Animation.h"

namespace CoreEngine
{
// 前方宣言
class DirectXCommon;
class ResourceFactory;

/// @brief アニメーション読み込み情報
/// @note パスはファイル名のみ指定。ディレクトリは AssetDatabase が自動解決する。
struct AnimationLoadInfo {
    std::string modelFile;          ///< モデルファイル名（例: "walk.gltf"）
    std::string animationName;      ///< 識別用のアニメーション名（例: "walkAnimation"）
    std::string animationFile = ""; ///< アニメーションファイル名（空 = modelFile と同じ）
};

/// @brief モデルリソースとインスタンスを管理するマネージャークラス
/// リソースのキャッシュとインスタンスの生成を担当
class ModelManager {
public:
    /// @brief 初期化
    /// @param dxCommon DirectXCommonのポインタ
    /// @param factory リソースファクトリのポインタ
    void Initialize(DirectXCommon* dxCommon, ResourceFactory* factory);

    /// @brief 静的モデルを作成（アニメーションなし）
    /// @param filePath ファイルパス（Assetsフォルダを省略可能）
    /// @return 作成されたModelのユニークポインタ
    std::unique_ptr<Model> CreateStaticModel(const std::string& filePath);

    /// @brief キーフレームアニメーションモデルを作成
    /// @param filePath ファイルパス（Assetsフォルダを省略可能）
    /// @param animationName アニメーション名（空の場合は最初のアニメーション）
    /// @param loop ループ再生するか
    /// @return 作成されたModelのユニークポインタ
    std::unique_ptr<Model> CreateKeyframeModel(
        const std::string& filePath,
        const std::string& animationName = "",
        bool loop = true
    );

    /// @brief スケルトンアニメーションモデルを作成
    /// @param filePath ファイルパス（Assetsフォルダを省略可能）
    /// @param animationName アニメーション名（空の場合は最初のアニメーション）
    /// @param loop ループ再生するか
    /// @return 作成されたModelのユニークポインタ
    std::unique_ptr<Model> CreateSkeletonModel(
        const std::string& filePath,
        const std::string& animationName = "",
        bool loop = true
    );

    /// @brief アニメーションをモデルリソースに追加
    /// @param loadInfo アニメーション読み込み情報
    /// @return 成功したらtrue
    bool LoadAnimation(const AnimationLoadInfo& loadInfo);

    /// @brief 全てのキャッシュをクリア
    void ClearCache();

    /// @brief 初期化されているか確認
    /// @return 初期化済みならtrue
    bool IsInitialized() const { return dxCommon_ != nullptr; }

    /// @brief ModelResourceを取得（既に読み込まれている場合のみ）
    /// @param filePath ファイルパス（Assetsフォルダを省略可能）
    /// @return ModelResourceのポインタ（見つからない場合はnullptr）
    ModelResource* GetModelResource(const std::string& filePath);

    /// @brief モデルリソースを事前読み込み（シーン読み込み最適化用）
    /// @param filePath ファイル名またはフルパス（AssetDatabase が解決）
    void LoadModelResource(const std::string& filePath);

private:
    // DirectXCommon
    DirectXCommon* dxCommon_ = nullptr;
    
    // リソースファクトリ
    ResourceFactory* resourceFactory_ = nullptr;
    
    // デフォルトのベースパス
    const std::string basePath_ = "Application/Assets/";
    
    // ファイルパスをキーとしたリソースキャッシュ
    std::unordered_map<std::string, std::unique_ptr<ModelResource>> resourceCache_;

    /// @brief フルパスを解決（Assetsフォルダを自動的に追加）
    /// @param filePath 入力パス
    /// @return 解決されたフルパス
    std::string ResolveFilePath(const std::string& filePath) const;

    /// @brief モデルリソースを読み込む（内部使用・キャッシュあり）
    /// @param directoryPath ディレクトリパス
    /// @param filename ファイル名
    /// @return ModelResourceのポインタ
    ModelResource* LoadModelResourceInternal(const std::string& directoryPath, const std::string& filename);

    /// @brief ファイルパスを正規化（キャッシュキー用）
    /// @param directoryPath ディレクトリパス
    /// @param filename ファイル名
    /// @return 正規化されたパス
    std::string MakeNormalizedPath(const std::string& directoryPath, const std::string& filename) const;

    /// @brief フルパスをディレクトリとファイル名に分離
    /// @param filePath フルパス
    /// @param outDirectory 出力：ディレクトリパス
    /// @param outFilename 出力：ファイル名
    void SplitPath(const std::string& filePath, std::string& outDirectory, std::string& outFilename) const;
};
}
