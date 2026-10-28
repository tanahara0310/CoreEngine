using namespace CoreEngine;

// 地面の高さの表（Tools/OkinawaBeach の bake_*_walk.py が書き出す JSON）を読んで、双一次で引く
class GroundGrid
{
    private array<int>@ heights_;
    private float originX_ = 0.0f;
    private float originZ_ = 0.0f;
    private float cell_ = 0.5f;
    private int countX_ = 0;
    private int countZ_ = 0;
    private int noGround_ = -9999;

    // 読めたら true。読めなかったときは訳を problem に入れる
    bool Load(const string &in path, string &out problem)
    {
        problem = "";
        @heights_ = null;
        DataFile@ file = DataFile(path);
        if (!file.exists) {
            problem = "地面の高さの表 " + path + " がありません";
            return false;
        }
        originX_ = file.GetFloat("originX");
        originZ_ = file.GetFloat("originZ");
        cell_ = file.GetFloat("cell", 0.5f);
        countX_ = file.GetInt("countX");
        countZ_ = file.GetInt("countZ");
        noGround_ = file.GetInt("noGround", -9999);
        array<int>@ heights = file.GetIntArray("heights");
        if (heights is null || int(heights.length()) != countX_ * countZ_ || cell_ <= 0.0f) {
            problem = "地面の高さの表 " + path + " の大きさが合いません";
            return false;
        }
        @heights_ = heights;
        return true;
    }

    void Clear()
    {
        @heights_ = null;
    }

    bool IsLoaded() const
    {
        return heights_ !is null;
    }

    // 表の外と地面の無い所は false
    bool Sample(float x, float z, float &out height) const
    {
        height = 0.0f;
        if (heights_ is null) {
            return false;
        }
        const float fx = (x - originX_) / cell_;
        const float fz = (z - originZ_) / cell_;
        const int i = int(floor(fx));
        const int j = int(floor(fz));
        if (i < 0 || j < 0 || i + 1 >= countX_ || j + 1 >= countZ_) {
            return false;
        }
        const int h00 = heights_[j * countX_ + i];
        const int h01 = heights_[j * countX_ + i + 1];
        const int h10 = heights_[(j + 1) * countX_ + i];
        const int h11 = heights_[(j + 1) * countX_ + i + 1];
        if (h00 == noGround_ || h01 == noGround_ || h10 == noGround_ || h11 == noGround_) {
            return false;
        }
        const float tx = fx - float(i);
        const float tz = fz - float(j);
        const float near = Lerp(float(h00), float(h01), tx);
        const float far = Lerp(float(h10), float(h11), tx);
        height = Lerp(near, far, tz) * 0.01f;
        return true;
    }
}
