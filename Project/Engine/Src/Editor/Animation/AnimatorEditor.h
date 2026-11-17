#pragma once

#ifdef CORE_EDITOR

#include "AnimatorCanvas.h"
#include "AnimatorInspectorPanel.h"
#include "AnimatorParameterPanel.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace CoreEngine
{
    class EngineSystem;
}

namespace CoreEngine::Editor
{
    struct AnimatorDocument;
    struct AnimatorLiveView;

    /// @brief アニメーター設定（.animator）の窓（開いた設定をタブで並べる）
    /// @details 左にパラメータとクリップ、中に状態の箱と遷移の矢印、右に選んだものの設定を出す。
    ///          再生中に Hierarchy で選んだオブジェクトの Animator がこの設定を使っていれば、その実行の状態を映す。
    ///          窓はタブ・開く・閉じる・Undo の履歴へ積むことを受け持ち、中身の描画は 3 つの部品へ任せる。
    class AnimatorEditor
    {
    public:
        /// @brief 窓の名前（パネルの id）
        static constexpr const char* kPanelId = "Animator";

        /// @brief Project の窓から設定をドラッグするときの名前（中身はファイル名）
        static constexpr const char* kControllerPayload = "ANIMATOR_FILE";

        static AnimatorEditor& Get();

        void Initialize(EngineSystem* engine);
        void Finalize();

        /// @brief 窓の中身を描く
        void Draw();

        /// @brief 設定を開く（開いていればそのタブへ切り替える）
        /// @return 読めたら true
        bool Open(const std::filesystem::path& file);

        /// @brief 保存していない変更のある設定があるか
        bool HasUnsavedChanges() const;

        /// @brief 開いている設定の中身を、控えた JSON の文へ戻す（Undo / Redo から呼ぶ）
        void RestoreDocument(const void* document, const std::string& text);

        /// @brief 空の設定のファイルを作る
        /// @param folder 置くフォルダ（絶対パスか `Application/…` の綴り）
        /// @param name 拡張子を除いた名前
        /// @return 作れたら true（作れなければ error に訳を書く）
        static bool CreateControllerFile(const std::filesystem::path& folder, const std::string& name,
            std::filesystem::path* created, std::string* error);

    private:
        AnimatorEditor();
        ~AnimatorEditor();
        AnimatorEditor(const AnimatorEditor&) = delete;
        AnimatorEditor& operator=(const AnimatorEditor&) = delete;

        AnimatorDocument* FindDocument(const void* document);

        void DrawDocument(AnimatorDocument& document);
        void DrawToolbar(AnimatorDocument& document, const AnimatorLiveView& live);
        void DrawCloseDialog();
        void CloseDocument(size_t index);

        /// @brief 変えたところがあれば、操作を終えたときに Undo の履歴へ積む
        void CommitIfChanged(AnimatorDocument& document);

        EngineSystem* engine_ = nullptr;
        std::vector<std::unique_ptr<AnimatorDocument>> documents_;
        size_t activeDocument_ = 0;
        std::optional<size_t> closingDocument_;
        bool focusRequested_ = false;

        AnimatorParameterPanel parameterPanel_;
        AnimatorCanvas canvas_;
        AnimatorInspectorPanel inspectorPanel_;
    };
}

#endif // CORE_EDITOR
