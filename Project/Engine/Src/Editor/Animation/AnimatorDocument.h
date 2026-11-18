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
        Layer,  ///< レイヤー（selected はレイヤーの番号）
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
    ///          キャンバスと状態・遷移の操作は activeLayer の層を扱う。層は必ず 1 枚以上ある。
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

        /// @brief 窓が今扱っている層（キャンバス・状態・遷移）
        AnimatorLayer& Layer();
        const AnimatorLayer& Layer() const;

        /// @brief 扱う層を変える（選んでいる状態と遷移は外す）
        void SetActiveLayer(int layerIndex);

        /// @brief 空の層を足して、それを扱う
        void AddLayer();

        /// @brief 層を消す（最初の層は消さない）
        void DeleteLayer(int layerIndex);

        /// @brief 層の名前を変える
        void RenameLayer(int layerIndex, const std::string& newName);

        /// @brief 2 枚目からの層の中で 1 つ前か後ろへ動かす
        void MoveLayer(int layerIndex, int direction);

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

        /// @brief クリップの尺（秒。読めなければ 0。ファイルと中の名前の組ごとに 1 回だけ読む）
        float ClipDurationOf(const AnimatorClip& clip);

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

        int activeLayer = 0;  ///< 窓が扱っている層の番号
        std::string skeletonTried;  ///< 骨の木を出すために読み込んだ見本のモデル

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
        std::map<std::string, float> clipDurations;  ///< ファイルと中の名前の組ごとの尺

        // イベントの帯でドラッグしている印
        int eventDragClip = -1;
        int eventDragIndex = -1;

        // ブレンドツリーの子のうち、クリップの設定を出している子
        int blendChildFocus = 0;

        // シーンで見る（停止中に、Hierarchy で選んだオブジェクトへ状態の姿勢を出す）
        bool previewEnabled = false;
        bool previewPlaying = true;  ///< 進みを時間で進める
        float previewTime = 0.0f;    ///< 状態の進み（0〜1）
        std::map<std::string, float> previewFloats;  ///< ブレンドツリーを混ぜる値（無い名前は初めの値）
        std::string previewObject;   ///< 姿勢を出しているオブジェクトの名前（出していなければ空）

        /// @brief シーンで見るときの数のパラメータの値（決めていなければ初めの値）
        float PreviewFloat(const std::string& parameterName) const;

    private:
        std::string UniqueStateName(const std::string& base) const;
        std::string UniqueClipId(const std::string& base) const;
        std::string UniqueParameterName(const std::string& base) const;
        std::string UniqueLayerName(const std::string& base) const;
    };
}

#endif // CORE_EDITOR
