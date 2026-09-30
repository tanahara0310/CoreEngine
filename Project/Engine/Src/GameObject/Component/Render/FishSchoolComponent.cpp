#include "pch.h"
#include "FishSchoolComponent.h"

#include "Camera/View/ViewInfo.h"
#include "EngineSystem/EngineSystem.h"
#include "GameObject/Component/Core/ComponentFactory.h"
#include "GameObject/GameObject.h"
#include "Graphics/Asset/AssetInfo.h"
#include "Graphics/Material/MaterialInstance.h"
#include "Graphics/Model/ModelManager.h"
#include "Graphics/Model/ModelResource.h"
#include "Graphics/Render/Culling/ModelVisibility.h"
#include "Graphics/Render/DrawViewInfo.h"
#include "Graphics/Render/Model/VertexAnimation.h"
#include "Math/MathCore.h"
#include "Utility/FrameRate/Time.h"
#include "Utility/JsonManager/JsonManager.h"
#include "Utility/Logger/Logger.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <unordered_map>
#include <utility>

COMPONENT_REGISTER(CoreEngine::FishSchoolComponent)

REFLECT_DEFINE_BEGIN(CoreEngine::FishSchoolComponent, "魚の群れ")
    REFLECT_ACCESSOR("school", "群れの配置", GetSchoolAsset, SetSchoolAsset,
        p.assetType = ::CoreEngine::AssetType::Json,
        p.tooltip = "Models/Okinawa/FishSchools の配置 JSON（使うモデルと、1 匹ずつの位置・向き・大きさ）")
    REFLECT_ACCESSOR("swimSpeed", "泳ぎの速さ", GetSwimSpeed, SetSwimSpeed,
        p.range = Range(0.1f, 3.0f),
        p.tooltip = "体のくねりと胸びれの速さの倍率（魚の種類ごとの既定の速さに掛ける）")
    REFLECT_ACCESSOR("drift", "漂い", GetDrift, SetDrift,
        p.range = Range(0.0f, 3.0f),
        p.tooltip = "1 匹ずつ小さく上下・前後に漂い、首を振る大きさの倍率（0 で配置どおりに止まる）")
    REFLECT_ACCESSOR("castShadow", "影を落とす", IsCastingShadow, SetCastShadow,
        p.tooltip = "レイトレーシングの影（と水中のコースティクス）に 1 匹ずつ入れる")
    REFLECT_READONLY_ACCESSOR("fishCount", "匹数", GetFishCount)
REFLECT_DEFINE_END()
REFLECT_REGISTER(CoreEngine::FishSchoolComponent)

namespace CoreEngine
{
    namespace
    {
        constexpr float kTwoPi = 6.28318531f;

        // 漂いの大きさ（全長に対する割合）と首振りの角度（漂いの倍率 1 のとき）
        constexpr float kBobAmplitude = 0.12f;    ///< 上下
        constexpr float kSurgeAmplitude = 0.25f;  ///< 前後
        constexpr float kYawAmplitude = 0.087f;   ///< 首振り [rad]（約 5°）

        /// @brief "a/b/../c" のようなパスを正規化する（区切りは '/'）
        std::string NormalizeSlashPath(const std::string& path)
        {
            std::vector<std::string> parts;
            std::string current;
            auto flush = [&]() {
                if (current.empty() || current == ".") {
                    current.clear();
                    return;
                }
                if (current == ".." && !parts.empty() && parts.back() != "..") {
                    parts.pop_back();
                } else {
                    parts.push_back(current);
                }
                current.clear();
            };
            for (const char c : path) {
                if (c == '/' || c == '\\') {
                    flush();
                } else {
                    current.push_back(c);
                }
            }
            flush();

            std::string out = (!path.empty() && (path.front() == '/' || path.front() == '\\')) ? "/" : "";
            for (size_t i = 0; i < parts.size(); ++i) {
                if (i > 0) {
                    out += '/';
                }
                out += parts[i];
            }
            return out;
        }

