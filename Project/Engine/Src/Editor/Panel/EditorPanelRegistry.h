#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace CoreEngine::Editor
{
    /// @brief パネルをどこへ出すか
    enum class PanelPlacement
    {
        SettingsSection,  ///< Engine Settings ウィンドウの 1 セクション
        Window,           ///< 単独ウィンドウ（Window / Debug メニューから開閉）
        InspectorTab,     ///< Inspector のタブ（× で閉じられる）
        EnvironmentTree,  ///< Hierarchy の Environment ツリー → 選ぶと Inspector に出る
        HierarchyContent, ///< Hierarchy パネルの中身（シーンのオブジェクト一覧）
        InspectorObject,  ///< Inspector の Object タブの中身
    };

    /// @brief メニュー上での分類（＝どこから開くか）
    /// @details Placement（描画先）とは独立した軸。
    enum class PanelGroup
    {
        General,     ///< 常設パネル（Hierarchy / Inspector / Console など）
        Rendering,   ///< 描画結果の確認・レンダリング設定
        Analysis,    ///< 計測・統計・プロファイリング
        Editor,      ///< エディタ自身の設定・操作
        Application, ///< アプリ固有（Window > Application サブメニュー）
    };

    /// @brief Window メニューと Engine Settings で共通に使う並び順
    /// @note Application はサブメニューを分けて出すのでここには含めない。
    inline constexpr std::pair<PanelGroup, const char*> kPanelGroupOrder[] = {
        { PanelGroup::General,   "General"   },
        { PanelGroup::Rendering, "Rendering" },
        { PanelGroup::Analysis,  "Analysis"  },
        { PanelGroup::Editor,    "Editor"    },
    };

    /// @brief グループの表示名
    const char* ToDisplayName(PanelGroup group);

    /// @brief パネル 1 枚の登録内容
    struct EditorPanelDesc
    {
        /// @brief 一意の識別子。ウィンドウタイトル・タブ名・保存キーを兼ねる
        std::string id;

        PanelPlacement placement = PanelPlacement::SettingsSection;
        PanelGroup     group = PanelGroup::General;

        /// @brief 初期表示状態（保存された状態があればそちらが優先される）
        bool defaultVisible = false;

        /// @brief 登録元の識別子（破棄時に自分の登録だけを外すために使う）
        const void* owner = nullptr;

        /// @brief 単独ウィンドウの初回サイズ
        float defaultWidth = 460.0f;
        float defaultHeight = 540.0f;

        /// @brief 中身の描画
        std::function<void()> draw;

        /// @brief EnvironmentTree のみ。子ツリー行の描画（クリックされたら true）
        std::function<bool()> childTree;

        /// @brief EnvironmentTree のみ。親エントリが選ばれたときの通知
        std::function<void()> onParentSelected;
    };

    /// @brief 登録済みパネル（記述子＋表示状態）
    struct EditorPanel
    {
        EditorPanelDesc desc;
        bool visible = false;

        const std::string& Id() const noexcept { return desc.id; }
    };

    /// @brief エディタのパネル登録を 1 か所に集める
    /// @details 「どこに出るか」は記述子の placement / group で決まる。
    ///          登録側は出し先ごとに別の API を選ぶ必要がない。
    class EditorPanelRegistry
    {
    public:
        /// @brief 単独ウィンドウをドッキング先へ登録するための橋渡し
        using DockRegistrar = std::function<void(const std::string& id)>;

        static EditorPanelRegistry& Get();

        /// @brief パネルを登録する（同じ id が既にあれば中身を差し替える）
        /// @return 登録されたパネル（アドレスは以後も変わらない）
        EditorPanel& Register(EditorPanelDesc desc);

        /// @brief 登録を外す（owner が一致する場合のみ）
        /// @param owner 登録時に渡した識別子。nullptr なら owner を問わず外す
        /// @note シーン切り替えで「新シーンの登録 → 旧シーンの破棄」の順になっても
        ///       新しい登録を古いデストラクタが消してしまわないようにするための照合。
        void Unregister(const std::string& id, const void* owner);

        EditorPanel* Find(const std::string& id);

        /// @brief 出し先で絞って登録順に列挙する
        void ForEach(PanelPlacement placement, const std::function<void(EditorPanel&)>& fn);

        /// @brief 出し先で絞って条件に合うものがあるか
        bool Any(PanelPlacement placement,
                 const std::function<bool(const EditorPanel&)>& pred) const;

        /// @brief 最初に見つかった 1 枚（HierarchyContent など単数のもの用）
        EditorPanel* FindFirst(PanelPlacement placement);

        const std::vector<std::unique_ptr<EditorPanel>>& GetAll() const noexcept { return panels_; }

        /// @brief 単独ウィンドウのドック登録先を設定する
        /// @note 設定した時点で、既に登録済みの単独ウィンドウをまとめて通知する。
        void SetDockRegistrar(DockRegistrar registrar);

        /// @brief 保存されていた表示状態を流し込む
        /// @param saved id → 表示中か
        /// @note 登録済みのものへ即適用し、以後登録されるものにも効く。
        ///       パネルの登録は起動中ずっと続くので、保存値を控えておく必要がある。
        void ApplySavedVisibility(std::unordered_map<std::string, bool> saved);

        /// @brief 開閉を覚えておくパネルか（単独ウィンドウと Inspector タブ）
        static bool IsVisibilityPersisted(const EditorPanel& panel);

    private:
        EditorPanelRegistry() = default;
        ~EditorPanelRegistry() = default;
        EditorPanelRegistry(const EditorPanelRegistry&) = delete;
        EditorPanelRegistry& operator=(const EditorPanelRegistry&) = delete;

        std::vector<std::unique_ptr<EditorPanel>> panels_;
        DockRegistrar dockRegistrar_;

        /// 前回終了時の開閉状態（まだ登録されていないパネルのぶんも持っておく）
        std::unordered_map<std::string, bool> savedVisibility_;
    };
}
