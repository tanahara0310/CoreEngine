#pragma once

#include "GameObject/Primitive/PrimitiveGameObject.h"
#include "GameObject/Component/Scene/SceneTagComponent.h"
#include "Math/MathCore.h"

/// @brief y=0 に広がる無限遠グレータイル床（全シーン共通の既定床）
///
/// Unity / UE のデフォルト床に相当する、市松模様の無限地面。
/// 巨大な平面をカメラの XZ に追従させ、タイル周期にスナップすることで
/// 「どこまでも続く床」を表現する。タイルはワールド座標に固定されるため、
/// カメラが動いても模様は滑らない（1マス = 1m）。
///
/// テクスチャ tile_white.png（2×2 マスの市松模様＝テクスチャ 1 周が 2m）を
/// WRAP サンプラーで連続タイル化する。UV はメッシュ生成時に
/// 「ローカル座標 / kTileWorldSize」として焼き込む（平面中心が UV 原点）。
class InfiniteGroundObject : public CoreEngine::PrimitiveGameObject {
public:
    /// @brief コンストラクタ（「シーンの床」タグを付ける。Feature が具象型を知らずに見つけるため）
    InfiniteGroundObject() {
        AddComponent<CoreEngine::SceneTagComponent<InfiniteGroundObject>>(this);
    }

    const char* GetObjectName() const override;

    /// @brief カメラの XZ に追従して床を移動する
    /// @param cameraPosition ゲームビューカメラのワールド座標
    /// @details タイル周期（kTileWorldSize）にスナップして移動するため、
    ///          タイル模様はワールドに固定され、カメラ移動で滑らない。
    void FollowCamera(const CoreEngine::Vector3& cameraPosition);

    /// テクスチャ 1 周が覆うワールド距離（m）。
    /// tile_white.png は 2×2 マスの市松模様なので 2m（= 2マス）。
    static constexpr float kTileWorldSize = 2.0f;

    /// 平面の半径（m）。中心（＝カメラ）から ±kHalfExtent まで広がる。
    /// 大気・雲シーンが使う最大のファークリップ（50km）を全方向で覆えるサイズにし、
    /// 常に「平面の縁」ではなく「ファークリップ」で切れるようにする。
    static constexpr float kHalfExtent = 60000.0f;

    /// 平面の分割数（片軸）。
    /// 巨大な単一クアッドだと、頂点属性（UV・ワールド座標）の補間が
    /// 大きな値同士の桁落ちを起こしカメラ近傍で精度が落ちる。
    /// 分割してカメラ近傍の頂点が小さな値を持つようにすることで精度を保つ。
    static constexpr uint32_t kSubdivisions = 128;

    /// 床の高さ（ワールド y 座標）。
    static constexpr float kGroundHeight = 0.0f;

protected:
    std::string GetTexturePath() const override;
    std::unique_ptr<CoreEngine::IPrimitiveMeshGenerator> CreateMeshGenerator() const override;
    void OnInitialize() override;
};