        /// @brief パスの親フォルダ（区切りが無ければ空）
        std::string ParentDirectory(const std::string& path)
        {
            const size_t slash = path.find_last_of("/\\");
            return (slash == std::string::npos) ? std::string{} : path.substr(0, slash);
        }

        /// @brief JSON の数の配列を読む（足りなければ既定値のまま）
        template <size_t N>
        void ReadFloats(const json& node, const char* key, float (&out)[N])
        {
            const auto it = node.find(key);
            if (it == node.end() || !it->is_array()) {
                return;
            }
            for (size_t i = 0; i < N && i < it->size(); ++i) {
                if ((*it)[i].is_number()) {
                    out[i] = (*it)[i].get<float>();
                }
            }
        }

        /// @brief 整数から 0..1 の値を作る（個体ごとの漂いのばらつき用）
        float Hash01(uint32_t x)
        {
            x ^= x >> 16;
            x *= 0x7FEB352Du;
            x ^= x >> 15;
            x *= 0x846CA68Bu;
            x ^= x >> 16;
            return static_cast<float>(x >> 8) / static_cast<float>(1u << 24);
        }

        /// @brief 周波数を頂点アニメーションの時間の折り返し周期の逆数の整数倍に丸める（折り返しで途切れない）
        float QuantizeHz(float hz)
        {
            const float wrap = VertexAnimationParams::kWrapSeconds;
            return (std::max)(1.0f, std::round(hz * wrap)) / wrap;
        }

        /// @brief 行列が同じか（ビット単位）
        bool SameMatrix(const Matrix4x4& a, const Matrix4x4& b)
        {
            return std::memcmp(a.m, b.m, sizeof(a.m)) == 0;
        }
    }

    FishSchoolComponent::~FishSchoolComponent() = default;

    // ===== 群れの指定 =====

    void FishSchoolComponent::SetSchoolFile(const std::string& path)
    {
        schoolAsset_.SetPath(path);
        if (awoken_) {
            LoadSchool();
        }
    }

    void FishSchoolComponent::SetSchoolAsset(const Reflection::AssetRefValue& value)
    {
        if (value == schoolAsset_.GetValue()) {
            return;
        }
        schoolAsset_.SetValue(value);
        if (awoken_) {
            LoadSchool();
        }
    }

    void FishSchoolComponent::SetSwimSpeed(float speed)
    {
        swimSpeed_ = (std::max)(speed, 0.0f);
        ApplySwimSpeed();
    }

    // ===== ライフサイクル =====

    void FishSchoolComponent::Awake()
    {
        awoken_ = true;
        // トランスフォームは群れの置き場所なので、無ければ自動で足す
        if (GameObject* owner = GetOwner()) {
            transform_ = owner->GetOrAddComponent<TransformComponent>();
        }
        LoadSchool();
    }

    TransformComponent* FishSchoolComponent::ResolveTransform() const
    {
        if (!transform_) {
            transform_ = Sibling<TransformComponent>();
        }
        return transform_;
    }

    // 配置 JSON を読み、種類ごとにモデルを 1 つ作って、1 匹ずつの配置を控える
    void FishSchoolComponent::LoadSchool()
    {
        species_.clear();
        instances_.clear();
        localBounds_ = BoundingBox();
        driftReach_ = 0.0f;
        worlds_.clear();
        worldsFrame_ = kNoFrame;

        if (!schoolAsset_.IsSet()) {
            return;
        }
        GameObject* owner = GetOwner();
        EngineSystem* engine = owner ? owner->GetEngineSystem() : nullptr;
        auto* modelMgr = engine ? engine->GetService<ModelManager>() : nullptr;
        if (!modelMgr) {
            return;
        }

        Logger& log = Logger::GetInstance();
        const std::string schoolPath = schoolAsset_.GetPath();
        const json school = JsonManager::GetInstance().LoadJson(schoolPath);
        if (!school.is_object() || !school.contains("models") || !school["models"].is_object()
            || !school.contains("instances") || !school["instances"].is_array()) {
            log.Logf(LogLevel::Warn, LogCategory::Resource, "{}",
                "[FishSchool] 配置 JSON を読めない（models / instances が無い）: " + schoolPath);
            return;
        }
        try {
            LoadSchoolFrom(school, schoolPath, *modelMgr);
        }
        catch (const json::exception& e) {
            // 手で書き換えて型が違う値（文字列の scale など）が入っていても止まらないように
            log.Logf(LogLevel::Warn, LogCategory::Resource, "{}",
                "[FishSchool] 配置 JSON の値が読めない: " + schoolPath + " (" + e.what() + ")");
            species_.clear();
            instances_.clear();
            localBounds_ = BoundingBox();
            driftReach_ = 0.0f;
            return;
        }

        ApplySwimSpeed();

        log.Logf(LogLevel::Info, LogCategory::Resource, "{}", std::format(
            "[FishSchool] {}: {} 種類 {} 匹", schoolPath, species_.size(), instances_.size()));
    }

