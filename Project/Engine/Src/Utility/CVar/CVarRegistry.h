#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

/// @brief CVar のグローバルレジストリ

namespace CoreEngine
{
    class ICVar;

    /// @brief 全 CVar を一元管理するレジストリ
    /// @details CVar のコンストラクタが自動的に自分を登録する。UI（CVarPanel）と
    ///          自動保存（CVarSettingsSection）は、このリストを走査するだけで動く。
    ///          「機能を追加したのに UI/保存へ足し忘れる」事故を構造的に防ぐのが目的。
    ///
    ///          静的初期化中（main より前）に登録が走るため、Meyers シングルトンで
    ///          初期化順序問題を回避している。この時点では Logger が未初期化の可能性が
    ///          あるので、警告は溜めておき FlushPendingWarnings() でまとめて出力する。
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
    };
}
