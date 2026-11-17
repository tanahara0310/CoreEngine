#pragma once

#include "Math/Vector/Vector2.h"
#include "Utility/JsonManager/JsonManager.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace CoreEngine
{
    /// @brief パラメータの種類
    enum class AnimatorParameterType
    {
        Float,
        Int,
        Bool,
        Trigger,
    };

    /// @brief 遷移の条件の比べ方
    enum class AnimatorConditionOp
    {
        Greater,   ///< 数・整数が値より大きい（`>`）
        Less,      ///< 数・整数が値より小さい（`<`）
        Equal,     ///< 整数が値と等しい（`==`）
        NotEqual,  ///< 整数が値と違う（`!=`）
        True,      ///< 真偽が true
        False,     ///< 真偽が false
        Trigger,   ///< トリガーが立っている（移ったら下ろす）
    };

    /// @brief パラメータ 1 つ
    struct AnimatorParameter
    {
        std::string name;
        AnimatorParameterType type = AnimatorParameterType::Float;
        float defaultFloat = 0.0f;
        int defaultInt = 0;
        bool defaultBool = false;
    };

    /// @brief クリップの決まった時刻に出す印
    struct AnimatorClipEvent
    {
        float time = 0.0f;  ///< 秒
        std::string name;
    };

    /// @brief 設定で使うクリップ 1 本
    struct AnimatorClip
    {
        std::string id;      ///< 状態とイベントが指す名前
        std::string file;    ///< モデルかアニメーションのファイル（`Application/Assets/…`）
        std::string source;  ///< ファイルの中のアニメーション名（空なら先頭の 1 本）
        std::vector<AnimatorClipEvent> events;
    };

    /// @brief 状態 1 つ
    struct AnimatorState
    {
        std::string name;
        std::string clip;  ///< 流すクリップの id（空なら骨の初期の姿勢）
        float speed = 1.0f;
        bool loop = true;
        Vector2 position{};  ///< 窓のキャンバスの位置
    };

    /// @brief 遷移の条件 1 つ
    struct AnimatorCondition
    {
        std::string parameter;
        AnimatorConditionOp op = AnimatorConditionOp::Greater;
        float value = 0.0f;
    };

    /// @brief 遷移 1 本
    struct AnimatorTransition
    {
        std::string from;  ///< 元の状態（`kAnyState` ならどこからでも）
        std::string to;
        std::vector<AnimatorCondition> conditions;  ///< 全部そろったときだけ移る
        float duration = 0.25f;  ///< 混ぜる秒数
        bool hasExitTime = false;
        float exitTime = 0.9f;   ///< 元の状態の進みの割合（hasExitTime のときだけ見る）
        float offset = 0.0f;     ///< 移った先のクリップを始める割合
    };

    /// @brief 状態の切り替え表 1 枚
    struct AnimatorLayer
    {
        std::string name = "Base";
        std::string defaultState;
        Vector2 anyStatePosition{ -200.0f, 0.0f };
        std::vector<AnimatorState> states;
        std::vector<AnimatorTransition> transitions;

        /// @brief 名前で状態を引く（無ければ nullptr）
        const AnimatorState* FindState(std::string_view stateName) const;
        AnimatorState* FindState(std::string_view stateName);
    };

    /// @brief アニメーター設定（`.animator`）の中身
    struct AnimatorControllerData
    {
        std::string previewModel;  ///< 窓が骨の名前とクリップの候補に使うモデル
        std::vector<AnimatorParameter> parameters;
        std::vector<AnimatorClip> clips;
        std::vector<AnimatorLayer> layers;

        const AnimatorParameter* FindParameter(std::string_view parameterName) const;
        const AnimatorClip* FindClip(std::string_view clipId) const;
    };

    /// @brief `.animator` の読み書き
    namespace AnimatorControllerFormat
    {
        /// @brief 「どこからでも」を表す遷移の元
        inline constexpr const char* kAnyState = "*";

        /// @brief 保存の形の版
        inline constexpr uint32_t kVersion = 1;

        /// @brief 拡張子
        inline constexpr const char* kExtension = ".animator";

        /// @brief JSON から読む（知らないキーは無視する）
        /// @param warnings 読めなかった項目の説明を足す先（nullptr なら捨てる）
        /// @return 形が読めたら true
        bool Read(const json& root, AnimatorControllerData& out, std::vector<std::string>* warnings = nullptr);

        /// @brief JSON にする
        json Write(const AnimatorControllerData& data);

        /// @brief 新しく作るときの中身（`Base` のレイヤー 1 枚・状態は無し）
        AnimatorControllerData MakeEmpty();

        const char* ToString(AnimatorParameterType type);
        const char* ToString(AnimatorConditionOp op);

        /// @brief 種類と比べ方が組み合わせとして使えるか
        bool IsValidOp(AnimatorParameterType type, AnimatorConditionOp op);
    }

    /// @brief 読んだアニメーター設定をパスごとに共有する
    /// @details 窓で保存し直すと中身を差し替えて版を上げる。使う側は版が変わったら作り直す。
    class AnimatorControllerLibrary
    {
    public:
        /// @brief 共有する 1 件
        struct Entry
        {
            std::shared_ptr<const AnimatorControllerData> data;  ///< 読めなければ nullptr
            uint64_t revision = 0;
        };

        static AnimatorControllerLibrary& Get();

        /// @brief パスの設定を引く（初めてなら読む）
        /// @param assetPath `Application/Assets/…` の綴りか絶対パス
        /// @return 空のパスなら nullptr。読めなかったときも Entry は返す（data が nullptr）
        std::shared_ptr<const Entry> Load(const std::string& assetPath);

        /// @brief 中身を差し替えて版を上げる（窓で保存したときに呼ぶ）
        void Store(const std::string& assetPath, AnimatorControllerData data);

        /// @brief 覚えている設定を全部忘れる
        void Clear();

    private:
        AnimatorControllerLibrary() = default;

        static std::wstring MakeKey(const std::string& assetPath);

        std::unordered_map<std::wstring, std::shared_ptr<Entry>> entries_;
        uint64_t nextRevision_ = 1;
    };
}
