#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

/// @file
/// @brief CVar のグローバルレジストリ

namespace CoreEngine
{
    class ICVar;

    /// @brief 全 CVar を一元管理するレジストリ
    /// @details CVar のコンストラクタが自分を登録するので、UI と自動保存はこのリストを走査するだけで動く。
    /// @note 静的初期化中に登録が走るため Meyers シングルトン。
    ///       この時点では Logger が未初期化なので、警告は FlushPendingWarnings() でまとめて出す。
    class CVarRegistry
    {
    public:
        static CVarRegistry& Get();

        /// @brief CVar を登録する（CVar のコンストラクタから呼ばれる）
        /// @note 名前が重複した場合は登録を拒否し、警告を蓄積する
        void Register(ICVar* cvar);

        /// @brief CVar の登録を解除する（CVar のデストラクタから呼ばれる）
        void Unregister(ICVar* cvar);

        /// @brief 名前で検索する
        /// @return 見つからなければ nullptr
        ICVar* Find(std::string_view name) const;

        /// @brief 登録順の全 CVar
        const std::vector<ICVar*>& GetAll() const noexcept { return cvars_; }

        /// @brief 系統に含まれる CVar を名前昇順で取得する
        /// @param prefix "r.Vignette" のような系統名（空文字なら全件）
        /// @details 名前が系統名と一致するものと、系統名の直後が `.` のものを返す（`r.SSAO` は `r.SSAOBlur.*` を含まない）。
        ///          系統名が `.` で終わるときは、その後に続く名前を返す。
        std::vector<ICVar*> GetByPrefix(std::string_view prefix) const;

        /// @brief 名前が指定の文字列で始まる CVar を名前昇順で取得する
        /// @param text 名前の先頭（段の途中で切れていてもよい）
        std::vector<ICVar*> GetByNameStart(std::string_view text) const;

        /// @brief 名前が決まりに合っているかを調べる
        /// @return 合っていなければ理由（合っていれば空）
        /// @details 決まり：接頭辞は r.（描画）/ sys.（システム）/ d.（エディタ）のどれか。
        ///          `<接頭辞>.<グループ>.<名前>` の 3 段以上。2 段目以降は英大文字で始まる英数字。
        ///          有効・無効を表す語は `Enabled`（`Enable` / `Disable` は使わない）。
        static std::string CheckName(std::string_view name);

        /// @brief いずれかの CVar が変更されるたびに増える通番
        /// @details 「どれか 1 つでも変わったか」を O(1) で判定するために使う。
        ///          自動保存の差分検知や、まとめて定数バッファを更新する用途向け。
        uint32_t GetGlobalRevision() const noexcept { return globalRevision_; }

        /// @brief 編集が「確定」するたびに増える通番
        /// @details 確定＝ドラッグを離した・Enter を押した・チェックボックスをクリックした等。
        ///          ドラッグ中の毎フレーム変更（GetGlobalRevision が進む）と区別し、
        ///          自動保存が「確定した瞬間に書き込む」ために使う。
        ///          設計書: Docs/Engine/Editor/InstantSettingsSave_Design.md
        uint64_t GetCommitRevision() const noexcept { return commitRevision_; }

        /// @brief 編集の確定を通知する（UI の確定イベントから呼ぶ）
        /// @details CVarPanel は ImGui::IsItemDeactivatedAfterEdit で自動的に呼ぶ。
        ///          CVar を書き換える専用 UI（水面パネル等の手書きウィジェット）も確定時に
        ///          呼ぶと即時保存になる。呼ばなくてもデバウンス保存（0.3 秒後）が働く。
        void NotifyCommit() noexcept { ++commitRevision_; }

        /// @brief 変更通知（ICVar::NotifyChanged から呼ばれる）
        void OnCVarChanged(ICVar* cvar);

        /// @brief 登録時に蓄積した警告と名前の決まりの違反をログへ出力し、バッファを空にする
        /// @details 静的初期化中は Logger が使えないため、エンジン初期化後に 1 回呼ぶ。
        ///          名前の決まりの違反はエラーとして出し、assert で止める
        void FlushPendingWarnings();

    private:
        CVarRegistry() = default;
        ~CVarRegistry() = default;
        CVarRegistry(const CVarRegistry&) = delete;
        CVarRegistry& operator=(const CVarRegistry&) = delete;

        std::vector<ICVar*> cvars_;                             ///< 登録順
        std::unordered_map<std::string, ICVar*> lookup_;        ///< 名前 → CVar
        std::vector<std::string> pendingWarnings_;              ///< 静的初期化中に出た警告
        std::vector<std::string> nameViolations_;               ///< 名前の決まりに合わない CVar と理由
        uint32_t globalRevision_ = 0;
        uint64_t commitRevision_ = 0;
    };
}
