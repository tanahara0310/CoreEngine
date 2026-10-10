#pragma once

#ifdef CORE_EDITOR

#include "Utility/Lifetime/ScopedRegistration.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class asIScriptFunction;
class asIScriptModule;
class asIScriptObject;
class asITypeInfo;
class CScriptBuilder;

namespace CoreEngine
{
    class ScriptHost;
}

namespace CoreEngine::Editor
{
    /// @brief スクリプトで書いたエディタの拡張（EditorWindow を継いだクラスと [MenuItem] の関数）をエディタへ出す
    /// @details ウィンドウはメニューから開閉するパネルにし、スクリプトを読み直しても値と開閉を持ち越す。
    ///          公開メンバ変数の値は、エディタを閉じるときに保存して次の起動で戻す。
    class ScriptEditorExtensions
    {
    public:
        explicit ScriptEditorExtensions(ScriptHost& host);
        ~ScriptEditorExtensions();

        ScriptEditorExtensions(const ScriptEditorExtensions&) = delete;
        ScriptEditorExtensions& operator=(const ScriptEditorExtensions&) = delete;

        /// @brief 組み上がったモジュールからウィンドウのクラスとメニューの関数を集める（差し替える前に呼ぶ）
        /// @param sources ファイルごとの中身（Editor フォルダの外でエディタの機能を使っていないかを調べる）
        void OnModuleCompiled(asIScriptModule& module, CScriptBuilder& builder,
                              const std::unordered_map<std::string, std::string>& sources);

        /// @brief 今のモジュールを捨てる前に、ウィンドウの値を控えてスクリプトのオブジェクトを手放す
        void OnModuleDiscarding();

        /// @brief 新しいモジュールへ差し替えた後に、ウィンドウを作り直して値を戻し、メニューとパネルを登録し直す
        void OnModuleSwapped();

        /// @brief 閉じたウィンドウの OnDisable を呼ぶ（フレームの最後に呼ぶ）
        void EndFrame();

        /// @brief ウィンドウとメニューをすべて外す
        void Shutdown();

    private:
        /// @brief EditorWindow を継いだクラス 1 つ
        struct WindowClass
        {
            asITypeInfo* type = nullptr;
            std::string className;
            std::string menuPath;
            std::string section;
        };

        /// @brief [MenuItem] を付けた関数 1 つ
        struct MenuFunction
        {
            asIScriptFunction* function = nullptr;
            std::string name;
            std::string menuPath;
        };

        /// @brief 読み直しの間、控えておくメンバ変数の値 1 つ
        struct SavedProperty;

        /// @brief 開いているウィンドウ 1 つ
        struct Window;

        /// @brief エディタを閉じても残すウィンドウの値（ファイルの中身）
        struct Persisted;

        /// @brief パネルの中身を描く
        void DrawWindow(const std::string& className);

        /// @brief メソッドを呼ぶ（止まったら false）
        bool CallWindowMethod(Window& window, asIScriptFunction* method, const char* methodName);

        /// @brief 前の起動で保存した公開メンバ変数の値を、作ったばかりのウィンドウへ戻す
        void ApplyPersisted(Window& window);

        /// @brief ウィンドウの公開メンバ変数の値をファイルへ保存する
        void SavePersisted();

        /// @brief Editor フォルダの外でエディタの機能を使っているファイルをエラーとしてログへ出す
        void ReportEditorApiOutsideEditorFolder(const asIScriptModule& module,
                                                const std::unordered_map<std::string, std::string>& sources) const;

        ScriptHost& host_;

        /// 組み上がったばかりのモジュールから集めたもの（差し替えた後に使う）
        std::vector<WindowClass> pendingWindows_;
        std::vector<MenuFunction> pendingMenus_;

        std::vector<std::unique_ptr<Window>> windows_;
        std::vector<ScopedRegistration> menuRegistrations_;

        /// 読み直しをまたいで持ち越す値（クラス名ごと）
        std::unordered_map<std::string, std::vector<SavedProperty>> savedProperties_;

        std::unique_ptr<Persisted> persisted_;
    };
}

#endif // CORE_EDITOR
