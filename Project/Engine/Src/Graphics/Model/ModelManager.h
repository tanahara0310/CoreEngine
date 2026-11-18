#pragma once

#include <memory>
#include <mutex>
#include <set>
#include <condition_variable>
#include <functional>
#include <future>
#include <string>
#include <unordered_map>
#include <vector>

#include "ModelResource.h"
#include "Model.h"
#include "ModelRenderContext.h"
#include "Animation/Animation.h"

namespace CoreEngine { class ThreadPool; }
namespace CoreEngine { class IPrimitiveMeshGenerator; }
namespace CoreEngine { class InstanceBatchManager; }
namespace CoreEngine { class SkinningComputeDispatcher; }
namespace CoreEngine { class CustomShaderPipelineCache; }
namespace CoreEngine { class SurfaceMaterialPipelineCache; }

namespace CoreEngine
{
// 前方宣言
class GraphicsCore;

/// @brief モデルリソースとインスタンスを管理するマネージャークラス
/// リソースのキャッシュとインスタンスの生成を担当
class ModelManager {
public:
    ModelManager();
    ~ModelManager();

    /// @brief 初期化
    /// @param dxCommon GraphicsCoreのポインタ
    void Initialize(GraphicsCore* dxCommon);

    /// @brief 描画依存コンテキストを設定（全レンダラー登録後に一度呼び出す）
    /// @param ctx レンダラー・デバイス等の固定依存コンテキスト
    void SetRenderContext(const ModelRenderContext& ctx);

    /// @brief 描画依存コンテキストを取得する（カスタムシェーダー構築に使用）
    const ModelRenderContext& GetRenderContext() const { return renderContext_; }

    /// @brief カスタムシェーダーパイプラインの共有キャッシュを取得する
    /// @details 同一シェーダー＋同一設定のオブジェクトが複数あっても構築は1回で済む。
    CustomShaderPipelineCache* GetCustomShaderPipelineCache() { return customShaderPipelineCache_.get(); }

    /// @brief 頂点の色を持たないモデルに差す、白 1 つだけの配列の SRV
    D3D12_GPU_DESCRIPTOR_HANDLE GetWhiteVertexColorSRV() const;

    /// @brief 3D モデルのマテリアル（`名前.Surface.hlsl`）のパイプラインの共有キャッシュ（描画依存を設定するまでは nullptr）
    SurfaceMaterialPipelineCache* GetSurfaceMaterialPipelineCache() { return surfaceMaterialPipelineCache_.get(); }

    /// @brief 静的モデルを作成（アニメーションなし）
    /// @param filePath ファイルパス（Assetsフォルダを省略可能）
    /// @return 作成されたModelのユニークポインタ
    std::unique_ptr<Model> CreateStaticModel(const std::string& filePath);

    /// @brief 骨で頂点を変形して描くモデルを作成
    /// @param filePath ファイルパス（Assetsフォルダを省略可能）
    /// @param initialPose 最初に描く姿勢のクリップ（その最初のコマで描く。nullptr なら骨の初期の姿勢）
    /// @return 作成されたModelのユニークポインタ（スケルトンが無い場合は静的モデル）
    /// @note 姿勢を動かすのは呼び出し側（Animator が Model::UpdateSkinCluster へ骨の行列を渡す）。
    std::unique_ptr<Model> CreateSkinnedModel(const std::string& filePath, const Animation* initialPose);

    /// @brief クリップを読む（同じファイルと名前の組は 1 回だけ読んで共有する）
    /// @param file モデルかアニメーションのファイル（パスかファイル名）
    /// @param sourceName ファイルの中のアニメーション名（空なら先頭の 1 本。見つからなければ先頭を使って警告する）
    /// @return 読めなければ nullptr
    std::shared_ptr<const Animation> AcquireClip(const std::string& file, const std::string& sourceName);

    /// @brief ファイルの中のアニメーションの名前（ファイルの中の順。読めなければ空）
    std::vector<std::string> GetClipNames(const std::string& file);

    /// @brief 初期化されているか確認
    /// @return 初期化済みならtrue
    bool IsInitialized() const { return dxCommon_ != nullptr; }

    /// @brief ModelResourceを取得（既に読み込まれている場合のみ）
    /// @param filePath ファイルパス（Assetsフォルダを省略可能）
    /// @return ModelResourceのポインタ（見つからない場合はnullptr）
    ModelResource* GetModelResource(const std::string& filePath);

    /// @brief 複数モデルを並列プリロード（全完了まで待機）
    /// @param filePaths プリロードするファイルパスのリスト
    void PreloadModels(const std::vector<std::string>& filePaths);

    /// @brief 複数モデルの並列プリロードを開始する（完了を待たずに即座に戻る）
    /// @details 後から CreateStaticModel が同じパスを要求するとロード権待ちで自動的に合流するので、
    ///          呼び出し側が完了を管理する必要はない。
    /// @note 先読みの失敗は握り潰してログに残すだけ（本番の CreateStaticModel が読み直す）。
    void BeginPreload(const std::vector<std::string>& filePaths);

