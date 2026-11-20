#pragma once

#ifdef CORE_EDITOR

#include "Editor/Scene/SceneNameList.h"

#include <string>
#include <vector>

struct ImGuiInputTextCallbackData;

namespace CoreEngine::Editor
{
    /// @brief File > シーンを開く の中身（絞り込みの欄・最近開いたシーン・群に分けた一覧）
    class SceneOpenMenu
    {
    public:
        /// @brief `BeginMenu("シーンを開く")` が開いている間に呼ぶ
        /// @param sceneNames 開けるシーンの名前
        /// @param currentScene 今開いているシーンの名前
        /// @return 選んだシーンの名前（選ばなければ空）
        std::string Draw(const std::vector<std::string>& sceneNames, const std::string& currentScene);

    private:
        /// @brief シーンの名前の行（今のシーンに印を付ける）
        static bool DrawSceneItem(const std::string& sceneName, const char* label, const std::string& currentScene);

        char filter_[128] = {};
        std::vector<std::string> recent_;           ///< 最近開いたシーン（今のシーンと無いシーンを除く）
        std::vector<SceneNameList::Entry> entries_; ///< 群に分けた一覧
    };

    /// @brief 名前を打ってシーンを開く欄（Ctrl+O）
    class SceneQuickOpen
    {
    public:
        /// @brief 欄を開く（次の Draw で画面の上に出す）
        /// @param sceneNames 開けるシーンの名前
        /// @param currentScene 今開いているシーンの名前
        void Open(const std::vector<std::string>& sceneNames, const std::string& currentScene);

        /// @brief 毎フレーム、どのウィンドウにも入っていない所で呼ぶ（開いている間だけ描く）
        /// @return Enter かクリックで選んだシーンの名前（選ばなければ空）
        std::string Draw();

    private:
        /// @brief 一覧の 1 行
        struct Row
        {
            std::size_t scene = 0;      ///< scenes_ の番号
            SceneNameList::Match match; ///< 絞り込みに当たった字
            bool recent = false;        ///< 最近開いたシーンの段の行か
        };

        /// @brief シーン 1 つ分の控え
        struct Scene
        {
            std::string name;
            std::string group; ///< 群の名前（群に入らなければ空）
            std::string date;  ///< 最後に保存した日（保存データが無ければ空）
        };

        /// @brief 絞り込みの字から一覧の行を作り直す
        void Rebuild();

        /// @brief 1 行を描く
        /// @param right 行の右端（画面の座標）
        /// @param dateWidth 日付の列の幅
        /// @return クリックされたら true
        bool DrawRow(std::size_t index, float right, float dateWidth);

        /// @brief 上下の矢印で選ぶ行を動かす
        static int OnInputEvent(ImGuiInputTextCallbackData* data);

        bool openRequested_ = false;
        bool scrollToSelected_ = false;
        char query_[128] = {};
        std::string builtQuery_;
        std::vector<Scene> scenes_;
        std::vector<std::size_t> recent_; ///< 最近開いたシーン（scenes_ の番号・新しい順）
        std::string currentScene_;
        std::vector<Row> rows_;
        int selected_ = 0;
    };
}

#endif // CORE_EDITOR
