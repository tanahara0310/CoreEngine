// ============================================================
// DXR 水面コースティクスシェーダー
// 第2段階ではまず受光点ごとの簡易集光量を専用RT出力へ書き出す。
// ============================================================

#include "RTWaterSurfaceCommon.hlsli"
#include "../../Include/Common/DepthReconstruction.hlsli"
#include "ColorSpace.hlsli" // Luminance

RWTexture2D<float4> gCausticsOutput : register(u0);
RaytracingAccelerationStructure gScene : register(t0);
Texture2D<float> gSceneDepth : register(t1); // WorldPosition ターゲット廃止に伴い深度から復元する
Texture2D<float4> gNormalRoughness : register(t2);
Texture2DArray<float4> gFFTOceanDisplacement : register(t3);
Texture2DArray<float4> gFFTOceanNormal : register(t4);

cbuffer WaterCausticsConstants : register(b0)
{
    float gMaxTraceDistance;
    float gSurfaceBias;
    float gIntensityScale;
    // 旧 gWaterHeight。水面高さは b1 の gSurfaceWaterHeight に一本化され未使用。
    // スロットを消すと後続の float3 が 16B 境界をまたいで C++ 側とずれるため
    // パディングとして残す（RTシャドウの cbuffer 配列ずれ事故と同型の罠）。
    float gPadding0;
    float3 gLightDirection;
    float gScreenWidth;
    float gScreenHeight;
    // 旧 FFT 有効情報 3 スロット。実体は b1（RTWaterSurfaceCommon.hlsli）へ一本化済み。
    uint gFFTOceanPad1;
    float gFFTOceanPad0;
    uint gFFTOceanPad2;
    float gRefractiveIndex;
    float gDebugDisplayScale;
    uint gDebugViewMode;
    // シーンの実際のディレクショナルライトが無効な場合はコースティクスも出さない。
    // 以前はここが未使用の padding で、ライトを消してもコースティクスが消えなかった。
    uint gLightEnabled;
    float3 gLightColor;
    float gLightIntensity;
    // 水面メッシュのワールドXZ範囲（WaterCausticsConstants と一致させること）。
    // コースティクスは解析的な無限水面として評価されるため、この矩形でマスクしないと
    // 水域の外（無限床など「水面高さより低い場所すべて」）にも集光模様が漏れる。
    float2 gRegionCenterXZ;
    float2 gRegionHalfExtentXZ;
    uint gRegionValid;
    // 波長依存の吸収係数 σa [1/m]（RGB）。水面描画（WaterFrameConstants.absorptionCoeff、
    // Jerlov プリセット/濁度 UI）と同じ値が毎フレーム同期される。赤 > 緑 > 青。
    float3 gAbsorptionCoeff;
    float4x4 gInvViewProj; // WorldPosition ターゲット廃止に伴う深度復元用
};

static const uint kRTCausticsDebugNone = 0;
static const uint kRTCausticsDebugShallowFade = 1;
static const uint kRTCausticsDebugMatchFactor = 2;
static const uint kRTCausticsDebugAttenuation = 3;
static const uint kRTCausticsDebugReceiverFacing = 4;
static const uint kRTCausticsDebugFinalIntensity = 5;
static const uint kRTCausticsDebugConcentration = 6;

// ペイロード（RTWaterPayload）・失敗理由コード・波面評価は RTWaterSurfaceCommon.hlsli（共通）。

#ifdef __INTELLISENSE__
void TraceRay(
    RaytracingAccelerationStructure scene,
    uint rayFlags,
    uint instanceInclusionMask,
    uint rayContributionToHitGroupIndex,
    uint multiplierForGeometryContributionToHitGroupIndex,
    uint missShaderIndex,
    RayDesc ray,
    inout RTWaterPayload payload);
#endif

float3 VisualizeScalar(float value)
{
    return VisualizeRTScalar(value, gDebugDisplayScale);
}

float3 BuildCausticsDebugColor(
    uint debugViewMode,
    float shallowFade,
    float matchFactor,
    float transmittanceLuma, // Beer–Lambert 透過率の輝度（旧 attenuation スロット）
    float receiverFacingFactor,
    float intensity,
    float concentration)
{
    if (debugViewMode == kRTCausticsDebugConcentration)
    {
        return VisualizeScalar(concentration);
    }

    if (debugViewMode == kRTCausticsDebugShallowFade)
    {
        return VisualizeScalar(shallowFade);
    }

    if (debugViewMode == kRTCausticsDebugMatchFactor)
    {
        return VisualizeScalar(matchFactor);
    }

    if (debugViewMode == kRTCausticsDebugAttenuation)
    {
        return VisualizeScalar(transmittanceLuma);
    }

    if (debugViewMode == kRTCausticsDebugReceiverFacing)
    {
        return VisualizeScalar(receiverFacingFactor);
    }

    if (debugViewMode == kRTCausticsDebugFinalIntensity)
    {
        return VisualizeScalar(intensity);
    }

    return 0.0f.xxx;
}

