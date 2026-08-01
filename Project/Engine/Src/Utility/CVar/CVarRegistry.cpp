#include "pch.h"
#include "CVarRegistry.h"
#include "CVar.h"
#include "Utility/Logger/Logger.h"
#include <algorithm>

namespace CoreEngine
{
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

    std::vector<ICVar*> CVarRegistry::GetSortedByName() const
    {
        std::vector<ICVar*> sorted = cvars_;
        std::sort(sorted.begin(), sorted.end(), [](const ICVar* a, const ICVar* b) {
            return std::string_view(a->GetName()) < std::string_view(b->GetName());
        });
        return sorted;
    }

    std::vector<ICVar*> CVarRegistry::GetByPrefix(std::string_view prefix) const
    {
        std::vector<ICVar*> result;
        for (ICVar* cvar : cvars_) {
            const std::string_view name = cvar->GetName();
            if (prefix.empty() || name.compare(0, prefix.size(), prefix) == 0) {
                result.push_back(cvar);
            }
        }
        std::sort(result.begin(), result.end(), [](const ICVar* a, const ICVar* b) {
            return std::string_view(a->GetName()) < std::string_view(b->GetName());
        });
        return result;
    }

    void CVarRegistry::OnCVarChanged(ICVar* /*cvar*/)
    {
        ++globalRevision_;
    }

    void CVarRegistry::FlushPendingWarnings()
    {
        for (const std::string& warning : pendingWarnings_) {
            Logger::GetInstance().Warnf(LogCategory::System, "{}", warning);
        }
        pendingWarnings_.clear();

        Logger::GetInstance().Infof(LogCategory::System,
            "CVar: {} 個のコンソール変数を登録しました", cvars_.size());
    }
}
