#pragma once
#include "Math/Matrix/Matrix4x4.h"
#include "Math/Vector/Vector4.h"
#include "Math/Vector/Vector3.h"
#include <cstdint>


namespace CoreEngine
{
    /// @brief シェーディングモードの種別
    /// @note GPU定数バッファの shadingMode フィールドと値を一致させること
    enum class ShadingMode : int32_t {
        None = 0, // ライティングなし
        Lambert = 1, // ランバートシェーディング
        HalfLambert = 2, // ハーフランバートシェーディング
        Toon = 3  // トゥーンシェーディング
    };

    /// @brief GPU定数バッファに送信するマテリアルパラメータ
    /// @note シェーダーとのメモリレイアウトを一致させる必要があります
    struct MaterialConstants {
        Vector4 color;
        int32_t enableLighting;
        float padding[3];
        Matrix4x4 uvTransform;
        float shininess;     // 光沢度
        int32_t shadingMode; // シェーディングモード (ShadingMode 列挙型を int32_t としてキャストして使用)
        float toonThreshold; // トゥーンシェーディングの閾値 (0.0-1.0)
        float toonSmoothness; // トゥーンシェーディングの滑らかさ (0.0-0.5)
        int32_t enableDithering; // ディザリング有効化フラグ (0: 無効, 1: 有効)
        float ditheringScale; // ディザリングのスケール（デフォルト: 1.0f）
        int32_t enableEnvironmentMap; // 環境マップ有効化フラグ (0: 無効, 1: 有効)
        float environmentMapIntensity; // 環境マップの反射強度 (0.0-1.0)

        // ===== PBR Parameters =====
        float metallic; // 金属性 (0.0 = 非金属, 1.0 = 金属)
        float roughness; // 粗さ (0.0 = 滑らか, 1.0 = 粗い)
        float ao; // Ambient Occlusion (環境遮蔽: 0.0 = 完全遮蔽, 1.0 = 遮蔽なし)
        int32_t enablePBR; // PBR有効化フラグ (0: 無効(従来のライティング), 1: 有効)

        // ===== PBR Texture Map Flags =====
        int32_t useNormalMap;    // 法線マップ使用フラグ (0: 使用しない, 1: 使用する)
        int32_t useMetallicMap;  // メタリックマップ使用フラグ (0: 使用しない, 1: 使用する)
        int32_t useRoughnessMap; // ラフネスマップ使用フラグ (0: 使用しない, 1: 使用する)
        int32_t useAOMap;        // AOマップ使用フラグ (0: 使用しない, 1: 使用する)

        // ===== IBL Parameters =====
        int32_t enableIBL; // IBL有効化フラグ (0: 無効, 1: 有効)
        float iblIntensity; // IBL強度 (0.0-1.0, デフォルト: 1.0)
        float environmentRotationY; // 環境マップY軸回転（ラジアン）
        float padding2[1]; // パディング（16バイトアライメント）
    };

    static_assert(sizeof(MaterialConstants) % 16 == 0,
        "MaterialConstants must be 16-byte aligned for HLSL cbuffer");

}
