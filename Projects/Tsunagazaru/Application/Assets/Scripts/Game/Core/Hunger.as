// バナナを 1 本取った瞬間の情報（演出だけに使う）
class BananaHarvestEvent
{
    int monkeyIndex = 0;   // 取ったサルの番号（0 が先頭）
    int treeGridX = 0;     // 取られた木のマス
    int treeGridZ = 0;
    int monkeyGridX = 0;   // そのサルがいるマス
    int monkeyGridZ = 0;
    float amountRate = 1.0f;  // 1 匹あたりの回復量が満額の何割か
    float amount = 0.0f;      // この木 1 本ぶんで回復した量
    int indexInFrame = 0;     // 同じ瞬間に取れたうちの何本目か
}

funcdef void MonkeyAddedCallback(int monkeyCount);
funcdef void StationCallback(int stationX, int stationZ);
funcdef void BananaHarvestCallback(BananaHarvestEvent@ harvest);

// 全部のサルで共有するスタミナ。敷いたときだけ減り、時間では減らない。
// サルがバナナの木の上下左右を通ると回復し、駅から車両を連結するとサルが増える
[DisplayName("スタミナ")]
class Hunger : ScriptComponent
{
    [DisplayName("最初のスタミナ"), Range(0, 1000)]
    float initialStamina = 120.0f;

    [DisplayName("スタミナの上限"), Range(0.01, 1000)]
    float maximumStamina = 120.0f;

    [DisplayName("0 になったらゲームオーバー")]
    bool gameOverAtZeroStamina = false;

    [DisplayName("バナナの木 1 本の回復量"), Range(0, 200)]
    float bananaRecovery = 20.0f;

    [DisplayName("サルが増えたときの回復の補正率"), Range(0, 1)]
    float bananaRecoveryMonkeyCorrectionRate = 0.35f;

    [DisplayName("サル 1 匹あたり増える費用の割合"), Range(0, 2)]
    float additionalMonkeyCostRate = 0.25f;

    [DisplayName("レールの費用"), Range(0, 100)]
    float railCost = 1.0f;

    [DisplayName("岩を壊す費用"), Range(0, 100)]
    float rockCost = 10.0f;

    [DisplayName("橋の費用"), Range(0, 100)]
    float bridgeCost = 5.0f;

    private MapGenerator@ map_;
    private GameManager@ gameManager_;
    private float current_ = 0.0f;
    private bool gameOverRequested_ = false;
    private int monkeyCount_ = 1;
    private MonkeyAddedCallback@ onMonkeyAdded_;
    private StationCallback@ onStationPop_;
    private StationCallback@ onStationEntered_;
    private BananaHarvestCallback@ onBananaHarvest_;
    // 発動した駅（サルが増えるたびに古い順に取り出す）
    private array<int> pendingMonkeyStations_;
    // サル・木・木のどの側から通ったかの組ごとに 1 回だけ回復する
    private array<int64> activatedBananaSides_;
    private array<int> activatedStations_;

    // GameDirector が最初に呼ぶ
    void Setup(MapGenerator@ map, GameManager@ gameManager)
    {
        @map_ = map;
        @gameManager_ = gameManager;
        current_ = Clamp(initialStamina, 0.0f, GetMaximum());
        gameOverRequested_ = false;
        monkeyCount_ = 1;
        pendingMonkeyStations_.resize(0);
        activatedBananaSides_.resize(0);
        activatedStations_.resize(0);
    }

    void SetMonkeyAddedCallback(MonkeyAddedCallback@ callback) { @onMonkeyAdded_ = callback; }
    void SetStationPopCallback(StationCallback@ callback) { @onStationPop_ = callback; }
    void SetStationEnteredCallback(StationCallback@ callback) { @onStationEntered_ = callback; }
    void SetBananaHarvestCallback(BananaHarvestCallback@ callback) { @onBananaHarvest_ = callback; }

    float GetCurrent() const { return Min(current_, GetMaximum()); }
    float GetMaximum() const { return Max(0.01f, maximumStamina); }
    int GetMonkeyCount() const { return monkeyCount_; }

    float GetCostMultiplier() const
    {
        return 1.0f + float(monkeyCount_ - 1) * Max(0.0f, additionalMonkeyCostRate);
    }

    // サルの数を掛けた費用（端数は切り上げ）
    float CalculateActionCost(float baseAmount) const
    {
        return ceil(Max(0.0f, baseAmount) * GetCostMultiplier());
    }

