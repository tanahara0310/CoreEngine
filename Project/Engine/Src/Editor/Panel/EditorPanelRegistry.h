#pragma once

#include "Editor/Panel/EditorDockArea.h"
#include "Utility/Lifetime/ScopedRegistration.h"

#include <cstdint>
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

        /// @brief 単独ウィンドウの既定のドック先
        /// @note None なら標準レイアウトへ組み込まず、フローティングで開く。
        DockArea defaultDock = DockArea::None;

        /// @brief 初期表示状態（保存された状態があればそちらが優先される）
        bool defaultVisible = false;

        /// @brief 単独ウィンドウの初回サイズ
        float defaultWidth = 460.0f;
        float defaultHeight = 540.0f;

        /// @brief 中身の描画
        std::function<void()> draw;
    };

    /// @brief 登録済みパネル（記述子＋表示状態）
    struct EditorPanel
    {
        EditorPanelDesc desc;
        bool visible = false;

        const std::string& Id() const noexcept { return desc.id; }

    private:
        friend class EditorPanelRegistry;

        /// 最後に中身を登録したときの番号
        uint64_t registration_ = 0;
    };

    /// @brief エディタのパネル登録を 1 か所に集める
    /// @details 「どこに出るか」は記述子の placement / group で決まる。
    ///          登録側は出し先ごとに別の API を選ぶ必要がない。
    class EditorPanelRegistry
    {
    public:
        /// @brief 単独ウィンドウをドッキング先へ登録するための橋渡し
        using DockRegistrar = std::function<void(const EditorPanelDesc& desc)>;

        static EditorPanelRegistry& Get();

        /// @brief パネルを登録する（同じ id が既にあれば中身を差し替える）
        /// @return 登録を握るハンドル。破棄すると外れる（後から同じ id を登録し直していれば、そちらを残す）
        ScopedRegistration Register(EditorPanelDesc desc);

        EditorPanel* Find(const std::string& id);

        /// @brief 出し先で絞って登録順に列挙する
        void ForEach(PanelPlacement placement, const std::function<void(EditorPanel&)>& fn);

        /// @brief 出し先で絞って条件に合うものがあるか
        bool Any(PanelPlacement placement,
                 const std::function<bool(const EditorPanel&)>& pred) const;

        /// @brief 最初に見つかった 1 枚（HierarchyContent など単数のもの用）
        EditorPanel* FindFirst(PanelPlacement placement);

        /// @brief 単独ウィンドウのドック登録先を設定する
        /// @note 設定した時点で、既に登録済みの単独ウィンドウをまとめて通知する。
        void SetDockRegistrar(DockRegistrar registrar);

        /// @brief 保存されていた表示状態を流し込む
        /// @param saved id → 表示中か
        /// @note 登録済みのものへ即適用し、以後登録されるものにも効く。
        void ApplySavedVisibility(std::unordered_map<std::string, bool> saved);

        /// @brief 開閉を覚えておくパネルの開閉状態を列挙する
        /// @note 登録を外したパネルは、外したときの状態を渡す。
        void ForEachPersistedVisibility(
            const std::function<void(const std::string& id, bool visible)>& fn) const;

        /// @brief 開閉を覚えておくパネルか（単独ウィンドウと Inspector タブ）
        static bool IsVisibilityPersisted(const EditorPanel& panel);

    private:
        EditorPanelRegistry() = default;
        ~EditorPanelRegistry() = default;
        EditorPanelRegistry(const EditorPanelRegistry&) = delete;
        EditorPanelRegistry& operator=(const EditorPanelRegistry&) = delete;

        /// @brief 登録を外す（後から同じ id を登録し直していれば何もしない）
        void Unregister(const std::string& id, uint64_t registration);

        std::vector<std::unique_ptr<EditorPanel>> panels_;
        DockRegistrar dockRegistrar_;

        /// 最後に振った登録の番号
        uint64_t lastRegistration_ = 0;

        /// 前回終了時の開閉状態（まだ登録されていないパネルのぶんも持っておく）
        std::unordered_map<std::string, bool> savedVisibility_;

        /// 登録を外したパネルの、外したときの開閉状態
        std::unordered_map<std::string, bool> unregisteredVisibility_;
    };
}
