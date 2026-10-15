// マスを 1 つの整数で表す（Z は 1024 未満）
int CellKey(int x, int z)
{
    return x * 1024 + z;
}

int CellX(int key)
{
    return key / 1024;
}

int CellZ(int key)
{
    return key % 1024;
}

// 取り消しの結果
class RailUndoResult
{
    bool succeeded = false;
    int removedCell = -1;  // 取り消したマス
    int builderCell = -1;  // 取り消した後にカーソルを置くマス
    float refundAmount = 0.0f;
}

// レールの並び。トロッコが通って確定したレール・まだ取り消せるレール・トロッコがこれから走る順の 3 本を持つ。
// 分岐は無く、1 本の連続した列
[DisplayName("レールの経路")]
class RailPath : ScriptComponent
{
    [DisplayName("奥行きのマス数"), Range(1, 100)]
    int mapSizeZ = 11;

    [DisplayName("開始のマス X"), Range(0, 500)]
    int startX = 1;

    [DisplayName("開始のマス Z"), Range(0, 99)]
    int startZ = 4;

    // 確定したレール（開始のマスを含み、増える一方）
    private array<int> railMap_;
    // まだ取り消せるレールと、取り消したときに返すスタミナ
    private array<int> undoStack_;
    private array<float> undoCosts_;
    // トロッコがこれから走るマス
    private array<int> routeQueue_;

    void Awake() override
    {
        Reset();
    }

    void OnScriptReloaded() override
    {
        Reset();
    }

    void Reset()
    {
        startZ = Clamp(startZ, 0, mapSizeZ - 1);
        railMap_.resize(0);
        undoStack_.resize(0);
        undoCosts_.resize(0);
        routeQueue_.resize(0);
        railMap_.insertLast(CellKey(startX, startZ));
    }

    // レールを置く（取り消せるレールとトロッコの道筋の両方へ足す）
    bool PlaceRail(int x, int z, float refundableStaminaCost)
    {
        if (x < 0 || z < 0 || z >= mapSizeZ) {
            Warn("RailPath: 範囲の外にレールを置こうとしました");
            return false;
        }
        const int key = CellKey(x, z);
        undoStack_.insertLast(key);
        undoCosts_.insertLast(refundableStaminaCost);
        routeQueue_.insertLast(key);
        return true;
    }

    // 最後に置いた取り消せるレールを取り消す
    RailUndoResult@ UndoLastRailPlacement()
    {
        RailUndoResult@ result = RailUndoResult();
        if (undoStack_.length() == 0 || undoCosts_.length() == 0) {
            Warn("RailPath: 取り消せるレールがありません");
            return result;
        }

        result.succeeded = true;
        result.removedCell = undoStack_[undoStack_.length() - 1];
        result.refundAmount = undoCosts_[undoCosts_.length() - 1];
        undoStack_.removeLast();
        undoCosts_.removeLast();

        if (routeQueue_.length() > 0 && routeQueue_[routeQueue_.length() - 1] == result.removedCell) {
            routeQueue_.removeLast();
        }

        result.builderCell = (undoStack_.length() > 0)
            ? undoStack_[undoStack_.length() - 1]
            : railMap_[railMap_.length() - 1];
        return result;
    }

    // トロッコが次のマスへ向かうときに、そのマスのレールを確定する
    bool ConfirmNextRailPlacement()
    {
        if (routeQueue_.length() == 0) {
            Warn("RailPath: 確定するレールがありません");
            return false;
        }

        const int key = routeQueue_[0];
        const bool alreadyConfirmed = railMap_.find(key) >= 0;
        if (!alreadyConfirmed) {
            if (undoStack_.length() == 0 || undoStack_[0] != key) {
                Error("RailPath: レールの並びが食い違っています");
                return false;
            }
            railMap_.insertLast(key);
            undoStack_.removeAt(0);
            undoCosts_.removeAt(0);
        }

        routeQueue_.removeAt(0);
        return true;
    }

    // 取り消せるレールを全部確定する
    void ConfirmAllPendingRailPlacements()
    {
        for (uint i = 0; i < undoStack_.length(); ++i) {
            railMap_.insertLast(undoStack_[i]);
        }
        undoStack_.resize(0);
        undoCosts_.resize(0);
    }

    // トロッコが次に向かうマス（無ければ -1）
    int GetNextUnconfirmedRail() const
    {
        return routeQueue_.length() > 0 ? routeQueue_[0] : -1;
    }

    // トロッコがこれから走るマスの数
    int GetUnconfirmedRailCount() const
    {
        return int(routeQueue_.length());
    }

    // 敷いたレールの数（開始のマスは数えない）
    int GetLaidRailCount() const
    {
        const int count = int(railMap_.length() + undoStack_.length());
        return count > 0 ? count - 1 : 0;
    }

    int GetConfirmedCount() const
    {
        return int(railMap_.length());
    }

    int GetConfirmed(int index) const
    {
        return railMap_[index];
    }

    int GetLastConfirmed() const
    {
        return railMap_[railMap_.length() - 1];
    }

    int GetPendingCount() const
    {
        return int(undoStack_.length());
    }

    int GetPending(int index) const
    {
        return undoStack_[index];
    }

    // 確定と未確定を続けて数えた index 番目（0 が開始のマス）
    int GetRail(int index) const
    {
        const int confirmed = int(railMap_.length());
        return index < confirmed ? railMap_[index] : undoStack_[index - confirmed];
    }

    int GetRailCount() const
    {
        return int(railMap_.length() + undoStack_.length());
    }

    // そのマスに確定・未確定のどちらかのレールがあるか
    bool HasRail(int x, int z) const
    {
        const int key = CellKey(x, z);
        return railMap_.find(key) >= 0 || undoStack_.find(key) >= 0;
    }
}