    /// @brief 進行中のプリロードの完了本数と全体本数を返す（待たない）
    /// @return {完了した本数, 積まれている本数}
    std::pair<size_t, size_t> GetPreloadProgress();

    /// @brief 進行中のプリロードが全て終わるまで待つ
    /// @details 終了処理で TextureManager や GraphicsCore を壊す前に必ず呼ぶこと。
    void WaitForPreload();

    /// @brief プリミティブメッシュジェネレーターからモデルを作成する
    /// @param key キャッシュキー（例: "Primitive::Plane_10x10"）
    /// @param generator メッシュ生成インターフェース
    /// @return 作成されたModelのユニークポインタ
    std::unique_ptr<Model> CreatePrimitiveModel(const std::string& key, const IPrimitiveMeshGenerator& generator);

    /// @brief スレッドプールを取得（デバッグ用）
    ThreadPool* GetThreadPool() const { return threadPool_.get(); }

    /// @brief モデルの読み込みのワーカーへ仕事を投げる（プールがまだ無ければ作る）
    /// @param label 計測の表示に出す名前
    /// @param work ワーカーで走らせる仕事
    /// @return 仕事の終わりを待つ future
    /// @note 読み込んだモデルから作る重い派生データ（当たりのメッシュなど）に使う。メインスレッドから呼ぶ。
    std::future<void> SubmitWork(const char* label, std::function<void()> work);

    /// @brief 全キャッシュ済みリソースに対してコールバックを実行する
    /// @param callback 各 ModelResource* に対して呼ばれるコールバック
    void ForEachResource(const std::function<void(ModelResource*)>& callback);

    /// @brief リソースキャッシュ統計（loadedModelCount / loadingResourceCount）を更新する
    void UpdateResourceCacheStats();

private:
    // GraphicsCore
    GraphicsCore* dxCommon_ = nullptr;

    // 頂点の色を持たないモデルに差す、白 1 つだけの配列（StructuredBuffer<uint>）
    Microsoft::WRL::ComPtr<ID3D12Resource> whiteVertexColorBuffer_;
    UniqueDescriptor whiteVertexColorSRV_;

    // Model インスタンス生成時に注入する描画依存コンテキスト
    ModelRenderContext renderContext_;

    // インスタンシングバッチマネージャー（通常モデル用）
    std::unique_ptr<InstanceBatchManager> instanceBatchManager_;

    // GPUスキニング(CS)ディスパッチャー（スキニングモデル用）
    std::unique_ptr<SkinningComputeDispatcher> skinningDispatcher_;

    // カスタムシェーダーパイプラインの共有キャッシュ（シェーダーパス＋設定がキー）
    std::unique_ptr<CustomShaderPipelineCache> customShaderPipelineCache_;

    // 3D モデルのマテリアルのパイプラインの共有キャッシュ（シェーダーのパスがキー）
    std::unique_ptr<SurfaceMaterialPipelineCache> surfaceMaterialPipelineCache_;

    // デフォルトのベースパス
    const std::string basePath_ = "Application/Assets/";
    
    // ファイルパスをキーとしたリソースキャッシュ
    std::unordered_map<std::string, std::unique_ptr<ModelResource>> resourceCache_;

    // 読んだクリップ（解決したパスとファイルの中の名前の組がキー）
    std::unordered_map<std::string, std::shared_ptr<const Animation>> clipCache_;
    std::mutex clipMutex_;

    // キャッシュのスレッドセーフなアクセス用
    mutable std::mutex cacheMutex_;
    std::set<std::string>       loadingPaths_;  // 現在ロード中のパスセット
    std::condition_variable     cacheCondVar_;  // ロード完了通知用

    // 並列プリロード用スレッドプール
    std::unique_ptr<ThreadPool> threadPool_;

    // 進行中のプリロード（BeginPreload で積み、WaitForPreload で回収する）
    std::vector<std::future<void>> preloadFutures_;
    std::mutex preloadMutex_;

    /// @brief スレッドプールの遅延初期化
    void EnsureThreadPool();

    /// @brief フルパスを解決（Assetsフォルダを自動的に追加）
    /// @param filePath 入力パス（UTF-8）
    /// @return 解決されたフルパス（UTF-8）
    /// @details ここから下流のモデル読み込みが扱う std::string は一貫して UTF-8。
    ///          Assimp が UTF-8 のパスを要求するため、この鎖を ANSI で汚さないこと。
    std::string ResolveFilePath(const std::string& filePath) const;

    /// @brief ファイルの中のアニメーションを読む（読み込み済みのモデルならそのリソースから写す）
    /// @return 読めたら true
    bool ReadFileAnimations(const std::string& resolvedPath, std::vector<std::pair<std::string, Animation>>& out);

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
