#pragma once

#include <cstdint>

namespace CoreEngine
{
    /// @brief 雲の見た目のスタイル
    /// @details 密度場そのものは全スタイルで共通で、「サンプル座標をどう量子化して
    ///          密度をどう二値化するか」だけが違う（HLSL 側 CloudBlocky.hlsli）。
    ///          値は定数バッファへそのまま乗るので、CloudBlocky.hlsli の
    ///          CLOUD_STYLE_* と番号を一致させること。
    enum class CloudStyle : uint32_t {
        Realistic = 0,  ///< 量子化なし。現行のボリューメトリック雲
        Voxel = 1,      ///< XZ + Y の 3 軸を量子化。立方体で構成された塊
        Minecraft = 2,  ///< XZ のみ量子化 + 箱型プロファイル。一定高度の平らな板
        Terraced = 3,   ///< Y のみ量子化。輪郭は有機的なまま高さが段々になる
        Count
    };

    /// @brief 範囲外のスタイル番号を丸める
    /// @details CVar の範囲は UI のヒントでしかなく Set はクランプしないため、
    ///          手書きの CVars.json 等で範囲外が入りうる。丸めておかないと C++ 側と
    ///          HLSL 側（未知の値は Voxel へ落ちる）で判定がずれる。
    constexpr uint32_t ClampCloudStyleIndex(uint32_t styleIndex) noexcept
    {
        constexpr uint32_t kLast = static_cast<uint32_t>(CloudStyle::Count) - 1u;
        return (styleIndex < kLast) ? styleIndex : kLast;
    }

    /// @brief スタイルがブロック雲（量子化あり）か
    constexpr bool IsBlockyCloudStyle(uint32_t styleIndex) noexcept
    {
        return styleIndex != static_cast<uint32_t>(CloudStyle::Realistic)
            && styleIndex < static_cast<uint32_t>(CloudStyle::Count);
    }

    /// @brief DDA ボクセルトラバーサル（CloudVoxelMarch.CS）を使うスタイルか
    /// @details 水平方向にも格子があるスタイルだけが対象。Terraced は縦しか量子化せず
    ///          水平の輪郭が連続なので、たどるべき格子が無く固定ステップのままにする。
    constexpr bool CloudStyleUsesVoxelMarch(uint32_t styleIndex) noexcept
    {
        return styleIndex == static_cast<uint32_t>(CloudStyle::Voxel)
            || styleIndex == static_cast<uint32_t>(CloudStyle::Minecraft);
    }

    /// @brief 雲の見た目パラメータ（単位はメートル・秒・無次元）
    /// @details 値の実体は CloudCVars が持つ。既定値もそちらにあるため、ここでは初期化しない。
    ///          チューニング指針は Docs/Engine/Graphics/Cloud/VolumetricCloud_Refactoring_Plan.md を見ること。
    struct VolumetricCloudParameters {
        // ===== 雲層ジオメトリ =====
        float layerBottomAltitudeM;     ///< 雲底高度 [m]
        float layerThicknessM;          ///< 層厚 [m]

        // ===== カバレッジ・密度 =====
        float globalCoverage;           ///< 全体カバレッジ倍率 [0,1]
        float densityScale;             ///< 密度→消散係数 [1/m]

        // ===== ノイズスケール（サンプル時のスケールのみ。ノイズ生成には影響しない） =====
        float baseNoiseScaleM;          ///< ベースノイズ 1 タイルの実寸 [m]
        float detailNoiseScaleM;        ///< ディテールノイズ 1 タイルの実寸 [m]
        float detailErosionStrength;    ///< 縁の侵食強度 [0,1]
        float weatherMapScaleM;         ///< 天候マップ 1 タイルの実寸 [m]
        float baseNoiseVerticalScale;   ///< ベースノイズの縦方向スケール倍率
        float heightSkewM;              ///< 高度による風下方向のずらし量 [m]

        // ===== 距離フェード =====
        float detailFadeDistanceM;      ///< ディテール侵食の強さを弱めきる距離 [m]
        float noiseLodBias;             ///< ノイズのミップ段のオフセット（負で細かく、正で粗く）
        float cloudStreetStretch;       ///< 天候マップを風方向へ引き伸ばす倍率（1 で等方）
        float cloudTopVariation;        ///< 雲頂高度を場所ごとにばらつかせる量（0 で一定）
        float farFadeWidthM;            ///< マーチ最大距離手前で密度をフェードさせる幅 [m]
        float hazeDistanceM;            ///< 遠方の雲が空色へ溶けるまでの消散距離 [m]

        // ===== 風（移流アニメーション） =====
        float windDirX;                 ///< 風向 XZ（正規化）
        float windDirZ;
        float windSpeedMPerS;           ///< 風速 [m/s]

