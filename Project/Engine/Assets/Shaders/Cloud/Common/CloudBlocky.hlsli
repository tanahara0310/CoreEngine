/// @file CloudBlocky.hlsli
/// @brief ブロック状（マイクラ / Unrailed 風）の雲スタイルの量子化
/// @details 密度そのものは計算しない。CloudCommon.hlsli の密度関数に対して
///          「サンプル座標をどう量子化するか」「密度をどう二値化するか」だけを与える。
///          Realistic では両方とも恒等変換になり、リアル雲の数式は 1 行も変わらない。
/// @note 密度関数は雲シャドウマップ・スカイキューブマップとも共有しているので、
///       ここでの量子化は影と環境反射にも自動で反映される。

#ifndef CLOUD_BLOCKY_HLSLI
#define CLOUD_BLOCKY_HLSLI

// CloudStyle（C++ 側 CloudSettings.h の enum と番号を一致させること）
#define CLOUD_STYLE_REALISTIC 0u
#define CLOUD_STYLE_VOXEL     1u
#define CLOUD_STYLE_MINECRAFT 2u
#define CLOUD_STYLE_TERRACED  3u

/// 縦のボクセルが層厚に対して大きすぎると雲層が 1 段も入らず雲が丸ごと消える。
/// 最低 2 段は入るように上限を掛ける
static const float kCloudMinVoxelSlabs = 2.0f;

// @note しきい値に遷移帯を設けてポップを緩和しようとしないこと。密度は coverage の
//       下側（0〜globalCoverage）に密集しているため、しきい値まわりの狭い帯でも
//       大量のセルが入り、雲の広い範囲が半透明になってボクセルらしさが失われる。
//       ポップは「セルの中身を時間不変にする」（下の advectMaps）で断つこと。

/// @brief スタイルごとに量子化したサンプル座標と、密度の組み立て方の指示
struct CloudQuantized
{
    float3 worldPos;        ///< 量子化後のワールド座標
    float  heightFraction;  ///< 量子化後の層内高さ率 [0,1]
    bool   useBaseNoise;    ///< false なら 3D ベースノイズを引かず、雲量だけで平面形状を決める
    bool   flatProfile;     ///< true なら高度勾配を使わず層全体を一定厚みの箱にする
    bool   advectMaps;      ///< true なら天候マップと配置ペイントを移流後の座標で引く

    /// ノイズのミップ段を決める間隔 [m]。0 でマーチのステップ幅（既定の挙動）
    /// @details ブロック雲でステップ幅からミップを選んではいけない。ステップ幅は視点距離で
    ///          伸びる（CloudTuning.hlsli の kCloudStride*）ので、同じボクセルでもカメラが
    ///          動くとミップが変わる。密度が連続値なら少しボケるだけだが、二値化していると
    ///          しきい値をまたいだブロックが丸ごと点いたり消えたりする。
    ///          さらに雲探索の粗ステップ（dtBig = dtFine * 4）と本サンプル（dtFine）で
    ///          ミップが 2 段ずれるため、「探索では雲なしと判定して素通りしたが、
    ///          本サンプルなら雲あり」という食い違いも起き、雲塊が丸ごと消える。
    ///          ブロック雲はボクセルの大きさ自体が帯域制限なので、そこから決めて
    ///          視点距離に依存させない。
    float  lodSpacingM;

    /// 天候マップを箱平均するときの半径 [m]。0 で 1 点サンプル（既定の挙動）
    /// @details 天候マップは 512 テクセルを weatherMapScaleM に張るのでテクセルは百数十 m。
    ///          それをボクセル間隔で 1 点だけ拾うとエイリアスし、しきい値ぎりぎりの
    ///          孤立ボクセルが空に散る。ベースノイズはミップで帯域制限できるが、
    ///          天候マップはミップを持たない（CloudResources::CreateNoiseTextures）ので
    ///          タップを散らして平均する。
    float  weatherFilterRadiusM;
};

/// @brief ボクセル 1 個の寸法（x,z = 水平、y = 縦）[m]
/// @details 量子化（CloudQuantizeSample）と DDA（CloudVoxelMarch.hlsli）で
///          同じ格子を切るための単一情報源。片方だけ変えると格子がずれて面が割れる。
float3 CloudBlockyCellSize(CloudConstants c)
{
    float cellXZ = max(c.voxelSizeM, 1.0f);
    float cellY = (c.voxelHeightSizeM > 0.0f) ? c.voxelHeightSizeM : cellXZ;
    cellY = min(cellY, max(c.layerThicknessM / kCloudMinVoxelSlabs, 1.0f));
    return float3(cellXZ, cellY, cellXZ);
}

/// @brief 座標を格子の中心へスナップする
float3 CloudSnapToCell(float3 pos, float3 cell)
{
    return (floor(pos / cell) + 0.5f) * cell;
}