    // 先頭がマスの中央に着いたとき。まだ通っていない駅の正面なら true（この後トロッコが減速する）
    bool OnTrainEnteredCell(int gridX, int gridZ)
    {
        if (map_ is null || !IsPlaying()) {
            return false;
        }

        bool stationActivated = false;
        const int stationKey = CellKey(gridX, gridZ + 1);
        if (gridX >= 0 && gridZ >= 0 && map_.IsStationRailCell(gridX, gridZ)
            && activatedStations_.find(stationKey) < 0) {
            activatedStations_.insertLast(stationKey);
            stationActivated = true;
            // 駅はレールの 1 マス奥。サルが増えたときに弾ませる
            pendingMonkeyStations_.insertLast(stationKey);
            if (onStationEntered_ !is null) {
                onStationEntered_(gridX, gridZ + 1);
            }
        }

        OnMonkeyEnteredCell(0, gridX, gridZ);
        return stationActivated;
    }

    // 各サルがマスの中央に着いたとき。上下左右のバナナの木から回復する
    void OnMonkeyEnteredCell(int monkeyIndex, int gridX, int gridZ)
    {
        if (monkeyIndex >= monkeyCount_ || map_ is null || !IsPlaying()) {
            return;
        }

        const array<int> offsetX = { 1, -1, 0, 0 };
        const array<int> offsetZ = { 0, 0, 1, -1 };
        array<int> treesX;
        array<int> treesZ;
        for (uint side = 0; side < 4; ++side) {
            const int treeX = gridX + offsetX[side];
            const int treeZ = gridZ + offsetZ[side];
            if (treeX < 0 || treeZ < 0 || map_.GetChip(treeX, treeZ) != MapChip::BananaTree) {
                continue;
            }
            const int64 key = ((int64(monkeyIndex) * 16777216 + treeX) * 16 + treeZ) * 4 + side;
            if (activatedBananaSides_.find(key) >= 0) {
                continue;
            }
            activatedBananaSides_.insertLast(key);
            treesX.insertLast(treeX);
            treesZ.insertLast(treeZ);
        }

        const int triggeredCount = int(treesX.length());
        if (triggeredCount == 0) {
            return;
        }

        // サルが増えるほど 1 匹あたりの回復量を減らす
        const float correctionRate = Clamp(bananaRecoveryMonkeyCorrectionRate, 0.0f, 1.0f);
        const float monkeyCorrection = 1.0f / (1.0f + float(monkeyCount_ - 1) * correctionRate);
        const float recovery = Max(0.0f, bananaRecovery) * float(triggeredCount) * monkeyCorrection;
        AddStamina(recovery);
        GameResult::AddBananaHarvestCount(triggeredCount);

        if (onBananaHarvest_ !is null) {
            for (int index = 0; index < triggeredCount; ++index) {
                BananaHarvestEvent@ harvest = BananaHarvestEvent();
                harvest.monkeyIndex = monkeyIndex;
                harvest.treeGridX = treesX[index];
                harvest.treeGridZ = treesZ[index];
                harvest.monkeyGridX = gridX;
                harvest.monkeyGridZ = gridZ;
                harvest.amountRate = monkeyCorrection;
                harvest.amount = recovery / float(triggeredCount);
                harvest.indexInFrame = index;
                onBananaHarvest_(harvest);
            }
        }
    }

    // 駅から車両を連結したとき
    void AddMonkey()
    {
        ++monkeyCount_;
        if (onMonkeyAdded_ !is null) {
            onMonkeyAdded_(monkeyCount_);
        }
        if (pendingMonkeyStations_.length() > 0) {
            const int stationKey = pendingMonkeyStations_[0];
            pendingMonkeyStations_.removeAt(0);
            if (onStationPop_ !is null) {
                onStationPop_(CellX(stationKey), CellZ(stationKey));
            }
        }
    }

    // 足りるときだけ減らす
    bool TryConsumeStamina(float amount)
    {
        if (amount <= 0.0f) {
            return true;
        }
        if (gameOverRequested_ || !IsPlaying()) {
            return false;
        }
        if (GetCurrent() < amount) {
            return false;
        }

        current_ = Max(0.0f, GetCurrent() - amount);
        if (current_ <= 0.0f && gameOverAtZeroStamina) {
            gameOverRequested_ = true;
            gameManager_.RequestGameOver();
        }
        return true;
    }

    void AddStamina(float amount)
    {
        if (amount > 0.0f) {
            current_ = Min(GetMaximum(), GetCurrent() + amount);
        }
    }

    private bool IsPlaying() const
    {
        return gameManager_ !is null && gameManager_.IsPlaying();
    }
}
