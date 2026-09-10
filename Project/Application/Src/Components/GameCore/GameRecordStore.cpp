#include "pch.h"
#include "GameRecordStore.h"

#include "Utility/JsonManager/JsonManager.h"

#include <algorithm>

using namespace CoreEngine;

namespace
{
    constexpr const char* kRecordPath = "Application/Config/result_record.json";
    constexpr const char* kKeyLast = "lastMeters";
    constexpr const char* kKeyBest = "bestMeters";
    constexpr const char* kKeyHasLast = "hasLast";

    struct Stored
    {
        std::uint32_t last = 0;
        std::uint32_t best = 0;
        bool hasLast = false;
    };

    Stored ReadStored()
    {
        Stored stored;
        const json data = JsonManager::GetInstance().LoadJson(kRecordPath);
        if (data.empty() || !data.is_object()) {
            return stored;
        }
        stored.last = JsonManager::SafeGet<std::uint32_t>(data, kKeyLast, 0u);
        stored.best = JsonManager::SafeGet<std::uint32_t>(data, kKeyBest, 0u);
        stored.hasLast = JsonManager::SafeGet<bool>(data, kKeyHasLast, false);
        return stored;
    }
}

GameComponents::GameRecordStore::Record GameComponents::GameRecordStore::Peek()
{
    const Stored stored = ReadStored();
    Record record;
    record.previous = stored.last;
    record.best = stored.best;
    record.hasPrevious = stored.hasLast;
    return record;
}

GameComponents::GameRecordStore::Record
GameComponents::GameRecordStore::CommitRun(std::uint32_t meters)
{
    const Stored stored = ReadStored();

    // 返すのは「今回を含めない」値。杭を立てる相手は今回ではなく前回だから。
    Record record;
    record.previous = stored.last;
    record.best = stored.best;
    record.hasPrevious = stored.hasLast;
    record.isNewBest = meters > stored.best;

    json data;
    data[kKeyLast] = meters;
    data[kKeyBest] = (std::max)(stored.best, meters);
    data[kKeyHasLast] = true;
    JsonManager::GetInstance().SaveJson(kRecordPath, data);

    return record;
}
