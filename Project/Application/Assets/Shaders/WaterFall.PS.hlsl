// マップ端の落水カーテンのピクセルシェーダー。
// ------------------------------------------------------------
// ライティングは既定のフォワード（ForwardMain）へそのまま任せ、
// その前後で「流れの筋」「白泡」「下端の霧散」だけを足す。
// こうすると水面板と同じ光・影・フォグに乗るので、時間帯が変わっても
// カーテンだけ色が取り残されない。
//
// 模様はテクスチャを使わず、ワールド座標と時刻から手続き的に作る。
// 板ごとに UV を持たないので、隣り合うマスのカーテンでも筋が繋がる。
#include "Object3dForward.hlsli"
// cbuffer のレイアウトを VS と 1 文字も違わず揃えるため、波の構造体定義も同じものを引く。
#include "GerstnerWave.hlsli"

// WaterFall.VS.hlsl と同じ宣言。両方から読むことで反射結果が
// D3D12_SHADER_VISIBILITY_ALL へ統合され、ルートパラメータは 1 本で済む。
cbuffer WaterFallConstants : register(b8)
{
    GerstnerWave gWaves[GERSTNER_MAX_WAVE_COUNT];
    uint gActiveWaveCount;
    float gTime;
    float gSurfaceY;
    float gFallLength;
    float gFlowSpeed;
    float gFoamStrength;
    float gPatternScale;
    float gFallPadding;
};

/// @brief 列ごとの固定位相（0..1）
/// @details 隣の列と無関係な値にするための素朴なハッシュ。
float HashColumn(float column)
{
    return frac(sin(column * 12.9898f) * 43758.5453f);
}

/// @brief 落水の筋模様（0..1）
/// @param lane 板に沿った横位置[m]
/// @param height ワールド Y[m]
/// @details 横方向の「リボン」は高さをまったく含めない。ここに高さを混ぜると
///          筋が斜めになり、真上寄りのカメラでは縦の落下が横縞に見えてしまう。
///          流れは列ごとに位相をずらした縦スクロールだけで作る。
float EvaluateFlowBand(float lane, float height, float scale)
{
    const float side = lane * scale;

    // 上端から下端までまっすぐ通る縦の筋。ここが模様の主役になる。
    const float ribbon = sin(side * 5.3f) * 0.6f + sin(side * 11.9f + 2.2f) * 0.4f;

    // 時間で下へ流す粒。位相を y + speed*t にすると、模様は y が減る向き＝下へ動く。
    // 列ごとに位相をずらすので、横に揃った縞が下りてくる見え方にならない。
    // 筋より弱く・縦に長くすること。粒を強くすると格子模様に見えて水に見えない。
    const float jitter = HashColumn(floor(side * 2.0f));
    const float phase = (height + gTime * gFlowSpeed) * scale;
    const float grain =
        sin(phase * 3.1f + jitter * 6.2831853f) * 0.6f
        + sin(phase * 7.7f + jitter * 4.1f + 2.1f) * 0.4f;

    return saturate(0.5f + ribbon * 0.34f + grain * 0.16f);
}

/// @brief 落ち口（＝すぐ上の水面板）と同じ影の濃さへ揃える補正倍率
/// @param screenPos  この画素のスクリーン座標（SV_POSITION.xy）
/// @param fallT      落下率 0..1
/// @param dFallTdy   fallT のスクリーン縦方向微分（制御フローの外で取ること）
/// @details RT シャドウはスクリーン空間のマスクで、不透明の G-Buffer から作られる。
///          落水カーテンはブレンド＝フォワード描画なので G-Buffer に居らず、
///          マップの外へはみ出した部分は背景の空、つまり「影なし」を拾ってしまう。
///          雲の影がマップへ落ちている間だけ水面が暗くカーテンが明るくなり、
///          落ち口が明るさの段差になる（実測 #5EC5D8 対 #A0D4E0＝約 3 倍）。
///          カーテンの画素から落ち口までのスクリーン距離は fallT の縦微分で求まるので、
///          そこのマスクを引いて比を掛け、水面と同じ影の下に置く。
float ResolveLipShadowCorrection(float2 screenPos, float fallT, float dFallTdy)
{
    float maskWidth, maskHeight;
    gRTShadowMask.GetDimensions(maskWidth, maskHeight);
    // マスク未提供（非 DXR 環境の 1x1 ダミー）なら ForwardMain 側も影なしなので揃っている
    if (maskWidth <= 1.0f || maskHeight <= 1.0f || abs(dFallTdy) < 1.0e-6f)
    {
        return 1.0f;
    }

    const float2 maxCoord = float2(maskWidth - 1.0f, maskHeight - 1.0f);
    const float2 lipPos = float2(screenPos.x, screenPos.y - fallT / dFallTdy);
    const int2 hereCoord = int2(clamp(screenPos, float2(0.0f, 0.0f), maxCoord));
    const int2 lipCoord = int2(clamp(lipPos, float2(0.0f, 0.0f), maxCoord));

    // ForwardMain の shadowFactor と同じ式で比を取る（式がずれると補正が効かない）
    const float shadowHere = lerp(0.3f, 1.0f, gRTShadowMask.Load(int3(hereCoord, 0)).r);
    const float shadowLip = lerp(0.3f, 1.0f, gRTShadowMask.Load(int3(lipCoord, 0)).r);
    return shadowLip / max(shadowHere, 1.0e-4f);
}

