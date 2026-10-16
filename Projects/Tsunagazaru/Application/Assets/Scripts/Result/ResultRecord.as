using namespace CoreEngine;

// 遊んだ記録（前回の距離と自己最高）。Application/Saved/result_record.json に残す
class ResultRecordEntry
{
    int previous = 0;        // 前回の距離（今回を含まない）
    int best = 0;            // 自己最高（今回を含まない）
    bool hasPrevious = false;
    bool isNewBest = false;  // 今回が自己最高を超えたか
}

namespace ResultRecord
{
    const string kPath = "result_record.json";

    // 今回の距離を書き込み、書き込む前の記録を返す
    ResultRecordEntry@ CommitRun(int meters)
    {
        SaveFile file(kPath);
        ResultRecordEntry entry;
        entry.previous = file.GetInt("lastMeters", 0);
        entry.best = file.GetInt("bestMeters", 0);
        entry.hasPrevious = file.GetBool("hasLast", false);
        entry.isNewBest = meters > entry.best;

        file.SetInt("lastMeters", meters);
        file.SetInt("bestMeters", Max(entry.best, meters));
        file.SetBool("hasLast", true);
        if (!file.Save()) {
            Warn("ResultRecord: 記録を保存できませんでした");
        }
        return entry;
    }
}
