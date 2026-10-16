using namespace CoreEngine;

// マスの種類（ステージのデータの数字と同じ並び）
enum MapChip
{
    Void = 0,       // 空白。見た目は地面と硬い岩
    Water = 1,      // 水（レールを敷くと橋になる）
    Ground = 2,     // 地面
    Station = 3,    // 駅
    Resource = 4,   // 岩（壊してから通る）
    BananaTree = 5, // バナナの木
    Grass = 6       // 草（見た目だけ。地面と同じ扱い）
}

// マップを作る。先頭にステージの固定マップを置き、その後ろはエリアの区画を区画ごとに継ぐ。
// 区画は全部を 1 回ずつ使い切ってから並べ直す（シャッフルバッグ）。X 方向へは必要な分だけ伸ばす
[DisplayName("マップ生成")]
class MapGenerator : ScriptComponent
{
    [DisplayName("ステージのデータ（Application/Assets からのパス）")]
    string stageData = "Data/Stage.json";

    [DisplayName("最初に作る列数"), Range(1, 500)]
    int initialGenerateSizeX = 30;

    [DisplayName("奥行きのマス数"), Range(1, 100)]
    int mapSizeZ = 11;

    [DisplayName("使うエリア（空ならデータの既定）")]
    string area = "";

    [DisplayName("区画の並びの種（0 なら毎回変える）")]
    int randomSeed = 0;

    // chips_[x][z]
    private array<array<int>> chips_;
    private array<array<int>> fixed_;
    private array<array<array<int>>> chunks_;
    private array<int> shuffled_;
    private uint nextShuffled_ = 0;
    private int activeChunk_ = -1;
    private int activeColumn_ = 0;
    private int chunkSizeX_ = 10;
    private RandomStream@ random_;
    private bool loaded_ = false;

    // ほかの部品が Start から列を求めても間に合うように、付けた直後に読んで最初の列を作る
    void Awake() override
    {
        Load();
    }

    // 読み直しで中身が初期値に戻るので、同じ手順で作り直す
    void OnScriptReloaded() override
    {
        Load();
    }

    // ---------------------------------------------------------------- 問い合わせ

    // 作った列の数
    int GetSizeX() const
    {
        return int(chips_.length());
    }

    // 先頭の固定マップの幅
    int GetFixedMapSizeX() const
    {
        return int(fixed_.length());
    }

    // マスの種類。範囲外は Void。駅の正面のマスは地面として返す
    int GetChip(int x, int z) const
    {
        if (x < 0 || z < 0 || x >= int(chips_.length()) || z >= mapSizeZ) {
            return MapChip::Void;
        }
        if (IsStationRailCell(x, z)) {
            return MapChip::Ground;
        }
        return chips_[x][z];
    }

    // 駅の正面（-Z 側の隣）は、常設のレールを置く地面にする
    bool IsStationRailCell(int x, int z) const
    {
        return x >= 0 && z >= 0 && x < int(chips_.length()) && mapSizeZ > 1 && z < mapSizeZ - 1
            && chips_[x][z] != MapChip::Station
            && chips_[x][z + 1] == MapChip::Station;
    }

    // 隣のマスへレールをつなげられるか（空白・バナナの木・駅には入れない）
    bool CanConnectRail(int fromX, int fromZ, int toX, int toZ) const
    {
        if (fromX < 0 || fromZ < 0 || toX < 0 || toZ < 0
            || AbsInt(toX - fromX) + AbsInt(toZ - fromZ) != 1) {
            return false;
        }
        const int destination = GetChip(toX, toZ);
        return destination != MapChip::Void && destination != MapChip::BananaTree
            && destination != MapChip::Station;
    }

    // ---------------------------------------------------------------- 変更

    // xCount 列になるまで作る
    void CreateToX(int xCount)
    {
        if (xCount > int(chips_.length())) {
            AddMapChips(xCount - int(chips_.length()));
        }
    }

    // マスの種類を変える（足りない列は作る）
    bool SetChip(int x, int z, int type)
    {
        if (x < 0 || z < 0 || z >= mapSizeZ) {
            return false;
        }
        CreateToX(x + 1);
        chips_[x][z] = type;
        return true;
    }

    // ---------------------------------------------------------------- 作る