    // 種類（モデル）を読み込み、1 匹ずつの配置を控える
    void FishSchoolComponent::LoadSchoolFrom(const json& school, const std::string& schoolPath, ModelManager& modelMgr)
    {
        Logger& log = Logger::GetInstance();

        // ===== 種類（models: 名前 → JSON のフォルダから見た glTF の相対パス） =====
        const std::string directory = ParentDirectory(schoolPath);
        std::unordered_map<std::string, uint32_t> speciesIndex;
        for (const auto& [name, relative] : school["models"].items()) {
            if (!relative.is_string()) {
                continue;
            }
            const std::string path = NormalizeSlashPath(
                directory.empty() ? relative.get<std::string>() : directory + "/" + relative.get<std::string>());
            // 無いモデルを ModelManager に渡すと読み込みエラーで止まるので、アセットとして引けるものだけ読む
            const AssetInfo* info = FindAssetInfo(path, AssetType::Model);
            if (!info) {
                log.Logf(LogLevel::Warn, LogCategory::Resource, "{}",
                    "[FishSchool] モデルが見つからない: " + path + "（" + schoolPath + "）");
                continue;
            }

            Species species;
            species.name = name;
            species.model = modelMgr.CreateStaticModel(ToAssetPath(*info));
            const ModelResource* resource = species.model ? species.model->GetModelResource() : nullptr;
            if (!resource) {
                continue;
            }
            // 全長: 頭が +X を向いたまっすぐなモデルなので X の幅。AABB は揺れの分だけ広げてあるので頂点から測る
            float minX = 0.0f, maxX = 0.0f;
            bool first = true;
            for (const VertexData& v : resource->GetModelData().vertices) {
                minX = first ? v.position.x : (std::min)(minX, v.position.x);
                maxX = first ? v.position.x : (std::max)(maxX, v.position.x);
                first = false;
            }
            species.length = (std::max)(maxX - minX, 0.01f);

            // 種類の既定が無いモデルでも、揺れの値を持っていれば魚として泳がせる
            if (resource->HasVertexAnimationData()) {
                for (size_t i = 0; i < species.model->GetMaterialCount(); ++i) {
                    const MaterialInstance* defaults = resource->GetDefaultMaterial(static_cast<uint32_t>(i));
                    if (defaults && defaults->GetVertexAnimation() == VertexAnimationType::None) {
                        species.model->GetMaterial(i)->SetVertexAnimation(VertexAnimationType::Fish);
                    }
                }
            }

            speciesIndex[name] = static_cast<uint32_t>(species_.size());
            species_.push_back(std::move(species));
        }

        // ===== 1 匹ずつ（位置・回転 [x, y, z, w]・大きさ・位相。座標はエンジン座標で群れの中心が原点） =====
        const json& list = school["instances"];
        instances_.reserve(list.size());
        for (size_t n = 0; n < list.size(); ++n) {
            const json& item = list[n];
            if (!item.is_object()) {
                continue;
            }
            const auto it = speciesIndex.find(item.value("model", std::string{}));
            if (it == speciesIndex.end()) {
                continue;
            }
            float position[3] = { 0.0f, 0.0f, 0.0f };
            float rotation[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
            ReadFloats(item, "position", position);
            ReadFloats(item, "rotation", rotation);
            const float scale = item.value("scale", 1.0f);
            const float phase = item.value("phase", Hash01(static_cast<uint32_t>(n) * 3u + 17u));

            Fish fish;
            fish.species = it->second;
            fish.local = MathCore::Matrix::MakeAffine(
                Vector3{ scale, scale, scale },
                Quaternion{ rotation[0], rotation[1], rotation[2], rotation[3] },
                Vector3{ position[0], position[1], position[2] });
            // 漂いは個体ごとに周期と位相をずらす（そろって動くと群れが 1 つの塊に見える）
            const uint32_t seed = static_cast<uint32_t>(n) * 0x9E3779B9u;
            fish.phase[0] = phase;
            fish.phase[1] = Hash01(seed + 1u);
            fish.phase[2] = Hash01(seed + 2u);
            fish.hz[0] = QuantizeHz(0.16f + 0.10f * Hash01(seed + 3u));
            fish.hz[1] = QuantizeHz(0.11f + 0.07f * Hash01(seed + 4u));
            fish.hz[2] = QuantizeHz(0.07f + 0.05f * Hash01(seed + 5u));

            const Species& species = species_[fish.species];
            const BoundingBox& modelBounds = species.model->GetModelResource()->GetLocalBoundingBox();
            const BoundingBox placed = modelBounds.TransformBy(fish.local);
            if (placed.IsValid()) {
                localBounds_.min = { (std::min)(localBounds_.min.x, placed.min.x),
                    (std::min)(localBounds_.min.y, placed.min.y), (std::min)(localBounds_.min.z, placed.min.z) };
                localBounds_.max = { (std::max)(localBounds_.max.x, placed.max.x),
                    (std::max)(localBounds_.max.y, placed.max.y), (std::max)(localBounds_.max.z, placed.max.z) };
            }
            // 前後 + 上下 + 首振りで頭と尾が振れる分
            const float reach = (kSurgeAmplitude + kBobAmplitude + 0.5f * std::sin(kYawAmplitude))
                * species.length * scale;
            driftReach_ = (std::max)(driftReach_, reach);

            instances_.push_back(fish);
        }
    }

    void FishSchoolComponent::ApplySwimSpeed()
    {
        for (Species& species : species_) {
            if (!species.model) {
                continue;
            }
            const ModelResource* resource = species.model->GetModelResource();
            for (size_t i = 0; i < species.model->GetMaterialCount(); ++i) {
                const MaterialInstance* defaults =
                    resource ? resource->GetDefaultMaterial(static_cast<uint32_t>(i)) : nullptr;
                const float baseSpeed = defaults ? defaults->GetVertexAnimSpeed() : 1.0f;
                // 倍率 1 のままならモデル共有の既定マテリアルを使い続ける（他の配置とまとめて描ける）
                const MaterialInstance* current = std::as_const(*species.model).GetMaterial(i);
                const float target = baseSpeed * swimSpeed_;
                if (current && current->GetVertexAnimSpeed() == target) {
                    continue;
                }
                if (MaterialInstance* material = species.model->GetMaterial(i)) {
                    material->SetVertexAnimSpeed(target);
                }
            }
        }
    }

    // ===== 行列 =====

    const std::vector<Matrix4x4>& FishSchoolComponent::UpdateWorldMatrices()
    {
        const TransformComponent* transform = ResolveTransform();
        const Matrix4x4 schoolMatrix = transform ? transform->Get().GetWorldMatrix() : MathCore::Matrix::Identity();
        const uint64_t frame = Time::FrameCount();
        if (frame == worldsFrame_ && worlds_.size() == instances_.size() && SameMatrix(schoolMatrix, worldsSchoolMatrix_)) {
            return worlds_;
        }
        worldsFrame_ = frame;
        worldsSchoolMatrix_ = schoolMatrix;
        worlds_.resize(instances_.size());

        // 頂点アニメーションと同じ時間（折り返しあり・止めると止まる）
        const float time = VertexAnimationCVars::Enabled.Get()
            ? std::fmod(Time::TimeSinceStartup(), VertexAnimationParams::kWrapSeconds) : 0.0f;
        const float drift = (std::max)(drift_, 0.0f);

        for (size_t i = 0; i < instances_.size(); ++i) {
            const Fish& fish = instances_[i];
            if (drift <= 0.0f) {
                worlds_[i] = fish.local * schoolMatrix;
                continue;
            }
            // 魚のモデル空間（頭 +X・背 +Y）で、前後・上下にずらして首を振る
            const float length = species_[fish.species].length;
            const float bob = drift * kBobAmplitude * length * std::sin(kTwoPi * (fish.hz[0] * time + fish.phase[0]));
            const float surge = drift * kSurgeAmplitude * length * std::sin(kTwoPi * (fish.hz[1] * time + fish.phase[1]));
            const float yaw = drift * kYawAmplitude * std::sin(kTwoPi * (fish.hz[2] * time + fish.phase[2]));
            const Matrix4x4 wander = MathCore::Matrix::MakeAffine(
                Vector3{ 1.0f, 1.0f, 1.0f },
                Quaternion{ 0.0f, std::sin(0.5f * yaw), 0.0f, std::cos(0.5f * yaw) },
                Vector3{ surge, bob, 0.0f });
            worlds_[i] = wander * fish.local * schoolMatrix;
        }
        return worlds_;
    }

    BoundingBox FishSchoolComponent::GetWorldBoundingBox()
    {
        const TransformComponent* transform = ResolveTransform();
        if (instances_.empty() || !localBounds_.IsValid() || !transform) {
            return BoundingBox();
        }
        BoundingBox bounds = localBounds_;
        const float reach = driftReach_ * (std::max)(drift_, 0.0f);
        bounds.min = bounds.min - Vector3{ reach, reach, reach };
        bounds.max = bounds.max + Vector3{ reach, reach, reach };
        return bounds.TransformBy(transform->Get().GetWorldMatrix());
    }

    // ===== 描画 =====

    // 群れ全体 → 1 匹ずつの順に視錐台で棄却し、種類ごとにまとめてインスタンシングバッチへ積む
    void FishSchoolComponent::Render(const DrawViewInfo& view)
    {
        if (instances_.empty() || !view.view || !view.view->isValid) {
            return;
        }
        if (!ModelVisibility::IsModelInView(view.view->frustum, GetWorldBoundingBox())) {
            return;
        }

        const std::vector<Matrix4x4>& worlds = UpdateWorldMatrices();
        // モーションベクターの履歴は GameView だけが持つ（Model::Draw と同じ規約）
        const bool isGameView = (view.viewType == RenderViewType::GameView);
        const uint64_t frame = Time::FrameCount();

        for (uint32_t s = 0; s < species_.size(); ++s) {
            Species& species = species_[s];
            if (!species.model) {
                continue;
            }
            const BoundingBox& modelBounds = species.model->GetModelResource()->GetLocalBoundingBox();

            drawWorlds_.clear();
            drawPrevWVPs_.clear();
            for (size_t i = 0; i < instances_.size(); ++i) {
                Fish& fish = instances_[i];
                if (fish.species != s) {
                    continue;
                }
                if (!ModelVisibility::IsModelInView(view.view->frustum, modelBounds.TransformBy(worlds[i]))) {
                    continue;
                }
                drawWorlds_.push_back(worlds[i]);
                if (isGameView) {
                    // ちょうど 1 フレーム前に描いた個体だけ前フレームの WVP を信用する
                    // （視錐台から外れていた個体の古い WVP は、嘘のモーションベクターになる）
                    const Matrix4x4 wvp = worlds[i] * view.view->viewProjection;
                    const bool hasHistory = fish.prevFrame != kNoFrame && fish.prevFrame + 1 == frame;
                    drawPrevWVPs_.push_back(hasHistory ? fish.prevWVP : wvp);
                    fish.prevWVP = wvp;
                    fish.prevFrame = frame;
                }
            }
            if (drawWorlds_.empty()) {
                continue;
            }
            species.model->DrawInstances(drawWorlds_,
                isGameView ? std::span<const Matrix4x4>(drawPrevWVPs_) : std::span<const Matrix4x4>{}, view);
        }
    }
}