// UseFFTOceanSurface / 波面評価は RTWaterSurfaceCommon.hlsli の共通実装
// （b1 の gSurfaceFFTOceanEnabled / gSurfaceFFTOceanResolution で判定）を使う。

float3 EvaluateCausticsWaterOffset(float2 worldXZ)
{
    return EvaluateWaterOffset(gFFTOceanDisplacement, worldXZ);
}

// ★水中判定（coverage）は「実際にラスタライザが描く面」そのものを評価する★（2026-07-27 第2ラウンド）
//
// 【「水面の外なのに水中判定される」のからくり】
// 水中判定は「受光点の真上に水面があるか」を*解析的な波面*（変位テクスチャのピクセル単位評価）で
// 判定していた。ところが実際に描かれる水面は**頂点グリッドの三角形メッシュ**で、頂点間は線形補間
// される。つまり比較している 2 つの面は別物:
//   ・解析面 : ワールドXZの連続関数（波長 127m / 521m を正しく再現）
//   ・描画面 : 頂点間隔ごとの折れ線（この scene では 頂点間隔 = 100 × scale1000 / 256 = **390m**）
// 390m 間隔ではカスケードのパッチ長（521m/127m）は完全にエイリアスするため、描画面は解析面から
// 波高そのもの（数十cm〜m）ずれる。海底が緩勾配の浜では「高さのズレ ÷ 勾配」で水平方向に増幅され、
//   解析面は海底より上（＝水中判定 ON）／描画面は海底より下（＝水面が描かれない）
// という領域が数m〜数十m の帯として生じる。これが「水面が無いのに水中ライティングだけ乗った、
// のっぺりした層」の正体。逆向きのズレでは「水面はあるのに濡れ暗色化されない明るい帯」になる。
//
// 【対策】判定側を描画面に合わせる。頂点グリッドの格子点（＝実際のメッシュ頂点）でのみ変位を評価し、
// ラスタライザと同じ三角形補間で高さを求める。こうすると 2 つの面が定義上一致するので、
// メッシュがどれだけ粗くても「水面の外なのに水中判定」は原理的に発生しない。
// （メッシュが粗いこと自体の品質問題＝汀線が波ではなく巨大な三角形の形に従う、は別途 LOD で解く）
//
// グリッドは PlaneMeshGenerator の生成規則そのまま:
//   頂点 local = (-size/2 + x*step, 0, -size/2 + z*step),  world = local * scale + translate
//   → 原点 = gRegionCenterXZ - gRegionHalfExtentXZ、セル = 2*gRegionHalfExtentXZ / 分割数
// 三角形分割は (topLeft, bottomLeft, topRight) / (topRight, bottomLeft, bottomRight)。
// 分割数は b1 の gSurfaceMeshSubdivisions（WaterRenderFeature が実際のメッシュ解像度を
// 毎フレーム供給。以前はここに 256 がハードコードされ、シーン側の変更で静かに壊れた）。

/// @brief 水面メッシュ頂点 1 点分の変位（FFTWater.VS と同一式: 先頭2カスケード＋波群エンベロープ）
float3 SampleMeshVertexDisplacement(float2 baseXZ)
{
    float3 disp = 0.0f.xxx;
    [unroll]
    // 頂点変位に使うカスケード数は FFTWater.VS と共有（Common/FFTOceanCascade.hlsli）
    for (int c = 0; c < kFFTGeometryCascadeCount; ++c)
    {
        const float2 gridXZ = RotateToFFTCascadeGrid(baseXZ, c);
        const float3 d = SampleFFTOceanArraySlice(
            gFFTOceanDisplacement, gridXZ, kFFTCascadePatch[c], (uint)c, gSurfaceFFTOceanResolution).xyz;
        const float2 horizontal = RotateFromFFTCascadeGrid(float2(d.x, d.z), c);
        disp += float3(horizontal.x, d.y, horizontal.y);
    }
    return disp * ComputeFFTWaveGroupEnvelope(baseXZ);
}

