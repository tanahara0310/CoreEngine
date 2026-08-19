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


        /// @brief 接頭辞に一致する CVar を名前昇順で取得する
        /// @param prefix "r.Vignette" のような接頭辞（空文字なら全件）
        std::vector<ICVar*> GetByPrefix(std::string_view prefix) const;

        /// @brief いずれかの CVar が変更されるたびに増える通番
        /// @details 「どれか 1 つでも変わったか」を O(1) で判定するために使う。
        ///          自動保存の差分検知や、まとめて定数バッファを更新する用途向け。
        uint32_t GetGlobalRevision() const noexcept { return globalRevision_; }

        /// @brief 編集が「確定」するたびに増える通番
        /// @details 確定＝スライダーを離した・Enter を押した・チェックボックスをクリックした等。
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

        /// @brief 登録時に蓄積した警告をログへ出力し、バッファを空にする
        /// @details 静的初期化中は Logger が使えないため、エンジン初期化後に 1 回呼ぶ
        void FlushPendingWarnings();

    private:
        CVarRegistry() = default;
        ~CVarRegistry() = default;
        CVarRegistry(const CVarRegistry&) = delete;
        CVarRegistry& operator=(const CVarRegistry&) = delete;

        std::vector<ICVar*> cvars_;                             ///< 登録順
        std::unordered_map<std::string, ICVar*> lookup_;        ///< 名前 → CVar
        std::vector<std::string> pendingWarnings_;              ///< 静的初期化中に出た警告
        uint32_t globalRevision_ = 0;
        uint64_t commitRevision_ = 0;
    };
}
