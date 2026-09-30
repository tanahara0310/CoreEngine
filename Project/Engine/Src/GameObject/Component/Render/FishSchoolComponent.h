#pragma once

#include "GameObject/Component/Core/IComponent.h"
#include "GameObject/Component/Render/IRenderableComponent.h"
#include "GameObject/Component/Transform/TransformComponent.h"
#include "Graphics/Asset/AssetRef.h"
#include "Graphics/Model/Model.h"
#include "Graphics/RayTracing/AccelerationStructureManager.h"
#include "Math/Geometry/Shapes.h"
#include "Reflection/Reflect.h"

#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace CoreEngine
{
class ModelManager;

/// @brief 魚の群れを、配置 JSON（`Models/Okinawa/FishSchools/*.json`）どおりにまとめて描くコンポーネント
/// @details JSON の `models`（モデル名 → glTF の相対パス）を 1 種類につき 1 つだけ読み込み、
///          `instances`（位置・回転・大きさ）の全個体を `Model::DrawInstances` でインスタンシング描画する
///          （種類ごと・サブメッシュごとに 1 回の DrawIndexedInstanced）。
///          体のくねりと胸びれはモデルのマテリアルの頂点アニメーション（魚）が動かし、
///          ここでは 1 匹ずつの小さな漂い（上下・前後・首振り）を足す。
///          群れ全体の位置・向き・大きさは TransformComponent で決め、泳がせて移動するのはゲーム側で行う。
/// @note レイトレーシングの影には個体ごとに載る（`CollectRayTracingInstances`）。
///       体のくねりは 1 cm ほどなので、影の形は静止したモデル（共有の BLAS）で足りる。
class FishSchoolComponent : public IComponent, public IRenderableComponent {
public:
    FishSchoolComponent() = default;
    ~FishSchoolComponent() override;

    const char* GetTypeName() const override { return "FishSchool"; }

    /// @brief トランスフォームを使う
    bool RequiresComponent(const IComponent& other) const override
    {
        return dynamic_cast<const TransformComponent*>(&other) != nullptr;
    }

    REFLECT_DECLARE(FishSchoolComponent)

    // ===== 群れの指定 =====

    /// @brief 配置 JSON をパスかファイル名で指す（Awake 済みなら読み込み直す）
    void SetSchoolFile(const std::string& path);

    /// @brief 指している配置 JSON（型記述子とやり取りする値）
    Reflection::AssetRefValue GetSchoolAsset() const { return schoolAsset_.GetValue(); }

    /// @brief 配置 JSON を指し直す（Awake 済みなら読み込み直す。何も指さなければ群れを消す）
    void SetSchoolAsset(const Reflection::AssetRefValue& value);

    // ===== 見た目 =====

    /// @brief 泳ぎ（体のくねり・胸びれ）の速さの倍率（モデルの既定の速さに掛ける）
    float GetSwimSpeed() const { return swimSpeed_; }
    void SetSwimSpeed(float speed);

    /// @brief 1 匹ずつの漂い（上下・前後・首振り）の大きさの倍率（0 で配置どおりに止まる）
    float GetDrift() const { return drift_; }
    void SetDrift(float drift) { drift_ = drift; }

    /// @brief レイトレーシングの影に入れるか
    bool IsCastingShadow() const { return castShadow_; }
    void SetCastShadow(bool cast) { castShadow_ = cast; }

    /// @brief 読み込んだ個体の数
    int GetFishCount() const { return static_cast<int>(instances_.size()); }

    // ===== IRenderableComponent =====

    RenderPassType GetRenderPassType() const override { return RenderPassType::Model; }
    void Render(const DrawViewInfo& view) override;

    // ===== ライフサイクル =====

    /// @brief トランスフォームを確保し、指定済みの配置 JSON を読み込む
    void Awake() override;

    // ===== レイトレーシング =====

    /// @brief TLAS へ載せる個体を足す（影を落とす設定のときだけ。RayTracingSubsystem が呼ぶ）
    void CollectRayTracingInstances(std::vector<AccelerationStructureManager::InstanceDesc>& out);

    /// @brief 群れ全体のワールド空間の AABB（無ければ無効な箱）
    BoundingBox GetWorldBoundingBox();

private:
    /// @brief 魚の種類（モデル 1 つ）
    struct Species {
        std::string name;               ///< JSON の models のキー
        std::unique_ptr<Model> model;   ///< 全個体で共有するモデル（マテリアルもこの 1 組）
        float length = 0.1f;            ///< モデルの全長 [m]（漂いの大きさの基準）
    };

    /// @brief 1 匹
    struct Fish {
        uint32_t species = 0;           ///< species_ の添字
        Matrix4x4 local{};              ///< 群れの原点から見た配置（大きさ・回転・位置）
        float phase[3] = {};            ///< 漂いの位相 0..1（上下・前後・首振り）
        float hz[3] = {};               ///< 漂いの周波数 [Hz]
        Matrix4x4 prevWVP{};            ///< 前フレームの WVP（モーションベクター用）
        uint64_t prevFrame = kNoFrame;  ///< prevWVP を書いたフレーム
    };

    static constexpr uint64_t kNoFrame = (std::numeric_limits<uint64_t>::max)();

    /// @brief transform_ が未取得なら取りに行く
    TransformComponent* ResolveTransform() const;

    /// @brief 配置 JSON とモデルを読み込み直す
    void LoadSchool();

    /// @brief 読み込んだ配置 JSON から種類と個体を作る（型の違う値があると json::exception を投げる）
    void LoadSchoolFrom(const json& school, const std::string& schoolPath, ModelManager& modelMgr);

    /// @brief 全個体の今フレームのワールド行列を作る（同じフレーム・同じ群れの行列なら作り直さない）
    const std::vector<Matrix4x4>& UpdateWorldMatrices();

    /// @brief 全マテリアルへ泳ぎの速さを入れる
    void ApplySwimSpeed();

    AssetRef<JsonAsset> schoolAsset_;       ///< 配置 JSON（保存とインスペクタに出す）
    float swimSpeed_ = 1.0f;
    float drift_ = 1.0f;
    bool castShadow_ = true;
    bool awoken_ = false;

    std::vector<Species> species_;
    std::vector<Fish> instances_;
    BoundingBox localBounds_;               ///< 群れの原点から見た全個体の AABB（漂いの分は含まない）
    float driftReach_ = 0.0f;               ///< 漂いの倍率 1 で個体が動きうる距離の最大 [m]（群れの原点の単位）

    // UpdateWorldMatrices のキャッシュ
    std::vector<Matrix4x4> worlds_;
    uint64_t worldsFrame_ = kNoFrame;
    Matrix4x4 worldsSchoolMatrix_{};

    // Render で使い回す作業用の配列
    std::vector<Matrix4x4> drawWorlds_;
    std::vector<Matrix4x4> drawPrevWVPs_;

    mutable TransformComponent* transform_ = nullptr;
};
}
