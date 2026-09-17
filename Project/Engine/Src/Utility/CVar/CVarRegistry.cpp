#include "pch.h"
#include "CVarRegistry.h"
#include "CVar.h"
#include "Utility/Logger/Logger.h"
#include <algorithm>
#include <cassert>
#include <cctype>
#include <iterator>

namespace CoreEngine
{
    namespace
    {
        /// @brief 名前の昇順に並べる
        void SortByName(std::vector<ICVar*>& cvars)
        {
            std::sort(cvars.begin(), cvars.end(), [](const ICVar* a, const ICVar* b) {
                return std::string_view(a->GetName()) < std::string_view(b->GetName());
            });
        }

        /// @brief 名前を `.` で段に分ける
        std::vector<std::string_view> SplitSegments(std::string_view name)
        {
            std::vector<std::string_view> segments;
            size_t begin = 0;
            while (true) {
                const size_t dot = name.find('.', begin);
                if (dot == std::string_view::npos) {
                    segments.push_back(name.substr(begin));
                    return segments;
                }
                segments.push_back(name.substr(begin, dot - begin));
                begin = dot + 1;
            }
        }
    }

    CVarRegistry& CVarRegistry::Get()
    {
        // 静的初期化中（main より前）に CVar のコンストラクタから呼ばれるため、
        // 初期化順序が保証される関数内 static を使う
        static CVarRegistry instance;
        return instance;
    }

    void CVarRegistry::Register(ICVar* cvar)
    {
        if (!cvar) {
            return;
        }

        const std::string name = cvar->GetName();
        if (name.empty()) {
            pendingWarnings_.push_back("CVar: 名前が空の変数が登録されようとしました（無視）");
            return;
        }

        // 名前の重複は「片方の変更がもう片方に反映されない」という追いにくい不具合になるため拒否する
        if (lookup_.find(name) != lookup_.end()) {
            pendingWarnings_.push_back("CVar: 名前が重複しています: " + name + "（後から登録された方を無視）");
            return;
        }

        if (const std::string reason = CheckName(name); !reason.empty()) {
            nameViolations_.push_back(name + "（" + reason + "）");
        }

        cvars_.push_back(cvar);
        lookup_.emplace(name, cvar);
    }

    void CVarRegistry::Unregister(ICVar* cvar)
    {
        if (!cvar) {
            return;
        }

        auto it = std::find(cvars_.begin(), cvars_.end(), cvar);
        if (it != cvars_.end()) {
            cvars_.erase(it);
        }

        // 重複登録を拒否している都合上、同名の別インスタンスが残っている可能性はない。
        // ただし登録を拒否された CVar が破棄される経路があるため、
        // 自分自身が登録されている場合のみ削除する
        auto lookupIt = lookup_.find(cvar->GetName());
        if (lookupIt != lookup_.end() && lookupIt->second == cvar) {
            lookup_.erase(lookupIt);
        }
    }

    ICVar* CVarRegistry::Find(std::string_view name) const
    {
        auto it = lookup_.find(std::string(name));
        return it != lookup_.end() ? it->second : nullptr;
    }

    std::vector<ICVar*> CVarRegistry::GetByPrefix(std::string_view prefix) const
    {
        // 系統名が `.` で終わらないときは、系統名の直後が段の区切りか名前の終わりのものだけを返す
        const bool endsWithSeparator = !prefix.empty() && prefix.back() == '.';
        std::vector<ICVar*> result;
        for (ICVar* cvar : cvars_) {
            const std::string_view name = cvar->GetName();
            const bool matched = prefix.empty()
                || (name.starts_with(prefix)
                    && (endsWithSeparator || name.size() == prefix.size() || name[prefix.size()] == '.'));
            if (matched) {
                result.push_back(cvar);
            }
        }
        SortByName(result);
        return result;
    }

    std::vector<ICVar*> CVarRegistry::GetByNameStart(std::string_view text) const
    {
        std::vector<ICVar*> result;
        for (ICVar* cvar : cvars_) {
            if (std::string_view(cvar->GetName()).starts_with(text)) {
                result.push_back(cvar);
            }
        }
        SortByName(result);
        return result;
    }

    std::string CVarRegistry::CheckName(std::string_view name)
    {
        const std::vector<std::string_view> segments = SplitSegments(name);

        static constexpr std::string_view kPrefixes[] = { "r", "sys", "d" };
        if (std::find(std::begin(kPrefixes), std::end(kPrefixes), segments.front()) == std::end(kPrefixes)) {
            return "接頭辞は r. / sys. / d. のどれかにします";
        }
        if (segments.size() < 3) {
            return "<接頭辞>.<グループ>.<名前> の 3 段以上にします";
        }
        for (size_t i = 1; i < segments.size(); ++i) {
            const std::string_view segment = segments[i];
            const bool pascalCase = !segment.empty()
                && std::isupper(static_cast<unsigned char>(segment.front())) != 0
                && std::all_of(segment.begin(), segment.end(),
                    [](char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0; });
            if (!pascalCase) {
                return "各段は英大文字で始まる英数字にします";
            }
            const bool toggleVerb = segment.starts_with("Disable") || segment.ends_with("Enable")
                || (segment.starts_with("Enable") && !segment.starts_with("Enabled"));
            if (toggleVerb) {
                return "有効・無効を表す語は Enabled にします（Enable / Disable は使いません）";
            }
        }
        return {};
    }

    void CVarRegistry::OnCVarChanged(ICVar* /*cvar*/)
    {
        ++globalRevision_;
    }

    void CVarRegistry::FlushPendingWarnings()
    {
        auto& logger = Logger::GetInstance();
        for (const std::string& warning : pendingWarnings_) {
            logger.Warnf(LogCategory::System, "{}", warning);
        }
        pendingWarnings_.clear();

        for (const std::string& violation : nameViolations_) {
            logger.Errorf(LogCategory::System, "CVar: 名前が決まりに合いません: {}", violation);
        }
        if (!nameViolations_.empty()) {
            logger.Flush();
        }
        assert(nameViolations_.empty() && "CVar の名前が決まりに合いません（System ログを参照）");
        nameViolations_.clear();

        logger.Infof(LogCategory::System,
            "CVar: {} 個のコンソール変数を登録しました", cvars_.size());
    }
}