/// @brief サンプル座標をスタイルに応じて量子化する
/// @param worldPos 量子化前のワールド座標
/// @param h 量子化前の層内高さ率
/// @details 格子は「風とともに動く座標系」で切る。ワールド固定の格子で切ると
///          ブロックの位置が動かないまま中身の密度だけが連続的に変わるため、
///          雲塊が流れずにその場で明滅する。移動する枠で切って world へ戻すことで、
///          ブロックが剛体のまま風下へスライドする。
/// @note 高度スキュー（heightSkewM）は格子には掛けない。高さごとに横へずらす剪断なので、
///       格子へ掛けると立方体が平行四辺形に崩れる。
float3 CloudBlockyWindOffset(CloudConstants c)
{
    return float3(c.windDirX, 0.0f, c.windDirZ) * (c.windSpeedMPerS * c.timeSec);
}

CloudQuantized CloudQuantizeSample(float3 worldPos, float h, CloudConstants c)
{
    CloudQuantized q;
    q.worldPos = worldPos;
    q.heightFraction = h;
    q.useBaseNoise = true;
    q.flatProfile = false;
    q.advectMaps = false;
    q.lodSpacingM = 0.0f;
    q.weatherFilterRadiusM = 0.0f;

    if (c.styleIndex == CLOUD_STYLE_REALISTIC)
    {
        return q;   // 恒等変換。リアル雲はここで抜ける
    }

    // 密度の情報源をすべて「風とともに動く座標系」で引く。1 つでもワールド座標で引くと、
    // ボクセル格子だけが風で動くのに参照先が静止しているため、セルがその場を滑っていき、
    // 引いてくる値が刻々と変わる。密度は二値化されているので、しきい値をまたいだ瞬間に
    // ブロック 1 個が 1 フレームで丸ごと湧く／消える（＝ブロックの明滅）。
    // 移動枠に揃えればセルの中身が時間不変になり、雲の場は剛体のままスライドして
    // ポップが原理的に起きなくなる。
    // リアル雲が「静止した天候系の中をディテールが流れる」モデルなのに対し、
    // ブロック雲は「雲の塊がそのまま流れる」モデルになる（マイクラの雲と同じ挙動）。
    q.advectMaps = true;

    const float3 cellSize = CloudBlockyCellSize(c);
    const float cellXZ = cellSize.x;
    const float cellY = cellSize.y;

    // ミップは粗いほうのセルへ合わせる（1 ボクセルより細かい構造は表現できないため）
    q.lodSpacingM = max(cellXZ, cellY);
    // 天候マップはボクセル 1 個分を平均する（4 タップの重心が ±cell/4）
    q.weatherFilterRadiusM = cellXZ * 0.25f;

    const float3 windOffset = CloudBlockyWindOffset(c);
    float3 p = worldPos + windOffset;

    if (c.styleIndex == CLOUD_STYLE_MINECRAFT)
    {
        // XZ だけ量子化し、縦は層全体を一定厚みの板にする。
        // ベースノイズを引かないので 3D テクスチャのフェッチが丸ごと消え、リアル雲より軽い
        p.xz = (floor(p.xz / cellXZ) + 0.5f) * cellXZ;
        q.worldPos = p - windOffset;
        q.useBaseNoise = false;
        q.flatProfile = true;
        return q;
    }

    if (c.styleIndex == CLOUD_STYLE_TERRACED)
    {
        // 縦だけ量子化。輪郭は有機的なまま高さが段々になる。
        // 風は水平なので縦の格子はワールド固定でよく、worldPos.y をそのまま切る
        q.worldPos.y = (floor(worldPos.y / cellY) + 0.5f) * cellY;
        q.heightFraction = CloudHeightFraction(q.worldPos, c);
        return q;
    }

    // CLOUD_STYLE_VOXEL: 3 軸すべて量子化する
    p = CloudSnapToCell(p, cellSize);
    q.worldPos = p - windOffset;
    q.heightFraction = CloudHeightFraction(q.worldPos, c);
    return q;
}

/// @brief 密度をスタイルに応じて二値化する
/// @details ブロック雲は縁が柔らかいと立方体の面が出ないので、しきい値で 0/1 に落とす。
///          Realistic では素通し。
/// @note 完全な階段関数にすること。ブロックは在るか無いかのどちらかで、半透明のブロックは
///       ボクセル雲では成立しない。しきい値をまたいだ瞬間のポップは、遷移帯でごまかすのではなく
///       セルの中身を時間不変にして（advectMaps）そもそも起きないようにする。
float CloudApplyStyleThreshold(float density, CloudConstants c)
{
    if (c.styleIndex == CLOUD_STYLE_REALISTIC)
    {
        return density;
    }
    return (density > c.densityThreshold) ? 1.0f : 0.0f;
}

#endif // CLOUD_BLOCKY_HLSLI
