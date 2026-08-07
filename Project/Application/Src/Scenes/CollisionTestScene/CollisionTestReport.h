#pragma once

#include <string>
#include <vector>
#include <deque>

namespace CoreEngine { class EngineSystem; }

/// @brief 当たり判定システムのリファクタリング用テスト基盤
/// @details リファクタリング前後で挙動が変わっていないことを確認するための
///          「安全網」。判定結果をここへ集約し、App Editor のパネルに表示する。
namespace CollisionTest
{
    /// @brief テストケースの判定状態
    enum class Status {
        Pending,  ///< まだ結論が出ていない（進行中）
        Pass,     ///< 期待どおり
        Fail,     ///< 期待と異なる（既知バグの再現を含む）
    };

    /// @brief テストケース 1 件の結果
    struct CaseResult {
        std::string id;        ///< "T1" などの識別子（Upsert のキー）
        std::string title;     ///< 表示名
        std::string expected;  ///< 期待する挙動
        std::string actual;    ///< 実際の挙動
        std::string knownBug;  ///< 紐づく既知バグ番号（"A-4" 等。空なら無し）
        std::string note;      ///< 補足
        Status      status = Status::Pending;
    };

    /// @brief テスト結果の集約先（プロセス唯一）
    /// @details シーンより長生きするシングルトン。App Editor へ登録するドロワーは
    ///          何もキャプチャせずここを読むだけにしてあり、シーン破棄後に
    ///          ダングリング参照が残らない（GameDebugUI に登録解除 API が無いため）。
    class Report {
    public:
        /// @brief シングルトンを取得
        static Report& Get();

        /// @brief 新しい検証セッションを開始（全ケース・ログをクリア）
        /// @param sessionName セッション名（シーン名など）
        void BeginSession(const std::string& sessionName);

        /// @brief テストケースを登録／上書き（id が一致すれば上書き）
        void Upsert(const CaseResult& result);

        /// @brief イベントログを 1 行追加（古い行から破棄）
        void Log(const std::string& line);

        /// @brief 現在フレーム番号を記録（ログ行の先頭に出す）
        void SetFrame(int frame) { frame_ = frame; }
        int  GetFrame() const { return frame_; }

        const std::vector<CaseResult>& GetCases() const { return cases_; }

        /// @brief A-2（コールバック中の RemoveCollider）再現を有効にするか
        /// @note 既定 false。有効にすると use-after-free でクラッシュする可能性があるため
        ///       パネルから明示的にオプトインさせる。
        bool  IsRemoveColliderInCallbackEnabled() const { return removeColliderInCallback_; }

        /// @brief テストの進行をリスタートしたいかどうか（シーン側が消費する）
        bool ConsumeRestartRequest();

#ifdef USE_IMGUI
        /// @brief App Editor へパネルを登録する（プロセス中 1 回だけ実行される）
        static void EnsureRegistered(CoreEngine::EngineSystem* engine);

        /// @brief パネル本体を描画
        void Draw();
#endif

    private:
        Report() = default;

        /// @brief 確定した判定をログへ 1 行出す（Pending → 確定 の遷移時のみ）
        void LogVerdict(const CaseResult& result);

        std::string             sessionName_;
        std::vector<CaseResult> cases_;
        std::deque<std::string> log_;
        int                     frame_ = 0;
        bool                    removeColliderInCallback_ = false;
        bool                    restartRequested_ = false;

        static constexpr size_t kMaxLogLines = 300;
    };

    /// @brief Report::Log の簡易版（フレーム番号を自動で前置）
    void LogEvent(const std::string& line);
}