/// @brief ラスタライザが実際に描く水面の高さ（基準面からのオフセット）を返す
float EvaluateDrawnSurfaceHeight(float2 worldXZ)
{
    if (!UseFFTOceanSurface())
    {
        return EvaluateWaterOffsetGerstner(worldXZ).y;
    }
    if (gRegionValid == 0)
    {
        // グリッドが不明なときは従来どおり解析面で代用する（帯が出うるが破綻はしない）
        return SampleMeshVertexDisplacement(worldXZ).y;
    }

    // choppiness の水平変位の逆写像（1 回だけの近似）。頂点間隔に対して水平変位は小さいので
    // これで十分収束する（間隔 390m なら補正は 0.5% 未満、間隔数 m でも 1 回で誤差は無視できる）。
    const float2 baseXZ = worldXZ - SampleMeshVertexDisplacement(worldXZ).xz;

    const float2 gridOrigin = gRegionCenterXZ - gRegionHalfExtentXZ;
    const float2 cellSize = max((2.0f * gRegionHalfExtentXZ) / max(gSurfaceMeshSubdivisions, 1.0f), 1.0e-4f.xx);
    const float2 gridCoord = (baseXZ - gridOrigin) / cellSize;
    const float2 cellIndex = floor(gridCoord);
    const float2 cellFrac = gridCoord - cellIndex;
    const float2 cellCorner = gridOrigin + cellIndex * cellSize;

    // ラスタライザと同じ三角形補間（対角は bottomLeft(0,1)-topRight(1,0)）。
    // 対角上の 2 頂点は両方の三角形で共通なので、評価するのは 3 頂点だけでよい。
    const float h10 = SampleMeshVertexDisplacement(cellCorner + float2(cellSize.x, 0.0f)).y;
    const float h01 = SampleMeshVertexDisplacement(cellCorner + float2(0.0f, cellSize.y)).y;
    if (cellFrac.x + cellFrac.y <= 1.0f)
    {
        const float h00 = SampleMeshVertexDisplacement(cellCorner).y;
        return h00 + cellFrac.x * (h10 - h00) + cellFrac.y * (h01 - h00);
    }
    const float h11 = SampleMeshVertexDisplacement(cellCorner + cellSize).y;
    return h11 + (1.0f - cellFrac.x) * (h01 - h11) + (1.0f - cellFrac.y) * (h10 - h11);
}

float3 EvaluateCausticsWaterNormal(float2 worldXZ)
{
    return EvaluateWaterNormal(gFFTOceanNormal, worldXZ);
}

/// @brief Schlick 近似のフレネル透過率 (1 - F) を返す
/// @param cosIncident 入射角の余弦（dot(-光の進行方向, 水面法線)）
float FresnelTransmittanceSchlick(float cosIncident)
{
    const float f0 = ((gRefractiveIndex - 1.0f) / (gRefractiveIndex + 1.0f))
                   * ((gRefractiveIndex - 1.0f) / (gRefractiveIndex + 1.0f));
    const float oneMinusCos = 1.0f - saturate(cosIncident);
    const float oneMinusCos2 = oneMinusCos * oneMinusCos;
    return 1.0f - (f0 + (1.0f - f0) * oneMinusCos2 * oneMinusCos2 * oneMinusCos);
}

// ★水面線での明るさの不連続（岸に張り付く明るい帯）の対策★（2026-07-27）
// 受光面の幾何項は「水平面照度 → 受光面照度」の換算で、屈折後の光線方向 r を使う:
//     G_refract = dot(N, -r) / (-r.y)
// 一方その隣（乾いた砂）は通常の直接光 dot(N, -L) で照らされる。r は屈折で鉛直側へ
// 立つため、太陽が低いほど、また斜面が太陽と反対を向くほど、この 2 つは大きく食い違う
// （実測: 水面線をまたいだ瞬間に約 +1EV = 2 倍明るくなる）。水深 0 でも成立してしまう
// 段差なので、汀線に沿った「明るく平坦な帯」として必ず見える。
//
// 水深 0 の極限では水は膜でしかなく、光の角度分布はまだ空中と同じであるべき
// （屈折後の光束が「別方向から来る光」として意味を持つのは、水柱が光路として
//   成立する深さから）。そこで幾何項を
//     G(0)   = dot(N, -L) / (-L.y)     ← 空中と同じ角度応答（水面線で連続）
//     G(深)  = dot(N, -r) / (-r.y)     ← 屈折後の正しい幾何
// の間で水深によりブレンドする。これで水深 0 での比が
//     cosZ × T_fresnel × G(0) / dot(N,-L) = T_fresnel ≈ 0.95
// となり、水面線をまたぐ明るさが連続する（残る 5% はフレネル反射の分で物理的に正しい）。
static const float kGeometryBlendDepthMeters = 0.6f;

