#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <filesystem>
#include <unordered_map>
#include <d3d12.h>
#include <wrl.h>

namespace CoreEngine
{
    class GraphicsCore;

    /// @brief Unityスタイルのプロジェクトビュー
    /// Assetsフォルダ以下の階層を表示し、ファイルとフォルダを管理する
    class ProjectView {
    public:
        /// @brief 初期化
        /// @param dxCommon DirectX共通クラスへのポインタ
        void Initialize(GraphicsCore* dxCommon);

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

        /// @brief 選択中のアセット（無ければ空）
        const std::filesystem::path& GetSelectedAsset() const { return selectedPath_; }

        /// @brief 選択中のアセットの情報を Inspector へ描く
        /// @note 種類・GUID・パスと、そのアセットを参照しているファイルの一覧を出す。
        void DrawSelectedAssetInspector();

        /// @brief アセットの種類（表示と絞り込みに使う）
        enum class Kind {
            Any,        ///< 絞り込みで「すべて」を表す（エントリには付かない）
            Folder,
            Model,
            Texture,
            Prefab,
            Script,
            Scene,
            Audio,
            Material,
            Shader,
            Data,
            Other,
        };

    private:
        /// @brief エントリ（ファイルまたはフォルダ）の情報
        struct Entry {
            std::string name;           // 名前
            std::filesystem::path path; // フルパス
            bool isDirectory;           // ディレクトリかどうか
            Kind kind = Kind::Other;    // 種類
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

        /// @brief ディレクトリの移動を頼む
        /// @param path 移動先のパス
        /// @note 実際に移るのはフレームの最後（描画の途中でキャッシュを捨てないため）。
        void NavigateToDirectory(const std::filesystem::path& path);

        /// @brief 頼まれていた移動を行う
        void ApplyPendingNavigation();

        /// @brief 親ディレクトリに戻る
        void NavigateUp();

        /// @brief グリッドレイアウトで項目を表示
        /// @param entries エントリのリスト
        void DrawGridLayout(const std::vector<Entry>& entries);

        /// @brief 一覧（名前・種類・GUID の表）で項目を表示
        void DrawListLayout(const std::vector<Entry>& entries);

        /// @brief 検索欄と種類の絞り込みを描画
        void DrawFilterBar();

        /// @brief そのアセットを参照しているファイルを探し直す
        void RebuildReferences(const std::filesystem::path& assetPath);

        /// @brief 絞り込みを通ったエントリだけを集める
        std::vector<Entry> FilterEntries(const std::vector<Entry>& entries) const;

        /// @brief 1 件分のクリック・ダブルクリック・ドラッグ開始を処理する
        /// @param index entries 内の位置
        void HandleEntryInteraction(const Entry& entry, int index);

        /// @brief アイコンを描画
        /// @param entry エントリ情報
        void DrawIcon(const Entry& entry);

        /// @brief PNGプレビューテクスチャを取得
        /// @param filePath PNGファイルのパス
        /// @return テクスチャのGPUハンドルとサイズ
        PNGPreviewInfo GetPNGPreview(const std::filesystem::path& filePath);

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
        GraphicsCore* dxCommon_ = nullptr;     // DirectX共通クラスへのポインタ

        std::filesystem::path rootPath_;        // 仮想ルート（プロジェクトルート）
        std::filesystem::path appAssetsPath_;   // Application/Assets パス
        std::filesystem::path engineAssetsPath_; // Engine/Assets パス
        std::filesystem::path currentPath_;     // 現在のパス

        bool isVisible_ = true;                 // 表示状態

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

        // 絞り込みと表示形式
        char searchFilter_[64] = {};            // 名前の絞り込み
        Kind kindFilter_ = Kind::Any;           // 種類の絞り込み
        bool useListView_ = false;              // 一覧表示にするか
        std::filesystem::path selectedPath_;    // 選択中のエントリ
        std::filesystem::path pendingNavigate_; // フレームの最後に移る先（空なら移らない）

        // 参照しているファイルの控え（選択が変わったときだけ調べ直す）
        struct Reference {
            std::filesystem::path path;         // 参照している側のファイル
            Kind kind = Kind::Other;            // その種類
        };
        std::filesystem::path referencesFor_;   // どのアセットについて調べたか
        std::vector<Reference> references_;

        // 最後に読み直したときの AssetDatabase の番号
        uint64_t seenAssetRevision_ = 0;

        // 左ツリー展開アニメーション
        std::unordered_map<std::string, float> treeExpandAnimTime_;
        std::unordered_map<std::string, bool> treeExpandAnimOpening_;
        std::unordered_map<std::string, bool> treePendingClose_;
        float treeExpandAnimDuration_ = 0.16f;

        // PNGプレビューキャッシュ
        std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D12Resource>> pngPreviewCache_;
        std::unordered_map<std::string, PNGPreviewInfo> pngPreviewInfoCache_;

        // フォルダツリーキャッシュ（毎フレームのファイルシステムスキャンを抑止）
        std::unordered_map<std::string, bool> hasSubdirCache_;
        std::unordered_map<std::string, std::vector<std::filesystem::path>> treeDirCache_;
    };
}