    private void Load()
    {
        chips_.resize(0);
        fixed_.resize(0);
        chunks_.resize(0);
        shuffled_.resize(0);
        nextShuffled_ = 0;
        activeChunk_ = -1;
        activeColumn_ = 0;

        DataFile data(stageData);
        if (!data.exists) {
            Warn("MapGenerator: ステージのデータ " + stageData + " がありません。空白で作ります");
        }
        chunkSizeX_ = Max(1, data.GetInt("chunkSizeX", 10));
        fixed_ = ParseMap(data.GetString("fixed"), 0);

        string areaName = area.isEmpty() ? data.GetString("initialArea") : area;
        string[]@ chunkTexts = data.GetStringArray("area." + areaName);
        if (chunkTexts is null || chunkTexts.length() == 0) {
            Warn("MapGenerator: エリア " + areaName + " の区画がありません。固定マップの後ろは空白で作ります");
        } else {
            for (uint i = 0; i < chunkTexts.length(); ++i) {
                chunks_.insertLast(ParseMap(chunkTexts[i], chunkSizeX_));
            }
        }

        const uint seed = (randomSeed != 0) ? uint(randomSeed) : uint(Random::Range(1, 0x7fffffff));
        @random_ = RandomStream(seed);
        loaded_ = true;
        AddMapChips(initialGenerateSizeX);
    }

    private void AddMapChips(int count)
    {
        for (int i = 0; i < count; ++i) {
            if (chips_.length() < fixed_.length()) {
                chips_.insertLast(fixed_[chips_.length()]);
                continue;
            }
            if (chunks_.length() == 0) {
                chips_.insertLast(MakeVoidColumn());
                continue;
            }
            // 区画の途中では選び直さない
            if (activeColumn_ == 0) {
                if (nextShuffled_ >= shuffled_.length()) {
                    RefillShuffleBag();
                }
                activeChunk_ = shuffled_[nextShuffled_];
                ++nextShuffled_;
            }
            chips_.insertLast(chunks_[activeChunk_][activeColumn_]);
            activeColumn_ = (activeColumn_ + 1) % chunkSizeX_;
        }
    }

    // 区画の番号を全部並べて混ぜる
    private void RefillShuffleBag()
    {
        shuffled_.resize(chunks_.length());
        for (uint i = 0; i < shuffled_.length(); ++i) {
            shuffled_[i] = int(i);
        }
        for (int i = int(shuffled_.length()) - 1; i > 0; --i) {
            const int j = random_.UniformInt(0, i);
            const int swapValue = shuffled_[i];
            shuffled_[i] = shuffled_[j];
            shuffled_[j] = swapValue;
        }
        nextShuffled_ = 0;
    }

    private array<int> MakeVoidColumn() const
    {
        array<int> column(mapSizeZ, MapChip::Void);
        return column;
    }

    // "行/行/…"（行は "数,数,…"・1 行目が Z = 0）を [x][z] の形にする。
    // width が 0 なら一番長い行の幅、そうでなければ足りない列は空白・余る列は捨てる
    private array<array<int>> ParseMap(const string &in text, int width) const
    {
        array<array<int>> map;
        if (text.isEmpty()) {
            map.resize(width);
            for (uint x = 0; x < map.length(); ++x) {
                map[x] = MakeVoidColumn();
            }
            return map;
        }

        array<string> rows = SplitText(text, "/");
        int columns = width;
        if (columns == 0) {
            for (uint z = 0; z < rows.length() && int(z) < mapSizeZ; ++z) {
                columns = Max(columns, int(SplitText(rows[z], ",").length()));
            }
        }
        map.resize(columns);
        for (int x = 0; x < columns; ++x) {
            map[x] = MakeVoidColumn();
        }
        for (uint z = 0; z < rows.length() && int(z) < mapSizeZ; ++z) {
            array<string> cells = SplitText(rows[z], ",");
            for (uint x = 0; x < cells.length() && int(x) < columns; ++x) {
                map[x][z] = ParseChip(cells[x]);
            }
        }
        return map;
    }

    // 数字（0〜6）か英語名。分からない値は空白
    private int ParseChip(const string &in cell) const
    {
        if (cell == "0" || cell == "Void") return MapChip::Void;
        if (cell == "1" || cell == "Water") return MapChip::Water;
        if (cell == "2" || cell == "Ground") return MapChip::Ground;
        if (cell == "3" || cell == "Station") return MapChip::Station;
        if (cell == "4" || cell == "Resource") return MapChip::Resource;
        if (cell == "5" || cell == "BananaTree") return MapChip::BananaTree;
        if (cell == "6" || cell == "Grass") return MapChip::Grass;
        return MapChip::Void;
    }
}

int AbsInt(int value)
{
    return value < 0 ? -value : value;
}

// text を separator で区切る（空の欄も 1 つと数える）
array<string> SplitText(const string &in text, const string &in separator)
{
    array<string> parts;
    uint start = 0;
    while (true) {
        const int found = text.findFirst(separator, start);
        if (found < 0) {
            parts.insertLast(text.substr(start));
            break;
        }
        parts.insertLast(text.substr(start, found - int(start)));
        start = uint(found) + separator.length();
    }
    return parts;
}
