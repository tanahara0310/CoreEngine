#pragma once

#ifdef CORE_EDITOR

#include "Animation/AnimatorController.h"

#include <imgui.h>

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace CoreEngine
{
    class ModelManager;
}

namespace CoreEngine::Editor
{
    /// @brief アニメーターの窓で選んでいるものの種類
    enum class AnimatorSelection
    {
        None,
        State,
        AnyState,
        Transition,
    };

    /// @brief キャンバスの上の何か
    struct AnimatorHit
    {
        AnimatorSelection kind = AnimatorSelection::None;
        int index = -1;
    };

    /// @brief 層の中で名前の合う状態の番号（無ければ -1）
    int AnimatorStateIndexOf(const AnimatorLayer& layer, const std::string& name);

    /// @brief パスのファイル名の部分（区切りは `/` か `\`）
    std::string AnimatorFileNameOf(const std::string& assetPath);

    /// @brief アニメーターの窓で開いている設定 1 つ（中身・キャンバスの見え方・選んでいるもの・編集の操作）
    /// @details 編集の操作は data を書き換え、MarkChanged で操作の名前を残す。Undo の履歴へ積むのは窓が行う。
    ///          窓は 1 枚目の層だけを扱うので、層は必ず 1 枚以上ある。
    struct AnimatorDocument
    {
        /// @brief ファイルから読む（読めなければ nullptr。読めたが直したところは notice に並べる）
        static std::unique_ptr<AnimatorDocument> Load(const std::filesystem::path& sourceFile, ModelManager* modelManager);

        /// @brief 設定を保存の形の文にする
        static std::string ToText(const AnimatorControllerData& controller);

        /// @brief 一時ファイルへ書いてから差し替える
        /// @return 書けたら true
        static bool WriteText(const std::filesystem::path& target, const std::string& text);

        /// @brief `Application/Assets/…` の綴り（区切りは `/`）
        static std::string ToAssetPath(const std::filesystem::path& fullPath);

        /// @brief 保存していない変更があるか
        bool IsDirty() const { return currentText != savedText; }

        /// @brief 控えた JSON の文へ戻す（Undo / Redo）
        /// @return 読めたら true
        bool Restore(const std::string& text);

        /// @brief ファイルへ書き、同じ設定で動いているオブジェクトにも効かせる
        void Save();

        /// @brief 変えたことを残す（次に履歴へ積むときの操作の名前。最初の 1 つを使う）
        void MarkChanged(const char* label);

        // ===== 編集 =====

        void Select(AnimatorSelection kind, int index);

        /// @return 作った状態の番号
        int AddState(const Vector2& position, const std::string& clip);

        /// @param from 元の状態の番号（-1 なら「どこからでも」）
        void AddTransition(int from, int to);

        void DeleteSelection();
        void DeleteState(int stateIndex);
        void DeleteTransition(int transitionIndex);
        void SetDefaultState(int stateIndex);

        /// @brief 状態の名前を変え、遷移と最初の状態の指す名前も直す
        void RenameState(int stateIndex, const std::string& newName);

        void AddParameter(AnimatorParameterType type);

        /// @brief パラメータを消す（遷移の条件は残す）
        void DeleteParameter(int parameterIndex);

        /// @brief パラメータの名前を変え、条件の指す名前も直す
        void RenameParameter(int parameterIndex, const std::string& newName);

        /// @brief 見本のモデルを指す空のクリップを足す
        void AddEmptyClip();

        /// @brief クリップを消し、それを流していた状態のクリップを空にする
        void DeleteClip(int clipIndex);

        /// @brief クリップの名前を変え、状態の指す名前も直す
        void RenameClip(int clipIndex, const std::string& newName);

        /// @brief モデルの中のアニメーションをクリップの表へ足す
        /// @param position 状態も作るときの位置（nullptr ならクリップだけ足す）
        void AddClipsFromModel(const std::string& modelAssetPath, const Vector2* position);

        /// @brief 同じ元から出る遷移の中で 1 つ前か後ろへ動かす
        void MoveTransition(int transitionIndex, int direction);

        /// @brief ファイルの中のアニメーションの名前（ファイルごとに 1 回だけ調べる）
        const std::vector<std::string>& ClipNamesOf(const std::string& clipFile);

        std::filesystem::path file;
        std::string assetPath;  ///< `Application/Assets/…`
        std::string title;
        AnimatorControllerData data;
        ModelManager* models = nullptr;  ///< ファイルの中のアニメーションの名前を調べる先

        std::string currentText;   ///< 最後に履歴へ積んだか保存したときの中身
        std::string savedText;     ///< 最後に保存（読み込み）したときの中身
        std::string committedText; ///< 最後に Undo の履歴へ積んだときの中身
        std::string pendingLabel;  ///< 次に履歴へ積む操作の名前（空なら変えていない）
        std::string notice;        ///< 窓の下に出す知らせ
        bool selectOnNextFrame = false;

        // キャンバス
        bool viewInitialized = false;
        ImVec2 pan{ 0.0f, 0.0f };  ///< キャンバスの左上からワールドの原点までの画面の距離
        float zoom = 1.0f;
        bool panning = false;
        AnimatorSelection selection = AnimatorSelection::None;
        int selected = -1;
        std::optional<int> linkFrom;  ///< 遷移を引いている元（-1 なら「どこからでも」）
        AnimatorHit dragging;
        ImVec2 dragOffset{ 0.0f, 0.0f };
        AnimatorHit contextHit;
        ImVec2 contextWorld{ 0.0f, 0.0f };
        bool frameRequested = true;

        // 名前の欄（書き終えたときに、指している所を一緒に直す）
        ImGuiID nameEditId = 0;     ///< 書いている途中の欄（0 なら無し）
        std::string nameEditText;   ///< 書いている途中の中身
        std::string nameScratch;    ///< 書いていない欄に出す中身

        // ファイルの中のアニメーションの名前（ファイルごと）
        std::map<std::string, std::vector<std::string>> clipNames;

    private:
        std::string UniqueStateName(const std::string& base) const;
        std::string UniqueClipId(const std::string& base) const;
        std::string UniqueParameterName(const std::string& base) const;
    };
}

#endif // CORE_EDITOR
