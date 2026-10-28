using namespace CoreEngine;

// 建物の輪郭（多角形）を区画ごとに持ち、円が輪郭に掛かるかを調べる
// （Tools/OkinawaBeach/bake_araha_walk.py が書き出す JSON）
class FootprintGrid
{
    private array<int>@ bucketStart_;
    private array<int>@ bucketItems_;
    private array<int>@ shapeStart_;
    private array<int>@ points_;
    private float unit_ = 0.1f;
    private float originX_ = 0.0f;
    private float originZ_ = 0.0f;
    private float cell_ = 32.0f;
    private int countX_ = 0;
    private int countZ_ = 0;

    // 読めたら true。読めなかったときは訳を problem に入れる
    bool Load(const string &in path, string &out problem)
    {
        problem = "";
        Clear();
        DataFile@ file = DataFile(path);
        if (!file.exists) {
            problem = "建物の輪郭 " + path + " がありません";
            return false;
        }
        unit_ = file.GetFloat("unit", 0.1f);
        originX_ = file.GetFloat("originX");
        originZ_ = file.GetFloat("originZ");
        cell_ = file.GetFloat("cell", 32.0f);
        countX_ = file.GetInt("countX");
        countZ_ = file.GetInt("countZ");
        array<int>@ bucketStart = file.GetIntArray("bucketStart");
        array<int>@ bucketItems = file.GetIntArray("bucketItems");
        array<int>@ shapeStart = file.GetIntArray("shapeStart");
        array<int>@ points = file.GetIntArray("points");
        if (bucketStart is null || bucketItems is null || shapeStart is null || points is null
            || int(bucketStart.length()) != countX_ * countZ_ + 1 || cell_ <= 0.0f) {
            problem = "建物の輪郭 " + path + " の大きさが合いません";
            return false;
        }
        @bucketStart_ = bucketStart;
        @bucketItems_ = bucketItems;
        @shapeStart_ = shapeStart;
        @points_ = points;
        return true;
    }

    void Clear()
    {
        @bucketStart_ = null;
        @bucketItems_ = null;
        @shapeStart_ = null;
        @points_ = null;
    }

    // 中心 (x, z)・半径 radius の円が、どれかの輪郭の中に入るか縁に掛かるか
    bool Overlaps(float x, float z, float radius) const
    {
        if (bucketStart_ is null) {
            return false;
        }
        const int i = int(floor((x - originX_) / cell_));
        const int j = int(floor((z - originZ_) / cell_));
        if (i < 0 || j < 0 || i >= countX_ || j >= countZ_) {
            return false;
        }
        const int bucket = j * countX_ + i;
        for (int k = bucketStart_[bucket]; k < bucketStart_[bucket + 1]; k++) {
            if (ShapeOverlaps(bucketItems_[k], x, z, radius)) {
                return true;
            }
        }
        return false;
    }

    private bool ShapeOverlaps(int shape, float x, float z, float radius) const
    {
        const int first = shapeStart_[shape];
        const int last = shapeStart_[shape + 1];
        const float radiusSq = radius * radius;
        bool inside = false;
        int previous = last - 1;
        for (int current = first; current < last; current++) {
            const float ax = float(points_[previous * 2]) * unit_;
            const float az = float(points_[previous * 2 + 1]) * unit_;
            const float bx = float(points_[current * 2]) * unit_;
            const float bz = float(points_[current * 2 + 1]) * unit_;
            // 点から右へ伸ばした線が辺を横切る回数で中か外かを決める
            if ((az > z) != (bz > z) && x < (bx - ax) * (z - az) / (bz - az) + ax) {
                inside = !inside;
            }
            if (SegmentDistanceSq(x, z, ax, az, bx, bz) < radiusSq) {
                return true;
            }
            previous = current;
        }
        return inside;
    }

    private float SegmentDistanceSq(float x, float z, float ax, float az, float bx, float bz) const
    {
        const float dx = bx - ax;
        const float dz = bz - az;
        const float lengthSq = dx * dx + dz * dz;
        float t = 0.0f;
        if (lengthSq > 0.0f) {
            t = Clamp(((x - ax) * dx + (z - az) * dz) / lengthSq, 0.0f, 1.0f);
        }
        const float ex = ax + dx * t - x;
        const float ez = az + dz * t - z;
        return ex * ex + ez * ez;
    }
}