/// @brief 空中（屈折なし）と同じ角度応答の幾何項
float ComputeAerialGeometryFactor(float3 receiverNormal, float3 lightDir)
{
    return saturate(dot(receiverNormal, -lightDir)) / max(-lightDir.y, 0.05f);
}

/// @brief 屈折後の幾何項をどれだけ採用するか（水深 0 で 0、kGeometryBlendDepthMeters で 1）
float ComputeRefractedGeometryWeight(float submergedDepth)
{
    return smoothstep(0.0f, kGeometryBlendDepthMeters, submergedDepth);
}

/// @brief 水面上の点（XZ）へ入射した太陽光を屈折させ、床平面 y=floorY への着地点XZを返す
/// @details 集光率（ヤコビアン）の有限差分評価用。入射点探索と同じ波面評価・屈折計算を使うこと。
/// @return 屈折が有効（全反射・上向きでない）なら true
bool ProjectRefractedToFloor(float2 surfaceXZ, float floorY, float3 lightDir, float eta, out float2 landingXZ)
{
    const float surfaceY = gSurfaceWaterHeight + EvaluateCausticsWaterOffset(surfaceXZ).y;
    const float3 surfaceNormal = EvaluateCausticsWaterNormal(surfaceXZ);
    float3 refracted = refract(lightDir, surfaceNormal, eta);
    if (dot(refracted, refracted) <= 1.0e-6f || refracted.y >= -1.0e-4f)
    {
        landingXZ = surfaceXZ;
        return false;
    }
    refracted = normalize(refracted);
    const float travel = (surfaceY - floorY) / max(-refracted.y, 1.0e-4f);
    landingXZ = surfaceXZ + refracted.xz * travel;
    return true;
}

