using namespace CoreEngine;

// バナナの木の隣のマス（収穫できるマス）に、四角い波を内から外へ繰り返し広げる。
// 出すのは地面・草・水のマスだけで、レールや駅前のレールがあるマスには出さない。
// 波の四隅を画面へ写し、辺を細い UI 画像で描く（3D の線を描く手段がスクリプトに無いための代わり。深度で隠れはしない）
[DisplayName("バナナの木の収穫範囲の波")]
class BananaTreeAura : ScriptComponent
{
    [DisplayName("波を出す")]
    bool auraEnabled = true;

    [DisplayName("色（元の値）")]
    Vector4 color = Vector4(1.0f, 0.2816901f, 0.0f, 1.0f);

    [DisplayName("最大の不透明度"), Range(0, 1)]
    float alpha = 0.8f;

    [DisplayName("1 秒に広がる回数"), Range(0, 5)]
    float speed = 0.75f;

    [DisplayName("1 マスの波の本数"), Range(1, 8)]
    int waveCount = 3;

    [DisplayName("現れるときの一辺（マス）"), Range(0.01, 1)]
    float minSize = 0.16f;

    [DisplayName("消えるときの一辺（マス）"), Range(0.01, 1.5)]
    float maxSize = 0.88f;

    [DisplayName("地面から浮かせる高さ（マス）"), Range(0, 0.5)]
    float heightOffset = 0.025f;

    [DisplayName("線の太さ（px）"), Range(0.5, 6)]
    float lineThickness = 1.5f;

    [DisplayName("描画順"), Range(0, 5000)]
    int sortOrder = 10;

    [DisplayName("木を探す列（カメラの左右・マス）"), Range(4, 64)]
    int searchColumns = 24;

    [DisplayName("画面の外へはみ出しても描く幅（px）"), Range(0, 400)]
    float screenMargin = 80.0f;

    private MapGenerator@ map_;
    private RailPath@ path_;
    private array<UIImage@> lines_;
    private uint usedLines_ = 0;
    private float wavePhase_ = 0.0f;
    private float gridSize_ = 1.0f;

    void Setup(MapGenerator@ map, RailPath@ path, float gridSize)
    {
        @map_ = map;
        @path_ = path;
        gridSize_ = Max(0.01f, gridSize);
    }

    // カーソルが動いた後に呼ぶ（敷いたフレームで消す）
    void Tick(float deltaTime)
    {
        usedLines_ = 0;
        GameCamera@ camera = Scene::GetGameCamera();
        if (auraEnabled && map_ !is null && path_ !is null && camera !is null) {
            wavePhase_ = wavePhase_ + Max(0.0f, deltaTime) * Max(0.0f, speed);
            wavePhase_ -= floor(wavePhase_);
            DrawAura(camera);
        }
        HideUnused();
    }

    void HideAll()
    {
        usedLines_ = 0;
        HideUnused();
    }

