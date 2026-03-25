#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <unordered_map>
#include <d3d12.h>
#include <wrl.h>

namespace CoreEngine
{
class DirectXCommon;

/// @brief Unityスタイルのプロジェクトビュー
/// Assetsフォルダ以下の階層を表示し、ファイルとフォルダを管理する
class ProjectView {
public:
    /// @brief 初期化
    /// @param dxCommon DirectX共通クラスへのポインタ
    void Initialize(DirectXCommon* dxCommon);

    /// @brief 更新（ImGuiウィンドウの描画）
    void Update();

    /// @brief 終了処理
    void Finalize();

    /// @brief プロジェクトビューの表示状態を設定
    /// @param visible 表示するかどうか
    void SetVisible(bool visible) { isVisible_ = visible; }

    /// @brief プロジェクトビューの表示状態を取得
    /// @return 表示状態
    bool IsVisible() const { return isVisible_; }

private:
/// @brief エントリ（ファイルまたはフォルダ）の情報
struct Entry {
    std::string name;           // 名前
    std::filesystem::path path; // フルパス
    bool isDirectory;           // ディレクトリかどうか
};
    
/// @brief PNGプレビューの情報
struct PNGPreviewInfo {
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = {};
    float width = 0.0f;
    float height = 0.0f;
};

    /// @brief 現在のディレクトリの内容を取得
    /// @return エントリのリスト
    std::vector<Entry> GetCurrentDirectoryContents();

    /// @brief ディレクトリに移動
    /// @param path 移動先のパス
    void NavigateToDirectory(const std::filesystem::path& path);

    /// @brief 親ディレクトリに戻る
    void NavigateUp();

    /// @brief グリッドレイアウトで項目を表示
    /// @param entries エントリのリスト
    void DrawGridLayout(const std::vector<Entry>& entries);

    /// @brief アイコンを描画
    /// @param entry エントリ情報
    void DrawIcon(const Entry& entry);
    
    /// @brief PNGプレビューテクスチャを取得
    /// @param filePath PNGファイルのパス
    /// @return テクスチャのGPUハンドルとサイズ
    PNGPreviewInfo GetPNGPreview(const std::filesystem::path& filePath);

    /// @brief デフォルトアイコンテクスチャを読み込み
    void LoadDefaultIcon();

    /// @brief フォルダツリーを再帰的に描画
    /// @param path 描画するフォルダのパス
    /// @param depth ツリー深さ
    void DrawFolderTree(const std::filesystem::path& path, int depth = 0);

    /// @brief パンくずリスト（ブレッドクラム）を描画
    void DrawBreadcrumb();

    /// @brief 相対パスを取得
    /// @param fullPath フルパス
    /// @return ルートからの相対パス
    std::string GetRelativePath(const std::filesystem::path& fullPath);

    /// @brief ファイルを関連付けアプリで開く
    /// @param filePath 対象ファイルパス
    void OpenFile(const std::filesystem::path& filePath);

private:
    DirectXCommon* dxCommon_ = nullptr;     // DirectX共通クラスへのポインタ

    std::filesystem::path rootPath_;        // 仮想ルート（プロジェクトルート）
    std::filesystem::path appAssetsPath_;   // Application/Assets パス
    std::filesystem::path engineAssetsPath_; // Engine/Assets パス
    std::filesystem::path currentPath_;     // 現在のパス
    
    bool isVisible_ = true;                 // 表示状態
    
    // アイコンテクスチャ
    Microsoft::WRL::ComPtr<ID3D12Resource> directoryIconTexture_;
    D3D12_GPU_DESCRIPTOR_HANDLE directoryIconGpuHandle_ = {};
    bool directoryIconLoaded_ = false;
    
    Microsoft::WRL::ComPtr<ID3D12Resource> pngIconTexture_;
    D3D12_GPU_DESCRIPTOR_HANDLE pngIconGpuHandle_ = {};
    bool pngIconLoaded_ = false;
    
    Microsoft::WRL::ComPtr<ID3D12Resource> fileIconTexture_;
    D3D12_GPU_DESCRIPTOR_HANDLE fileIconGpuHandle_ = {};
    bool fileIconLoaded_ = false;
    
    // UI設定
    float iconSize_ = 64.0f;                // アイコンのサイズ
    float padding_ = 8.0f;                  // アイコン間のパディング
    float textHeight_ = 40.0f;              // テキスト表示用の高さ
    float treeViewWidth_ = 200.0f;          // 左側のツリービューの幅
    
    // ダブルクリック検出用
    int selectedIndex_ = -1;                // 選択中のインデックス
    double lastClickTime_ = 0.0;            // 最後のクリック時刻
    int lastClickedIndex_ = -1;             // 最後にクリックされたインデックス

    // 右ペイン表示データ
    std::vector<Entry> currentEntries_;

    // 左ツリー展開アニメーション
    std::unordered_map<std::string, float> treeExpandAnimTime_;
    std::unordered_map<std::string, bool> treeExpandAnimOpening_;
    std::unordered_map<std::string, bool> treePendingClose_;
    std::unordered_map<std::string, bool> treeCloseCommit_;
    float treeExpandAnimDuration_ = 0.16f;
    
    // PNGプレビューキャッシュ
    std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D12Resource>> pngPreviewCache_;
    std::unordered_map<std::string, PNGPreviewInfo> pngPreviewInfoCache_;
};
}