[shader("raygeneration")]
void RTWaterCausticsRayGen()
{
    uint2 launchIndex = DispatchRaysIndex().xy;

    // ライトが無効（消灯）な場合はコースティクスも出さない。
    // 非RT版 WaterCaustics.PS.hlsl の gMainLightEnabled チェックと同じ扱い。
    if (gLightEnabled == 0)
    {
        gCausticsOutput[launchIndex] = 0.0f.xxxx;
        return;
    }

    float ndcDepth = gSceneDepth.Load(int3(launchIndex, 0));
    if (IsBackgroundDepth(ndcDepth))
    {
        gCausticsOutput[launchIndex] = 0.0f.xxxx;
        return;
    }

    float2 screenUV = (float2(launchIndex) + 0.5f.xx) / float2(gScreenWidth, gScreenHeight);
    float3 receiverWorldPos = ReconstructWorldPosition(ScreenUVToNDC(screenUV), ndcDepth, gInvViewProj);

    // 水面メッシュの XZ 範囲外（無限床など水域の外）にはコースティクスを落とさない。
    // 範囲の内側 2m でフェードアウトさせ、水域の縁で模様が急に切れる輪郭を防ぐ。
    float regionFade = 1.0f;
    if (gRegionValid != 0)
    {
        float2 regionDelta = abs(receiverWorldPos.xz - gRegionCenterXZ) - gRegionHalfExtentXZ;
        float outsideDistance = max(regionDelta.x, regionDelta.y);
        const float kRegionEdgeFadeMeters = 2.0f;
        regionFade = saturate(-outsideDistance / kRegionEdgeFadeMeters);
        if (regionFade <= 0.0f)
        {
            gCausticsOutput[launchIndex] = 0.0f.xxxx;
            return;
        }
    }

    float3 receiverNormal = normalize(gNormalRoughness.Load(int3(launchIndex, 0)).xyz * 2.0f - 1.0f);

    // ===== 水中判定は「波込みの実水面高」で行い、結果を α で DeferredLighting へ運ぶ =====
    // ★汀線に平行な「透明っぽい薄い層」の真因と恒久対策（2026-07-27）★
    // 旧実装は平坦な基準水面高で判定していた（DeferredLighting が波を評価できないため、
    // 両者の基準を平坦側に揃えるしかなかった）。しかし実際の水面メッシュ＝Water.PS の
    // 着色開始線は波変位後の汀線なので、平坦等高線との間に
    //   「水は被っているのに直接光もコースティクスも通常のまま」の帯
    // が生じる。幅は 波振幅 ÷ 海底勾配 で、遠浅では数 m に広がる。両境界とも岸に
    // 平行な曲線なので、本物の波線と同じ形の薄い層が二重に見える（実害の正体）。
    //
    // 対策: 本シェーダーは全画面ピクセルで波を評価できるので、波込みの被覆率
    //   coverage = saturate(波込み水深 / 0.05) × regionFade
    // を出力 α に書き、DeferredLighting は自前の平坦判定をやめて α をそのまま
    // underwaterFactor に使う。基準がピクセル単位で完全一致するため、
    // 白い二重照明も黒い照明喪失も帯も構造的に生じない
    // （2026-07-26 の白/黒帯回帰は「片側だけ波基準にした」ことが原因で、
    //   両側を同じ波基準にする本方式では起きない）。
    const float surfaceYAboveReceiver =
        gSurfaceWaterHeight + EvaluateDrawnSurfaceHeight(receiverWorldPos.xz);
    float submergedDepth = surfaceYAboveReceiver - receiverWorldPos.y;
    if (submergedDepth <= 0.0f)
    {
        // 波の水面より上 = 水に覆われていない。直接光は通常のまま（α=0）
        gCausticsOutput[launchIndex] = 0.0f.xxxx;
        return;
    }

    // 直接光置換とのクロスフェード（波込み水深基準）。DeferredLighting は α=coverage を
    // そのまま置換率に使うため、合計が常に「直接光1つ分」になり汀線が連続につながる。
    const float crossFade = saturate(submergedDepth / 0.05f);
    const float coverage = crossFade * regionFade;

    // ★浅瀬の「模様フェード」は存在してはいけない★（2026-07-27 撤去）
    // 旧実装（saturate((d-0.03)/0.12)² → smoothstep(0.05,0.8)）は「ごく浅い水では
    // 模様がまだ形成されない」という仮定で集光率を 1 へ潰していたが、これが
    // 「模様の無い均一な帯」を作り、その境界が水深等高線＝汀線と平行な曲線として
    // 走るため、本物の波線と同じ形の薄い層が二重に見える元凶だった。
    // フェード幅を弄っても帯の位置が動くだけで消えない（実測で確認済みの対症療法）。
    //
    // 物理的には人工フェードは不要: ヤコビアン集光率は水深 → 0 で入射点写像が
    // 恒等写像に収束し |det J| → 1（等倍）へ自動的に連続化する。浅瀬で模様が
    // 淡くなる挙動は集光率の計算そのものが正しく再現する。

    const float eta = 1.0f / max(gRefractiveIndex, 1.001f);
    // refract() の入射ベクトルは「光の進行方向」（=下向きの lightDir）を渡す。
    // 以前は -lightDir を渡しており、屈折方向の水平成分が反転して
    // コースティクスが本来と逆向き・不正な位置に集光していた。
    // （非RT 版 WaterCaustics.PS.hlsl も incidentDir = gMainLightDirection をそのまま使用している）
    float3 lightDir = normalize(gLightDirection);

    // ===== フォールバック基準光（平坦水面の透過直接光・集光なし） =====
    // 置換モードでは本出力が「水中の直接光の全量」なので、入射点探索の失敗や
    // ヒット不整合で 0 を返すと床がそのまま真っ黒に抜ける。光は必ずどこかへ
    // 届いているため、判定不能時は平坦水面近似の透過光を返す。
    // 0 を返してよいのは「遮蔽が確定した場合」（＝本物の影）だけ。
    float3 baselineRadiance = 0.0f.xxx;
    {
        const float3 flatNormal = float3(0.0f, 1.0f, 0.0f);
        float3 flatRefracted = refract(lightDir, flatNormal, eta);
        if (dot(flatRefracted, flatRefracted) > 1.0e-6f && flatRefracted.y < -1.0e-4f)
        {
            flatRefracted = normalize(flatRefracted);
            const float flatFresnelT = FresnelTransmittanceSchlick(-lightDir.y);
            // 光路長は波込みの実水深基準（早期リターン済みなので常に正）
            const float flatPath = submergedDepth / max(-flatRefracted.y, 1.0e-4f);
            const float3 flatTransmittance = exp(-gAbsorptionCoeff * flatPath);
            float flatGeometry = saturate(dot(receiverNormal, -flatRefracted)) / max(-flatRefracted.y, 0.05f);
            flatGeometry = min(lerp(
                ComputeAerialGeometryFactor(receiverNormal, lightDir),
                flatGeometry,
                ComputeRefractedGeometryWeight(submergedDepth)), 4.0f);
            const float flatIlluminance = gLightIntensity * saturate(-lightDir.y);
            baselineRadiance = gLightColor
                * (gIntensityScale * flatIlluminance * flatFresnelT * flatGeometry * coverage)
                * flatTransmittance;
        }
    }

    // 波の谷が受光点より下がった瞬間（実水深 ≤ 0）は入射点探索が定義できない。
    // DeferredLighting は既に直接光を消しているため、ここで 0 を返すと波形の
    // 黒い照明喪失になる。露出した濡れ砂に相当する明るさ＝基準透過光で埋める。
    if (submergedDepth <= 1.0e-3f)
    {
        gCausticsOutput[launchIndex] = float4(baselineRadiance, coverage);
        return;
    }

    // ===== 入射点（この受光点へ屈折光を届ける水面上の点）を反復で解く =====
    // 入射点を受光点の真上に固定してはいけない。太陽が傾くほど実際の入射点は
    //   水平ずれ = 水深 × tan(水中屈折角)
    // だけ風上側へ移動する（高度27°の太陽なら水深の約1.15倍）。真上に固定したままだと
    // 後段の receiverMatchRadius（既定 0.15×水深）に全ピクセルが弾かれ、
    // コースティクスが完全に消える。太陽がほぼ天頂のときだけ動く実装だった。
    // ここでは受光点から屈折方向を逆にたどって水面へ戻す固定点反復で入射点を求める。
    float3 waterPos = float3(receiverWorldPos.x, surfaceYAboveReceiver, receiverWorldPos.z);
    [unroll]
    for (int iteration = 0; iteration < 3; ++iteration)
    {
        const float3 iterNormal = EvaluateCausticsWaterNormal(waterPos.xz);
        float3 iterRefracted = refract(lightDir, iterNormal, eta);
        if (dot(iterRefracted, iterRefracted) <= 1.0e-6f || iterRefracted.y >= -1.0e-4f)
        {
            // 全反射・上向き屈折。収束不能なのでループを抜け、最終判定で棄却させる
            break;
        }
        iterRefracted = normalize(iterRefracted);

        const float travel = (waterPos.y - receiverWorldPos.y) / max(-iterRefracted.y, 1.0e-4f);
        waterPos.xz = receiverWorldPos.xz - iterRefracted.xz * travel;
        waterPos.y = gSurfaceWaterHeight + EvaluateCausticsWaterOffset(waterPos.xz).y;
    }

    // 収束後の入射点で最終的な法線・屈折方向を評価する
    float3 waterNormal = EvaluateCausticsWaterNormal(waterPos.xz);
    float3 refractedDir = refract(lightDir, waterNormal, eta);
    if (dot(refractedDir, refractedDir) <= 1.0e-6f || refractedDir.y >= -1.0e-4f)
    {
        // 波面法線での全反射・上向き屈折 → 判定不能。基準透過光で埋める
        gCausticsOutput[launchIndex] = float4(baselineRadiance, coverage);
        return;
    }
    refractedDir = normalize(refractedDir);

    RayDesc ray;
    ray.Origin = waterPos - waterNormal * gSurfaceBias;
    ray.Direction = refractedDir;
    ray.TMin = 0.001f;

    float3 rayToReceiver = receiverWorldPos - ray.Origin;
    float receiverRayT = dot(rayToReceiver, ray.Direction);
    if (receiverRayT <= 0.0f)
    {
        // 屈折レイが受光点と逆向き → 探索失敗。基準透過光で埋める
        gCausticsOutput[launchIndex] = float4(baselineRadiance, coverage);
        return;
    }

    float3 projectedReceiverPos = ray.Origin + ray.Direction * receiverRayT;
    float receiverRayDistance = length(projectedReceiverPos - receiverWorldPos);
    // 許容半径は「水深 × 0.30」を基準にしつつ、斜め入射での光路長の伸び
    // （水深の 1/|refractedDir.y| 倍）でスケールさせる。入射点探索の残差も
    // 同じ比率で水平方向へ拡大するため。
    // 係数 0.30: マッチ判定の役割は「収束確認＋遮蔽」の可視性のみになった
    // （模様はヤコビアン集光率が担う）ので、旧 0.15 より緩くてよい。
    // 厳しくすると焦線（写像が多価になり反復が振動する場所＝本来最も明るい場所）
    // で棄却が起き、明るい筋がちょうど黒く抜ける「明暗反転」が再発する。
    const float obliquityScale = 1.0f / max(-refractedDir.y, 0.15f);
    float receiverMatchRadius = max(0.03f, submergedDepth * 0.30f * obliquityScale);
    if (receiverRayDistance > receiverMatchRadius)
    {
        // 反復が受光点へ収束しなかった（焦線近傍で写像が多価など）。
        // 光が届かない証明ではないため、基準透過光で埋める
        gCausticsOutput[launchIndex] = float4(baselineRadiance, coverage);
        return;
    }

    ray.TMax = min(gMaxTraceDistance, receiverRayT + receiverMatchRadius * 2.0f);

    RTWaterPayload payload;
    payload.hitT = 0.0f;
    payload.hitFlag = 0.0f;

    TraceRay(
        gScene,
        0,
        0xFF,
        0,
        1,
        0,
        ray,
        payload);

    if (payload.hitFlag < 0.5f)
    {
        // ミス＝レイ上に遮蔽物なし。受光点ジオメトリを掠めただけなので基準透過光で埋める
        gCausticsOutput[launchIndex] = float4(baselineRadiance, coverage);
        return;
    }

    float hitDistanceError = abs(payload.hitT - receiverRayT);
    float hitMatchRadius = max(0.10f, receiverMatchRadius * 2.0f);
    if (payload.hitT < receiverRayT - hitMatchRadius)
    {
        // 受光点より明確に手前でヒット＝遮蔽が確定（本物の影）。rgb だけは 0 を返してよい。
        // α（水中被覆率）は影でも水中である事実を変えないので coverage のまま伝える
        // （0 にすると DeferredLighting が直接光を復活させ、水中の影が二重照明で消える）。
        gCausticsOutput[launchIndex] = float4(0.0f, 0.0f, 0.0f, coverage);
        return;
    }
    if (payload.hitT > receiverRayT + hitMatchRadius)
    {
        // 受光点を素通りして奥にヒット＝面の食い違い。遮蔽ではないので基準透過光で埋める
        gCausticsOutput[launchIndex] = float4(baselineRadiance, coverage);
        return;
    }

    // マッチ判定は「正しい入射点に収束したか＋遮蔽と整合するか」の可視性重みへ役割変更。
    // 模様（明暗）はヤコビアン集光率が担うため、旧実装の二乗強調は行わない。
    float rayMatchFactor = saturate(1.0f - receiverRayDistance / receiverMatchRadius);
    float hitMatchFactor = saturate(1.0f - hitDistanceError / hitMatchRadius);
    float matchFactor = rayMatchFactor * hitMatchFactor;

    // ===== 集光率（ビーム集中度）: 入射点写像のヤコビアン =====
    // 物理的なコースティクスの明暗は「水面での光束の水平断面積 ÷ 床での照射面積」の比。
    // 入射点の近傍 ±ε で同じ屈折・投影を評価し、写像 F: 水面XZ → 床XZ（受光点高さの
    // 水平面）の面積拡大率 |det J| の逆数を集光率とする。
    //   |det J| < 1 : 光線が収束 → 明るい筋（焦線）
    //   |det J| > 1 : 光線が発散 → 暗い領域
    //   波が平坦    : |det J| = 1 → 等倍（素の透過光）
    // 焦線上では det J → 0 で発散するため上限でクランプする（エネルギーは本来
    // 有限幅の焦線に集中するが、点サンプルでは表現できずホワイトアウトになるだけのため）。
    // ε は最細 FFT カスケード（パッチ23m / 256テクセル ≈ 0.09m）のテクセル幅に合わせる。
    const float kJacobianEps = 0.08f;
    const float kMaxConcentration = 8.0f;
    float concentration = 1.0f;
    {
        const float floorY = receiverWorldPos.y;
        const float t0 = (waterPos.y - floorY) / max(-refractedDir.y, 1.0e-4f);
        const float2 landing0 = waterPos.xz + refractedDir.xz * t0;
        float2 landingX;
        float2 landingZ;
        const bool validX = ProjectRefractedToFloor(
            waterPos.xz + float2(kJacobianEps, 0.0f), floorY, lightDir, eta, landingX);
        const bool validZ = ProjectRefractedToFloor(
            waterPos.xz + float2(0.0f, kJacobianEps), floorY, lightDir, eta, landingZ);
        if (validX && validZ)
        {
            const float2 dFdX = (landingX - landing0) / kJacobianEps;
            const float2 dFdZ = (landingZ - landing0) / kJacobianEps;
            const float detJ = dFdX.x * dFdZ.y - dFdX.y * dFdZ.x;
            concentration = min(1.0f / max(abs(detJ), 1.0e-3f), kMaxConcentration);
        }
        // 近傍が全反射などで無効な場合は等倍（集光なし）のまま
    }

    // ===== 受光面の幾何項 =====
    // 水平面パラメータ化の照度（E_h）を実際の受光面の照度へ換算する:
    //   E_receiver = E_h × dot(N, -r) / (-r.y)
    // 床が水平（N=up）なら 1.0。斜面は屈折光へ正対するほど明るくなる。
    // 旧実装の receiverUpFactor / focus(=dot(N, -lightDir): 水の存在を無視して
    // 太陽を直接受ける計算で屈折後の光束と無関係) はこの項で置き換え。
    float geometryFactor = saturate(dot(receiverNormal, -refractedDir)) / max(-refractedDir.y, 0.05f);
    geometryFactor = min(lerp(
        ComputeAerialGeometryFactor(receiverNormal, lightDir),
        geometryFactor,
        ComputeRefractedGeometryWeight(submergedDepth)), 4.0f);

    // 浅瀬の減衰は入れない（上記コメント参照。|det J| → 1 が自然な浅瀬極限）

    // ===== フレネル透過率 =====
    // 入射光の一部は水面で反射されて水中へ入らない。透過分 T = 1 - F(θi) を掛ける。
    // Schlick 近似。F0 は屈折率から導出（n=1.333 → F0 ≈ 0.02）。
    // 太陽が低いほど（入射角が大きいほど）反射が増え、コースティクスは自然に弱まる。
    const float fresnelTransmittance = FresnelTransmittanceSchlick(dot(-lightDir, waterNormal));

    // ===== Beer–Lambert 吸収（波長依存） =====
    // 水面描画と同じ σa [1/m] を、入射点から受光点までの実光路長（receiverRayT）に適用する。
    // 旧 exp(-0.16 × 鉛直水深) はグレースケールかつ経路長非依存で、水色システムと
    // 食い違っていたため置き換え。赤から先に吸収され、深いほど青緑へ転ぶ。
    const float3 transmittance = exp(-gAbsorptionCoeff * receiverRayT);

    // 水平な水面が受け取る太陽照度 E_h = ライト強度 × cos(天頂角)。
    // ここに集光率を掛けたものが床の水平面照度になる（|det J|=1 の平坦水面で
    // ちょうど素通しの太陽照度＝エネルギー保存）。
    const float horizontalIlluminance = gLightIntensity * saturate(-lightDir.y);
    const float scalarIntensity = gIntensityScale * horizontalIlluminance * fresnelTransmittance
        * concentration * geometryFactor * coverage;
    // 収束品質（matchFactor）が低いピクセルは基準透過光へ寄せる。
    // 0 に落とすと置換モードで床が黒く抜けるため、必ずどちらかの透過光が出る。
    const float3 causticsRadiance = lerp(
        baselineRadiance,
        gLightColor * scalarIntensity * transmittance,
        matchFactor);

    if (gDebugViewMode != kRTCausticsDebugNone)
    {
        const float transmittanceLuma = Luminance(transmittance);
        const float intensityLuma = Luminance(causticsRadiance);
        const float3 debugColor = BuildCausticsDebugColor(
            gDebugViewMode,
            1.0f, // 旧 patternFade（撤去済み。Shallow Fade 表示は常に白）
            matchFactor,
            transmittanceLuma,
            geometryFactor,
            intensityLuma,
            concentration);
        gCausticsOutput[launchIndex] = float4(debugColor, coverage);
        return;
    }

    // gLightColor（シーンのライト色）× 波長依存透過率で、夕焼けや水質に連動した色になる。
    // α は波込みの水中被覆率（DeferredLighting の直接光置換率）。
    gCausticsOutput[launchIndex] = float4(causticsRadiance, coverage);
}

[shader("miss")]
void RTWaterCausticsMiss(inout RTWaterPayload payload)
{
    payload.hitT = 0.0f;
    payload.hitFlag = 0.0f;
}

[shader("closesthit")]
void RTWaterCausticsClosestHit(
    inout RTWaterPayload payload,
    in BuiltInTriangleIntersectionAttributes attr)
{
    payload.hitT = RayTCurrent();
    payload.hitFlag = 1.0f;
}