PixelShaderOutput main(VertexShaderOutput input)
{
    // 上端 0 → 下端 1。VS と同じ式で求める。
    const float fallRaw = (gSurfaceY - input.worldPosition.y) / max(gFallLength, 1.0e-4f);
    const float fallT = saturate(fallRaw);
    // 微分は discard より前・分岐の外で取る（そうしないと 2x2 クアッドの値が揃わない）。
    // saturate 前の値を使うこと。上端は 0 でクランプされていて微分が 0 になる。
    const float dFallTdy = ddy(fallRaw);

    // 落ち口の帯。ここでは水面板と 1 ピクセルも違わない色を出す。
    // 筋も泡も透過も、この重みで落ち始めてから効かせる。
    // 落ち口で少しでも違う色を出すと、水面板との境目が線として見えてしまう。
    const float fallIn = smoothstep(0.0f, 0.16f, fallT);

    // 横位置。X と Z を混ぜておくと、手前端と奥端でまったく同じ筋にならない。
    const float lane = input.worldPosition.x * 1.7f + input.worldPosition.z * 0.31f;
    const float band = EvaluateFlowBand(lane, input.worldPosition.y, gPatternScale);

    // ---- 不透明度 ----
    // 下へ行くほど薄くし、筋の濃淡でばらけさせて霧散させる。
    // 一様に消すと下端が水平な直線になり、切り取った板だと分かってしまう。
    // 空が明るいので、途中まではしっかり残さないと色が飛んで見えなくなる。
    // @note 空は HDR では水よりずっと明るい。α を 0.9 まで落としただけでも
    //       残り 1 割ぶんの空が混ざって水色が目に見えて白く浮く（実測）。
    //       透かすのは霧散させたい下端だけに限り、それまではほぼ不透明で通すこと。
    const float fade = 1.0f - smoothstep(0.70f, 1.0f, fallT);
    const float alpha = saturate(lerp(1.0f, fade * lerp(0.97f, 1.0f, band), fallIn));

    // 消えきった部分は深度も書かせない（書くと空を四角く抜いてしまう）
    if (alpha <= 0.02f)
    {
        discard;
    }

    // ---- ライティング ----
    // 「真上にある水面板とまったく同じ条件で」照らす。落ちているのも同じ水なので、
    // 色が繋がっていないと板を貼り付けたようにしか見えない。
    // そのために 2 つとも水面の値へ差し替える。
    //
    // 1) 法線を真上へ。板は垂直なので、面の向きのまま照らすと真上からの光を
    //    ほとんど受けられず、半透明の黒い板になる。lerp で中途半端に倒しても、
    //    光の向きが変わればまた暗く沈む。
    // 2) ワールド座標の高さを静止水面へ。高さフォグは低いほど濃いので、
    //    実際の高さで評価すると落ちるほど霧の色へ漂白され、
    //    下半分が空と見分けが付かなくなる（実測: 落差 6m でほぼ空色）。
    //    ライトの距離計算にも使われるが、水面から数 m の差でしかない。
    input.normal = float3(0.0f, 1.0f, 0.0f);
    input.worldPosition.y = gSurfaceY;

    PixelShaderOutput output = ForwardMain(input);

    // 筋は法線ではなく明るさで付ける。垂直面で法線を振ると、
    // 振った向き次第で簡単に光から外れて黒く落ちてしまうため。
    output.color.rgb *= lerp(1.0f, lerp(0.86f, 1.10f, band), fallIn);
    // 雲の影は落ち口の水面と同じ濃さで掛ける
    output.color.rgb *= ResolveLipShadowCorrection(input.position.xy, fallT, dFallTdy);

    // ---- 白泡 ----
    // 落ち口の砕けと、下ほど強くなる筋の白さ。
    // 全面に乗せると水色が飛んで、明るい空を背景にしたとき何も見えなくなる。
    // 落ち口には泡を置かない。ここを白くすると水面板との境目で色が段差になり、
    // 「水が繋がって落ちている」ようには見えなくなる。泡は落ちながら増やす。
    const float bandFoam = smoothstep(0.72f, 0.98f, band) * (0.06f + 0.34f * fallT);
    const float foam = saturate(bandFoam * gFoamStrength * fallIn);

    // HDR のまま扱うので白（1.0）へ寄せるのではなく、
    // 「彩度を落として明るくする」ことで泡にする。露出が変わっても破綻しない。
    const float3 lit = output.color.rgb;
    const float luminance = dot(lit, float3(0.299f, 0.587f, 0.114f));
    const float3 gray = float3(luminance, luminance, luminance);
    // 倍率を上げすぎると、トーンマップ後にほぼ真っ白へ振り切れて水色が消える。
    output.color.rgb = lerp(lit, lerp(lit, gray, 0.6f) * 1.35f, foam);
    output.color.a = alpha;

    return output;
}