    private void DrawAura(GameCamera@ camera)
    {
        const float height = BlockLayout::GetSurfaceHeight(gridSize_) + Max(0.0f, heightOffset) * gridSize_;

        // 木が隣り合っていても同じマスへ二重に描かない。探すのはカメラの周りの列だけで、
        // 画面に映らないマスには描かない（UI の枠を使い切らないように）
        array<int> cells;
        const int sizeX = map_.GetSizeX();
        const int sizeZ = path_.mapSizeZ;
        const int cameraX = int(floor(camera.position.x / gridSize_));
        const int firstX = Max(0, cameraX - searchColumns);
        const int lastX = Min(sizeX - 1, cameraX + searchColumns);
        const array<int> dx = { 1, -1, 0, 0 };
        const array<int> dz = { 0, 0, 1, -1 };
        for (int x = firstX; x <= lastX; ++x) {
            for (int z = 0; z < sizeZ; ++z) {
                if (map_.GetChip(x, z) != MapChip::BananaTree) {
                    continue;
                }
                for (uint d = 0; d < 4; ++d) {
                    const int auraX = x + dx[d];
                    const int auraZ = z + dz[d];
                    if (auraX < 0 || auraZ < 0 || auraX >= sizeX || auraZ >= sizeZ) {
                        continue;
                    }
                    const int chip = map_.GetChip(auraX, auraZ);
                    if (chip != MapChip::Ground && chip != MapChip::Grass && chip != MapChip::Water) {
                        continue;
                    }
                    if (map_.IsStationRailCell(auraX, auraZ) || path_.HasRail(auraX, auraZ)) {
                        continue;
                    }
                    if (!IsOnScreen(camera, Vector3(float(auraX) * gridSize_, height, float(auraZ) * gridSize_))) {
                        continue;
                    }
                    const int key = CellKey(auraX, auraZ);
                    if (cells.find(key) < 0) {
                        cells.insertLast(key);
                    }
                }
            }
        }

        const float minSide = Max(0.01f, minSize) * gridSize_;
        const float maxSide = Max(minSide, maxSize * gridSize_);
        const int waves = Clamp(waveCount, 1, 8);
        const Vector4 screenColor = OriginalHudColor(color);
        const float maxAlpha = Clamp(alpha * color.w, 0.0f, 1.0f);
        for (uint i = 0; i < cells.length(); ++i) {
            const float centerX = float(CellX(cells[i])) * gridSize_;
            const float centerZ = float(CellZ(cells[i])) * gridSize_;
            for (int wave = 0; wave < waves; ++wave) {
                float progress = wavePhase_ + float(wave) / float(waves);
                progress -= floor(progress);
                // すっと広がり、外へ近づくほど減速する
                const float eased = 1.0f - (1.0f - progress) * (1.0f - progress);
                const float half = (minSide + (maxSide - minSide) * eased) * 0.5f;
                const float waveAlpha = maxAlpha * sin(progress * kPi);
                const Vector4 lineColor = Vector4(screenColor.x, screenColor.y, screenColor.z, waveAlpha);
                const Vector3 a = camera.WorldToScreenPoint(Vector3(centerX - half, height, centerZ - half));
                const Vector3 b = camera.WorldToScreenPoint(Vector3(centerX + half, height, centerZ - half));
                const Vector3 c = camera.WorldToScreenPoint(Vector3(centerX + half, height, centerZ + half));
                const Vector3 d = camera.WorldToScreenPoint(Vector3(centerX - half, height, centerZ + half));
                DrawScreenLine(a, b, lineColor);
                DrawScreenLine(b, c, lineColor);
                DrawScreenLine(c, d, lineColor);
                DrawScreenLine(d, a, lineColor);
            }
        }
    }

    // マスの中心が画面（余白を含む）に映るか
    private bool IsOnScreen(GameCamera@ camera, const Vector3 &in world) const
    {
        const Vector3 screen = camera.WorldToScreenPoint(world);
        return screen.z > 1.0e-5f && screen.x >= -screenMargin && screen.x <= 1920.0f + screenMargin
            && screen.y >= -screenMargin && screen.y <= 1080.0f + screenMargin;
    }

    // 画面上の 2 点を結ぶ細い帯を 1 枚置く（カメラの後ろの点は描かない）
    private void DrawScreenLine(const Vector3 &in from, const Vector3 &in to, const Vector4 &in lineColor)
    {
        if (from.z <= 1.0e-5f || to.z <= 1.0e-5f) {
            return;
        }
        const float dx = to.x - from.x;
        const float dy = to.y - from.y;
        const float length = sqrt(dx * dx + dy * dy);
        if (length < 0.5f) {
            return;
        }
        if (usedLines_ == lines_.length()) {
            UIImage@ created = owner.SpawnUIImage("Application/Assets/Textures/UI/dim.png", "BananaTreeAuraLine_" + lines_.length());
            created.anchor = UIAnchor::TopLeft;
            created.pivot = Vector2(0.5f, 0.5f);
            created.sortOrder = sortOrder;
            lines_.insertLast(created);
        }
        UIImage@ line = lines_[usedLines_];
        ++usedLines_;
        if (!line.gameObject.active) {
            line.gameObject.active = true;
        }
        line.anchoredPosition = Vector2((from.x + to.x) * 0.5f, (from.y + to.y) * 0.5f);
        line.size = Vector2(length, lineThickness);
        line.rotation = atan2(dy, dx);
        line.color = lineColor;
    }

    private void HideUnused()
    {
        for (uint i = usedLines_; i < lines_.length(); ++i) {
            if (lines_[i].gameObject.active) {
                lines_[i].gameObject.active = false;
            }
        }
    }
}
