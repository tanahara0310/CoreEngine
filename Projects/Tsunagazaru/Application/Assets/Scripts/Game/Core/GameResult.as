// 1 回のプレイの結果。GameScene が書き、ResultScene が読む（シーンをまたぐので Session に置く）
namespace GameResult
{
    const string kProgressBlocks = "GameResult.HorizontalProgressBlocks";
    const string kMonkeyCount = "GameResult.MonkeyCount";
    const string kBrokenRockCount = "GameResult.BrokenRockCount";
    const string kBananaHarvestCount = "GameResult.BananaHarvestCount";

    void Reset()
    {
        Session::SetInt(kProgressBlocks, 0);
        Session::SetInt(kMonkeyCount, 0);
        Session::SetInt(kBrokenRockCount, 0);
        Session::SetInt(kBananaHarvestCount, 0);
    }

    void SetHorizontalProgressBlocks(int blocks)
    {
        Session::SetInt(kProgressBlocks, Max(blocks, 0));
    }

    // 0 なら 1 にする
    void SetMonkeyCount(int count)
    {
        Session::SetInt(kMonkeyCount, Max(count, 1));
    }

    void AddBrokenRockCount(int count = 1)
    {
        Session::SetInt(kBrokenRockCount, Session::GetInt(kBrokenRockCount) + count);
    }

    void AddBananaHarvestCount(int count)
    {
        Session::SetInt(kBananaHarvestCount, Session::GetInt(kBananaHarvestCount) + count);
    }

    int GetHorizontalProgressBlocks() { return Session::GetInt(kProgressBlocks); }
    int GetMonkeyCount() { return Max(Session::GetInt(kMonkeyCount), 1); }
    int GetBrokenRockCount() { return Session::GetInt(kBrokenRockCount); }
    int GetBananaHarvestCount() { return Session::GetInt(kBananaHarvestCount); }
}