        // ===== 配置ペイント（ワールド固定領域。天候マップと違いタイルしない） =====
        float paintRegionCenterX;       ///< ペイント領域の中心 X [m]
        float paintRegionCenterZ;       ///< ペイント領域の中心 Z [m]
        float paintRegionSizeM;         ///< ペイント領域の一辺 [m]
        float paintEdgeFade;            ///< 外周で影響度を落とす幅（領域サイズに対する比）

        // ===== ライティング =====
        float dropletDiameterUm;        ///< 雲粒の直径 [µm]。Mie 位相関数の唯一の形状パラメータ
        float maxPhase;                 ///< 位相関数の上限。前方ピークの発散を止める
        float ambientIntensity;         ///< Sky-View アンビエント倍率
        float ambientCosZenith;         ///< アンビエントで Sky-View LUT を引く仰角の cos
        float ambientBottomOcclusion;   ///< 雲底の空遮蔽率（1 で遮蔽なし）
        float ambientChroma;            ///< アンビエントに残す彩度 [0,1]
        float ambientGroundStrength;    ///< 地表反射の寄与倍率。0 で空だけ
        float beerPowderStrength;       ///< Powder 効果 [0,1]
        float lightMarchCoverage;       ///< サンライトマーチが覆う層内経路長の倍率
        float lightMarchConeSpread;     ///< サンライトマーチのコーン半径（距離に対する比）
        float maxSunOpticalDepth;       ///< サンライトマーチの光学的深さの上限

        // ===== 太陽散乱スケールと多重散乱（Hillaire オクターブ法） =====
        float sunLightScale;            ///< 雲の太陽散乱輝度スケール
        float msAttenuation;            ///< オクターブごとの消散減衰
        float msContribution;           ///< オクターブごとの寄与減衰
        float msEccentricity;           ///< オクターブごとの位相非対称度の減衰

        // ===== マーチング =====
        float earlyExitTransmittance;   ///< 早期終了しきい値
        float maxMarchDistanceM;        ///< マーチ最大距離 [m]
        uint32_t maxSteps;              ///< 反復回数の予算（実際の上限は 2 倍）
        uint32_t resolutionDivisor;     ///< レイマーチの解像度分割数（1=フル解像度, 2=半解像度）
        float upsampleDepthTolerance;   ///< アップサンプルでタップを棄却しはじめる相対距離差（0 で深度非考慮）

        // ===== 時間再投影 =====
        bool reprojectEnabled;          ///< 前フレームの結果を混ぜる
        float reprojectBlendMin;        ///< 履歴が使える画素の現フレーム寄与率（小さいほど収束が深い）
        float reprojectTolerance;       ///< 履歴を棄却しはじめる透過率の食い違い量

        // ===== 巻雲シェル（高層の薄い筋雲） =====
        float cirrusAltitudeM;          ///< 巻雲シェルの高度 [m]
        float cirrusCoverage;           ///< 巻雲の量 [0,1]。0 で無効
        float cirrusDensity;            ///< 巻雲の光学的深さのスケール
        float cirrusScaleM;             ///< 巻雲の模様 1 タイルの実寸 [m]
        float cirrusStretch;            ///< 風方向へ模様を引き伸ばす倍率
        float cirrusWindScale;          ///< 下層に対する巻雲の移流速度倍率

        // ===== ゴッドレイ（雲の隙間の光芒） =====
        bool godRayEnabled;             ///< ゴッドレイの有効/無効
        float godRayIntensity;          ///< 遮蔽差分（物理項）のスケール。1 が物理値
        float godRayMieBoost;           ///< 加算ミー項（演出）。0 で完全物理
        float godRayMaxDistanceM;       ///< ビューレイマーチの最大距離 [m]
        uint32_t godRayStepCount;       ///< ビューレイマーチのステップ数
        float cloudShadowRegionSizeM;   ///< 雲シャドウマップのカバー範囲（一辺）[m]
        float sceneShadowStrength;      ///< シーンへ落とす雲影の強さ（0 で落とさない）

        // ===== スタイル（ブロック雲） =====
        // Realistic 以外では雲影とスカイキューブマップも同じ量子化で追従する
        // （密度関数を共有しているため）。
        uint32_t styleIndex;            ///< CloudStyle。0 = Realistic
        float voxelSizeM;               ///< ボクセル 1 辺の実寸（水平）[m]
        float voxelHeightSizeM;         ///< ボクセルの縦の実寸 [m]。0 以下で水平と同じ
        float densityThreshold;         ///< 密度の二値化しきい値 [0,1]
        float voxelFaceBrightness;      ///< ボクセル面の直接光の倍率
        float voxelFaceShadeMin;        ///< 光源に背を向けた面の明るさ（小さいほど高コントラスト）
    };
}
