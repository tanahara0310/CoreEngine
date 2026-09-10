#include "pch.h"
#include "WaterWaveViewComponent.h"

#include "Camera/Camera.h"
#include "EngineSystem/EngineSystem.h"
#include "GameObject/GameObject.h"
#include "GameObject/Component/Render/MeshRendererComponent.h"
#include "GameObject/Component/Transform/TransformComponent.h"
#include "Graphics/Material/MaterialBase.h"
#include "Graphics/Material/MaterialInstance.h"
#include "Graphics/Pipeline/CustomShaderPipeline.h"
#include "Graphics/Primitive/PlaneMeshGenerator.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RootSignature/ShaderBinder.h"
#include "Graphics/Shader/ICustomShaderProvider.h"
#include "Graphics/Water/Surface/WaterSurfaceTypes.h"
#include "MapGeneratorComponent.h"
#include "Components/Utility/BlockModelLayout.h"
#include "Utility/FrameRate/Time.h"
#include "Utility/Logger/Logger.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <string>

#ifdef USE_IMGUI
#include "Editor/ImGui/ImGuiAll.h"
#endif

using namespace CoreEngine;

namespace GameComponents
{
    /// @brief 水面の板へ差すカスタムシェーダー一式
    /// @details 頂点シェーダーだけ独自にして、ピクセルシェーダーは既定のまま使う。
    ///          こうすると通常のモデルと同じライティング・影・フォグにそのまま乗る。
    /// @note 波の定数バッファは MaterialBase が確保・マップする1本を使い回す。
    ///       全ての板が同じ波を評価するので、板ごとにバッファを持つ必要はない。
    class WaterWaveShaderProvider final
        : public CoreEngine::ICustomShaderProvider,
          public CoreEngine::MaterialBase<WaterConstants> {
    public:
        void Initialize(ID3D12Device* device) { InitializeBuffer(device); }

        bool IsReady() const { return materialData_ != nullptr; }

        /// @brief 今フレームの波を GPU へ書き込む
        void Upload(const WaterConstants& constants) {
            if (!materialData_) { return; }
            *materialData_ = constants;
        }

        std::wstring GetVertexShaderPath() const override { return L"WaterWave.VS.hlsl"; }
        // ライティングは既定のフォワード PS に任せる（水専用の見た目は色と粗さで作る）
        std::wstring GetPixelShaderPath() const override { return L"Object3d.PS.hlsl"; }
        // 板は裏からも見える（水面より低い位置にカメラが来る）ので両面描く
        D3D12_CULL_MODE GetCullMode() const override { return D3D12_CULL_MODE_NONE; }

        void BindCustomResources(
            ID3D12GraphicsCommandList* cmdList,
            const CoreEngine::CustomShaderPipeline* pipeline) const override {
            if (!cmdList || !pipeline || !materialData_) { return; }

            EnsureResolved(pipeline);
            ShaderBinder binder(cmdList, ShaderBinder::Pipeline::Graphics);
            binder.Set(waveConstantsSlot_, GetGPUVirtualAddress());
        }

    private:
        /// @brief ルートパラメータ番号をリフレクション結果から引き当てる
        /// @note b8 という番号を C++ 側へ直書きしないこと。シェーダーを書き換えた
        ///       ときに片方だけずれて、静かに別のリソースを潰す事故になる。
        void EnsureResolved(const CoreEngine::CustomShaderPipeline* pipeline) const {
            const void* rootSignature = pipeline->GetForwardRootSignature();
            if (rootSignature == resolvedRootSignature_) { return; }

            // cbuffer 宣言のブロック名がそのままリソース名になる
            waveConstantsSlot_ = pipeline->GetRootSlot("WaterConstants");
            resolvedRootSignature_ = rootSignature;
        }

        // BindCustomResources は const 契約なので、解決結果のキャッシュは mutable で持つ
        mutable RootSlot waveConstantsSlot_{};
        mutable const void* resolvedRootSignature_ = nullptr;
    };

    /// @brief 落水カーテンの定数バッファ
    /// @note HLSL 側 WaterFall.VS.hlsl / WaterFall.PS.hlsl の cbuffer WaterFallConstants と
    ///       メモリレイアウトを一致させること。WaveParams は 32 バイト（＝float4 2 本）なので、
    ///       配列の直後から 16 バイト境界が続く。末尾の 2 ブロックもそれぞれ float4 1 本ぶん。
    struct WaterFallConstants {
        WaveParams waves[kMaxWaterWaveCount]{};
        uint32_t activeWaveCount = 0;
        float time = 0.0f;
        float surfaceY = 0.0f;
        float fallLength = 1.0f;
        float flowSpeed = 0.0f;
        float foamStrength = 1.0f;
        float patternScale = 1.0f;
        float padding = 0.0f;
    };
    static_assert(sizeof(WaterFallConstants) == sizeof(WaveParams) * kMaxWaterWaveCount + 32,
        "WaterFallConstants の並びが HLSL の cbuffer とずれている");

    /// @brief 落水カーテンへ差すカスタムシェーダー一式
    /// @details 水面と違い、ピクセルシェーダーも独自にする。
    ///          流れの筋・白泡・下端の霧散は、頂点では出せない粒度だから。
    /// @note 定数バッファは水面用とは別に 1 本持つ。中身の波は同じでも、
    ///       落差や流速は落水側にしか無いので、同じ構造体には収まらない。
    class WaterFallShaderProvider final
        : public CoreEngine::ICustomShaderProvider,
          public CoreEngine::MaterialBase<WaterFallConstants> {
    public:
        void Initialize(ID3D12Device* device) { InitializeBuffer(device); }

        bool IsReady() const { return materialData_ != nullptr; }

        void Upload(const WaterFallConstants& constants) {
            if (!materialData_) { return; }
            *materialData_ = constants;
        }

        std::wstring GetVertexShaderPath() const override { return L"WaterFall.VS.hlsl"; }
        std::wstring GetPixelShaderPath() const override { return L"WaterFall.PS.hlsl"; }
        // 奥端のカーテンはカメラへ裏面を向けるので、両面描いて PS 側で法線を向け直す
        D3D12_CULL_MODE GetCullMode() const override { return D3D12_CULL_MODE_NONE; }

        void BindCustomResources(
            ID3D12GraphicsCommandList* cmdList,
            const CoreEngine::CustomShaderPipeline* pipeline) const override {
            if (!cmdList || !pipeline || !materialData_) { return; }

            EnsureResolved(pipeline);
            ShaderBinder binder(cmdList, ShaderBinder::Pipeline::Graphics);
            binder.Set(fallConstantsSlot_, GetGPUVirtualAddress());
        }

    private:
        /// @brief ルートパラメータ番号をリフレクション結果から引き当てる
        /// @note VS と PS の両方が同じ名前で宣言しているので、反射結果は
        ///       visibility=ALL の 1 本へ統合される。番号を直書きしないこと。
        void EnsureResolved(const CoreEngine::CustomShaderPipeline* pipeline) const {
            const void* rootSignature = pipeline->GetForwardRootSignature();
            if (rootSignature == resolvedRootSignature_) { return; }

            fallConstantsSlot_ = pipeline->GetRootSlot("WaterFallConstants");
            resolvedRootSignature_ = rootSignature;
        }

        mutable RootSlot fallConstantsSlot_{};
        mutable const void* resolvedRootSignature_ = nullptr;
    };
}

namespace {
    /// @brief 水面の描画順
    /// @details RenderManager::ResetPassTypePriorities() の並びに合わせた値。
    ///          SkyBox=300 の後、ModelParticle=400・Sprite=700・UI=800 より前。
    ///          エンジン純正の水面パス（WaterSurface）と同じ位置に置いている。
    constexpr int kWaterRenderOrder = 350;

    /// @brief 板 1 マスあたりの分割数
    /// @details 1 マス 1m なら頂点間隔 0.167m。最短波長 0.95m でも 5 頂点以上で拾える。
    ///          上げるほど滑らかになるが、頂点数は 2 乗で増える。
    constexpr uint32_t kPlaneSubdivision = 6;

    /// @brief 落水カーテンの縦方向の分割数
    /// @details 縦は波を拾う必要がないので粗くてよいが、下端のフェードと
    ///          法線の揺らぎが階段状に見えない程度は要る。
    constexpr uint32_t kFallSubdivisionY = 10;

    /// @brief 板を立てるための回転量[rad]
    constexpr float kQuarterTurn = 1.57079632679489661923f;

    /// @brief 重ね合わせる Gerstner 波 1 本ぶんの設定（1 マス = 1m を基準にした値）
    struct WaterWavePreset {
        float directionX;
        float directionZ;
        float wavelength; ///< 波長[m]
        float amplitude;  ///< 振幅[m]
        float speed;      ///< 位相速度[m/s]
        float steepness;  ///< 0 で純粋な上下。大きいほど山が尖る
    };

    /// @brief 波長も向きも意図的に揃えていない。
    /// @details 揃えると位相が全マスで一致し、「水面が一斉に上下している」ように見える。
    ///          互いに素に近い波長を重ねると、繰り返しの周期が長くなって自然になる。
    /// @note 見た目に効くのは振幅そのものより「振幅 ÷ 波長」＝斜面の傾き。
    ///       高さを出さずに波を見せたいときは、振幅を上げるより波長を詰めるほうが安全。
    ///       振幅の合計は必ず水面から地面の上面までの余裕（1 マスの 0.35 倍）に収めること。
    ///       超えると波の山が岸へ乗り上げて見える。
    /// @note steepness（横ずれ）は必ず 0 にすること。
    ///       Gerstner 本来の横ずれは頂点を XZ 方向へも動かすが、ここは水域を
    ///       1 マス 1 枚の板で敷き詰めている。板の縁が自分のマスの外へはみ出すと、
    ///       隣の地面ブロックの側面を突き抜けて水が岸へ乗り上げて見える。
    ///       上下だけの単純な波にすれば、板は絶対に自分のマスから出ない。
    constexpr WaterWavePreset kWaterWavePresets[] = {
        {  0.970f,  0.242f, 3.20f, 0.048f, 0.55f, 0.0f },
        { -0.371f,  0.928f, 2.10f, 0.027f, 0.45f, 0.0f },
        {  0.707f, -0.707f, 1.35f, 0.013f, 0.65f, 0.0f },
        {  0.259f,  0.966f, 0.95f, 0.006f, 0.85f, 0.0f },
    };
    constexpr uint32_t kWaterWaveCount =
        static_cast<uint32_t>(std::size(kWaterWavePresets));
    static_assert(kWaterWaveCount <= kMaxWaterWaveCount,
        "定数バッファに入る波の本数を超えている");

    /// @brief 位置から安定した割り当てキーを作る
    /// @details ModelRenderPoolComponent と同じ方式。XZ を 0.01 単位へ量子化して
    ///          64bit へ詰める。マップ 1 マスは必ず同じ座標で描かれるので、
    ///          これで毎フレーム同じキーになる。
    std::uint64_t MakePositionKey(float worldX, float worldZ) {
        const auto x = static_cast<std::uint32_t>(
            static_cast<std::int32_t>(std::llround(worldX * 100.0)));
        const auto z = static_cast<std::uint32_t>(
            static_cast<std::int32_t>(std::llround(worldZ * 100.0)));
        return (static_cast<std::uint64_t>(x) << 32) | static_cast<std::uint64_t>(z);
    }
}

GameComponents::WaterWaveViewComponent::WaterWaveViewComponent(
    MapGeneratorComponent* mapGenerator,
    CoreEngine::Camera* viewCamera,
    float gridSize,
    uint32_t viewDistanceX,
    std::size_t initialCapacity)
    : gridSize_(gridSize),
      viewDistanceX_(viewDistanceX),
      initialCapacity_(initialCapacity),
      mapGenerator_(mapGenerator),
      viewCamera_(viewCamera) {
}

// unique_ptr が指す型の定義がここまで来ないとデストラクタを作れないので、
// 宣言はヘッダ・定義はこちらに置く
GameComponents::WaterWaveViewComponent::~WaterWaveViewComponent() = default;

json GameComponents::WaterWaveViewComponent::OnSerialize() const {
    return {
        { "gridSize", gridSize_ },
        { "viewDistanceX", viewDistanceX_ },
        { "waveHeightScale", waveHeightScale_ },
        { "waveSpeedScale", waveSpeedScale_ },
        { "waterLevelRatio", waterLevelRatio_ },
        { "waterRoughness", waterRoughness_ },
        { "waterColor", JsonManager::Vector4ToJson(waterColor_) },
        { "fallEnabled", fallEnabled_ },
        { "fallLengthRatio", fallLengthRatio_ },
        { "fallFlowSpeed", fallFlowSpeed_ },
        { "fallFoamStrength", fallFoamStrength_ }
    };
}

void GameComponents::WaterWaveViewComponent::OnDeserialize(const json& j) {
    gridSize_ = std::max(0.01f, JsonManager::SafeGet<float>(j, "gridSize", gridSize_));
    viewDistanceX_ = std::max<uint32_t>(1,
        JsonManager::SafeGet<uint32_t>(j, "viewDistanceX", viewDistanceX_));
    waveHeightScale_ = std::max(0.0f,
        JsonManager::SafeGet<float>(j, "waveHeightScale", waveHeightScale_));
    waveSpeedScale_ = std::max(0.0f,
        JsonManager::SafeGet<float>(j, "waveSpeedScale", waveSpeedScale_));
    waterLevelRatio_ = std::clamp(
        JsonManager::SafeGet<float>(j, "waterLevelRatio", waterLevelRatio_), 0.0f, 1.0f);
    waterColor_ = JsonManager::SafeGetVector4(j, "waterColor", waterColor_);
    waterRoughness_ = std::clamp(
        JsonManager::SafeGet<float>(j, "waterRoughness", waterRoughness_), 0.0f, 1.0f);
    fallEnabled_ = JsonManager::SafeGet<bool>(j, "fallEnabled", fallEnabled_);
    fallLengthRatio_ = std::max(0.01f,
        JsonManager::SafeGet<float>(j, "fallLengthRatio", fallLengthRatio_));
    fallFlowSpeed_ = std::max(0.0f,
        JsonManager::SafeGet<float>(j, "fallFlowSpeed", fallFlowSpeed_));
    fallFoamStrength_ = std::clamp(
        JsonManager::SafeGet<float>(j, "fallFoamStrength", fallFoamStrength_), 0.0f, 4.0f);
    ApplyMaterialToEntries();
}

#ifdef USE_IMGUI
bool GameComponents::WaterWaveViewComponent::DrawInspector() {
    bool changed = false;
    changed |= ImGui::DragFloat("波の高さ", &waveHeightScale_, 0.01f, 0.0f, 5.0f);
    changed |= ImGui::DragFloat("波の速さ", &waveSpeedScale_, 0.01f, 0.0f, 5.0f);
    changed |= ImGui::DragFloat("水面の高さ", &waterLevelRatio_, 0.005f, 0.0f, 1.0f);

    // || で繋ぐと短絡して後ろのウィジェットが描かれなくなるので、必ず別々に呼ぶ
    const bool colorEdited = ImGui::ColorEdit4("色", &waterColor_.x);
    const bool roughnessEdited =
        ImGui::DragFloat("粗さ", &waterRoughness_, 0.005f, 0.0f, 1.0f);
    if (colorEdited || roughnessEdited) {
        ApplyMaterialToEntries();
        changed = true;
    }

    ImGui::SeparatorText("マップ端の滝");
    changed |= ImGui::Checkbox("滝を出す", &fallEnabled_);
    changed |= ImGui::DragFloat("落差（マス）", &fallLengthRatio_, 0.05f, 0.5f, 40.0f);
    changed |= ImGui::DragFloat("流れの速さ", &fallFlowSpeed_, 0.02f, 0.0f, 20.0f);

    if (ImGui::DragFloat("泡の強さ", &fallFoamStrength_, 0.01f, 0.0f, 4.0f)) {
        changed = true;
    }

    ImGui::TextDisabled("板: 水面 %zu 枚 / 滝 %zu 枚",
        surfacePool_.entries.size(), fallPool_.entries.size());
    return changed;
}
#endif

void GameComponents::WaterWaveViewComponent::Awake() {
    GameObject* owner = GetOwner();
    EngineSystem* engine = owner ? owner->GetEngineSystem() : nullptr;
    auto* graphics = engine ? engine->GetService<GraphicsCore>() : nullptr;
    if (!graphics || !graphics->GetDevice()) {
        Logger::GetInstance().Errorf(
            LogCategory::Game,
            "WaterWaveViewComponent: 描画デバイスを取得できませんでした");
        SetEnabled(false);
        return;
    }

    shaderProvider_ = std::make_unique<WaterWaveShaderProvider>();
    shaderProvider_->Initialize(graphics->GetDevice());
    fallShaderProvider_ = std::make_unique<WaterFallShaderProvider>();
    fallShaderProvider_->Initialize(graphics->GetDevice());
    if (!shaderProvider_->IsReady() || !fallShaderProvider_->IsReady()) {
        Logger::GetInstance().Errorf(
            LogCategory::Game,
            "WaterWaveViewComponent: 波の定数バッファを確保できませんでした");
        SetEnabled(false);
        return;
    }
    UploadWaveConstants();

    // 滝が出るのは Z 両端の 2 行だけなので、水面ほどの枚数は要らない。
    // 足りなくなれば DrawFall が都度足すため、ここは初期確保の目安でよい。
    const std::size_t fallCapacity =
        (std::max)(static_cast<std::size_t>(16), initialCapacity_ / 4);

    struct PoolSetup {
        PlanePool* pool;
        PlaneKind kind;
        std::size_t capacity;
        const char* label;
    };
    const PoolSetup setups[] = {
        { &surfacePool_, PlaneKind::Surface, initialCapacity_, "水面" },
        { &fallPool_,    PlaneKind::Fall,    fallCapacity,     "滝" },
    };

    for (const PoolSetup& setup : setups) {
        setup.pool->entries.reserve(setup.capacity);
        while (setup.pool->entries.size() < setup.capacity) {
            if (!CreateEntry(*setup.pool, setup.kind)) {
                Logger::GetInstance().Errorf(
                    LogCategory::Game,
                    "WaterWaveViewComponent: {}の板の事前生成に失敗しました ({}/{})",
                    setup.label, setup.pool->entries.size(), setup.capacity);
                SetEnabled(false);
                return;
            }
        }
    }
}

void GameComponents::WaterWaveViewComponent::Update() {
    if (mapGenerator_ == nullptr || viewCamera_ == nullptr || !shaderProvider_) {
        return;
    }

    // 波の時間を進めて GPU へ送る。板は動かさず、頂点シェーダーがこの時刻で変位させる。
    elapsedTime_ += Time::DeltaTime();
    UploadWaveConstants();

    // 描画範囲の決め方は MapViewComponent と揃える（地面と水がずれた範囲で出ないように）
    const auto cameraFocusPosition = viewCamera_->GetTranslate();
    const float cameraFocusGridX = std::round(cameraFocusPosition.x / gridSize_);
    const uint32_t viewCenterX = cameraFocusGridX > 0.0f
        ? static_cast<uint32_t>(cameraFocusGridX)
        : 0;

    const std::size_t startX = (viewCenterX > viewDistanceX_)
        ? (viewCenterX - viewDistanceX_)
        : 0;
    const std::size_t endX = viewCenterX + viewDistanceX_;

    // カメラの先に必要な分だけ、X正方向へマップを延長する
    mapGenerator_->CreateToX(endX);

    const std::uint64_t frame = Time::FrameCount();
    BeginFrameIfNeeded(surfacePool_, frame);
    BeginFrameIfNeeded(fallPool_, frame);

    const auto& mapChips = mapGenerator_->GetMapChips();
    for (std::size_t x = startX; x < endX && x < mapChips.size(); ++x) {
        const std::size_t zCount = mapChips[x].size();
        for (std::size_t z = 0; z < zCount; ++z) {
            if (mapGenerator_->GetMapChip(x, z) != MapChipType::Water) {
                continue;
            }
            const float worldX = x * gridSize_;
            DrawCell(worldX, z * gridSize_, frame);

            if (!fallEnabled_) {
                continue;
            }
            // マップは Z 方向へ広がらない帯で、両端の外は地面すら無い空間。
            // そこに面した水マスからカーテンを垂らす。X 方向はカメラの先へ
            // 延び続けるので端が存在せず、ここでは見ない。
            // 幅 1 マスのマップでは両方に該当するため、else で繋がないこと。
            if (z == 0) {
                DrawFall(worldX, -0.5f * gridSize_, true, frame);
            }
            if (z + 1 == zCount) {
                DrawFall(worldX, (z + 0.5f) * gridSize_, false, frame);
            }
        }
    }

    HideUnusedEntries(surfacePool_, frame);
    HideUnusedEntries(fallPool_, frame);
}

void GameComponents::WaterWaveViewComponent::HideUnusedEntries(
    PlanePool& pool, std::uint64_t frame) {
    for (Entry& entry : pool.entries) {
        if (!entry.object || entry.object->IsMarkedForDestroy()) {
            continue;
        }
        if (entry.lastSubmittedFrame != frame && entry.object->IsActive()) {
            entry.object->SetActive(false);
        }
    }
}

void GameComponents::WaterWaveViewComponent::OnDestroy() {
    for (PlanePool* pool : { &surfacePool_, &fallPool_ }) {
        for (Entry& entry : pool->entries) {
            if (entry.object && !entry.object->IsMarkedForDestroy()) {
                entry.object->Destroy();
            }
        }
        pool->entries.clear();
        pool->entryByPosition.clear();
        pool->prevEntryByPosition.clear();
    }
}

GameComponents::WaterWaveViewComponent::Entry*
GameComponents::WaterWaveViewComponent::CreateEntry(PlanePool& pool, PlaneKind kind) {
    GameObject* owner = GetOwner();
    if (!owner || !shaderProvider_ || !fallShaderProvider_) {
        return nullptr;
    }

    GameObject* object = owner->Spawn<GameObject>();
    if (!object) {
        return nullptr;
    }

    const bool isFall = (kind == PlaneKind::Fall);
    object->SetName(owner->GetName()
        + (isFall ? "_WaterFall_" : "_WaterPlane_")
        + std::to_string(pool.entries.size()));
    object->SetSerializeEnabled(false);
    // 描画順は明示的に指定する。RenderManager::ResolveRenderOrder() は
    // ブレンド有りのモデルへ一律 +10000 するので、放っておくと
    // Model(100)+10000 = 10100 となり Sprite(700) や UI(800) より後、
    // つまりポーズメニューの手前へ水が出てしまう。
    // エンジンの水面パス（WaterSurface）と同じ 350 に置き、
    // 空（300）の後・パーティクル（400）や UI より前で描く。
    object->SetRenderOrder(kWaterRenderOrder);

    TransformComponent* transform = object->AddComponent<TransformComponent>();

    // メッシュ未指定で載せてから設定する。AddComponent の中で Awake() が走るので、
    // 先にメッシュを渡すとカスタムシェーダー指定より前に PSO が組まれてしまう。
    auto* renderer = object->AddComponent<MeshRendererComponent>();
    // ブレンド無しのモデルは Deferred 経路へ振り分けられ、その経路には
    // カスタム PSO を差す口が無い（＝波が消えて平らな板になる）。
    // ブレンドありにしてフォワード経路へ乗せることが、波を出すための前提条件。
    // 水面は α を 1 のままにするので見た目は不透明、滝だけ下端を透けさせる。
    renderer->SetBlendMode(BlendMode::kBlendModeNormal);
    renderer->SetCustomShaderProvider(isFall
        ? static_cast<CoreEngine::ICustomShaderProvider*>(fallShaderProvider_.get())
        : static_cast<CoreEngine::ICustomShaderProvider*>(shaderProvider_.get()));
    // 板はローカル 1×1。頂点変位はワールド座標で決まるので、
    // マスごとに別の板でも隣と縁の高さが必ず一致する。
    // 滝は横（ローカル X）を水面板と同じ分割数にしておくこと。分割数が違うと
    // 折れ線の頂点位置がずれ、同じ波を評価していても落ち口に隙間が開く。
    renderer->SetPrimitive(std::make_unique<PlaneMeshGenerator>(
        1.0f, 1.0f, kPlaneSubdivision,
        isFall ? kFallSubdivisionY : kPlaneSubdivision));
    renderer->ReloadFromSpec();

    object->SetActive(false);
    pool.entries.push_back({ object, transform, renderer });

    // マテリアルは MaterialComponent を介さず、モデルへ直接入れる。
    // MaterialComponent は Start() で「α が 1 ならブレンド無し」へ戻してしまい、
    // 上でせっかく指定したフォワード経路が Deferred へ落ちて波が止まるため。
    ApplyMaterialToEntry(pool.entries.back());
    return &pool.entries.back();
}

void GameComponents::WaterWaveViewComponent::BeginFrameIfNeeded(
    PlanePool& pool, std::uint64_t frame) {
    if (pool.allocationFrame == frame) {
        return;
    }
    pool.allocationFrame = frame;
    pool.nextEntryIndex = 0;
    pool.prevEntryByPosition = std::move(pool.entryByPosition);
    pool.entryByPosition.clear();
}

GameComponents::WaterWaveViewComponent::Entry*
GameComponents::WaterWaveViewComponent::AcquireEntry(
    PlanePool& pool, PlaneKind kind,
    std::uint64_t positionKey, std::uint64_t frame) {
    // 前フレームに同じ場所を描いた板を優先して使い回す（担当がずれると TAA がぶれる）
    Entry* entry = nullptr;
    if (const auto it = pool.prevEntryByPosition.find(positionKey);
        it != pool.prevEntryByPosition.end() && it->second < pool.entries.size()) {
        Entry& candidate = pool.entries[it->second];
        if (candidate.lastSubmittedFrame != frame && candidate.object &&
            !candidate.object->IsMarkedForDestroy()) {
            entry = &candidate;
        }
    }

    while (entry == nullptr && pool.nextEntryIndex < pool.entries.size()) {
        Entry& candidate = pool.entries[pool.nextEntryIndex++];
        if (candidate.lastSubmittedFrame != frame && candidate.object &&
            !candidate.object->IsMarkedForDestroy()) {
            entry = &candidate;
        }
    }

    if (entry == nullptr) {
        entry = CreateEntry(pool, kind);
    }

    if (entry == nullptr || !entry->object || !entry->transform) {
        // 生成にも失敗した場合。警告は1フレームに1回までに抑える。
        if (pool.lastExhaustedWarningFrame != frame) {
            pool.lastExhaustedWarningFrame = frame;
            Logger::GetInstance().Warnf(
                LogCategory::Game,
                "WaterWaveViewComponent: 板が足りません (kind={}, capacity={})",
                kind == PlaneKind::Fall ? "滝" : "水面", pool.entries.size());
        }
        return nullptr;
    }

    entry->lastSubmittedFrame = frame;
    entry->object->SetActive(true);
    pool.entryByPosition[positionKey] =
        static_cast<std::size_t>(entry - pool.entries.data());
    return entry;
}

void GameComponents::WaterWaveViewComponent::DrawCell(
    float worldX, float worldZ, std::uint64_t frame) {
    Entry* entry = AcquireEntry(
        surfacePool_, PlaneKind::Surface, MakePositionKey(worldX, worldZ), frame);
    if (entry == nullptr) {
        return;
    }

    auto& transform = entry->transform->Get();
    transform.translate = { worldX, GetWaterSurfaceHeight(), worldZ };
    transform.rotate = { 0.0f, 0.0f, 0.0f };
    transform.scale = { gridSize_, 1.0f, gridSize_ };
    transform.TransferMatrix();
}

void GameComponents::WaterWaveViewComponent::DrawFall(
    float worldX, float edgeZ, bool facingNegativeZ, std::uint64_t frame) {
    Entry* entry = AcquireEntry(
        fallPool_, PlaneKind::Fall, MakePositionKey(worldX, edgeZ), frame);
    if (entry == nullptr) {
        return;
    }

    // 板はローカル XZ 平面（法線 +Y）なので、X 軸まわりに 1/4 回すと立ち上がる。
    // -90 度で法線が -Z（手前端）、+90 度で +Z（奥端）を向き、
    // どちらもローカル Z がワールド Y へ写るので、縦の長さは scale.z で決まる。
    const float fallLength = GetFallLength();
    auto& transform = entry->transform->Get();
    transform.translate = {
        worldX,
        // 上端をちょうど静止水面へ合わせる。シェーダーはこの前提で
        // 「上端 0・下端 1」の落下率を求めている。
        GetWaterSurfaceHeight() - fallLength * 0.5f,
        edgeZ
    };
    transform.rotate = { facingNegativeZ ? -kQuarterTurn : kQuarterTurn, 0.0f, 0.0f };
    transform.scale = { gridSize_, 1.0f, fallLength };
    transform.TransferMatrix();
}

void GameComponents::WaterWaveViewComponent::UploadWaveConstants() {
    if (!shaderProvider_ || !fallShaderProvider_) {
        return;
    }

    WaterConstants constants{};
    constants.activeWaveCount = kWaterWaveCount;
    constants.time = elapsedTime_;
    for (uint32_t i = 0; i < kWaterWaveCount; ++i) {
        const WaterWavePreset& preset = kWaterWavePresets[i];
        WaveParams& wave = constants.waves[i];
        wave.direction = { preset.directionX, preset.directionZ };
        // 波の形はマスの大きさへ追従させる。gridSize を変えても粒度の印象が変わらない。
        wave.wavelength = preset.wavelength * gridSize_;
        wave.amplitude = preset.amplitude * gridSize_ * waveHeightScale_;
        wave.speed = preset.speed * gridSize_ * waveSpeedScale_;
        wave.steepness = preset.steepness;
        wave.phaseOffset = 0.0f;
    }

    shaderProvider_->Upload(constants);

    // 落水カーテンの上端は水面板の縁と一致していなければならない。
    // 波は必ず同じ値をそのまま渡し、落水側で作り直さないこと。
    WaterFallConstants fallConstants{};
    std::copy(std::begin(constants.waves), std::end(constants.waves),
        std::begin(fallConstants.waves));
    fallConstants.activeWaveCount = constants.activeWaveCount;
    fallConstants.time = constants.time;
    fallConstants.surfaceY = GetWaterSurfaceHeight();
    fallConstants.fallLength = GetFallLength();
    fallConstants.flowSpeed = fallFlowSpeed_ * gridSize_;
    fallConstants.foamStrength = fallFoamStrength_;
    // 模様の細かさもマスの大きさへ追従させる（波と揃える）
    fallConstants.patternScale = 1.0f / gridSize_;

    fallShaderProvider_->Upload(fallConstants);
}

void GameComponents::WaterWaveViewComponent::ApplyMaterialToEntry(Entry& entry) {
    Model* model = entry.renderer ? entry.renderer->GetModel() : nullptr;
    if (!model) {
        return;
    }

    // 落ちているのも溜まっているのと同じ水。マテリアルは水面とまったく同じ値を入れる。
    // @note 色も粗さも「落水らしく」ずらしてはいけない。水色（0, 0.35, 0.65）は
    //       赤がちょうど 0 なので、白を 0.1 混ぜるだけで拡散反射の赤が丸ごと増え、
    //       トーンマップ後には #57BFD3 → #C1DAE2 と別物になる（実測）。
    //       落水らしさは PS 側の筋・泡・下端フェードだけで付けること。
    // @note α も 1 のまま。下端のフェードは PS が自分で書き込むので、ここを下げると
    //       ForwardMain のアルファテストで先に消えてしまう。
    model->ForEachMaterial([this](MaterialInstance* material) {
        material->SetColor(waterColor_);
        material->SetMetallic(0.0f);
        material->SetRoughness(waterRoughness_);
        // ディザリングが有効だと半透明にしたときアルファテスト扱いになる。
        // ここは常にブレンドで出したいので切っておく。
        material->SetDitheringEnabled(false);
    });
}

void GameComponents::WaterWaveViewComponent::ApplyMaterialToEntries() {
    for (PlanePool* pool : { &surfacePool_, &fallPool_ }) {
        for (Entry& entry : pool->entries) {
            if (!entry.object || entry.object->IsMarkedForDestroy()) {
                continue;
            }
            ApplyMaterialToEntry(entry);
        }
    }
}

float GameComponents::WaterWaveViewComponent::GetWaterSurfaceHeight() const {
    // 地面ブロックは [底面, 底面+1マス] を占める。その途中に静止水面を置く。
    return BlockModelLayout::GetGroundHeight(gridSize_) + waterLevelRatio_ * gridSize_;
}

float GameComponents::WaterWaveViewComponent::GetFallLength() const {
    return (std::max)(fallLengthRatio_ * gridSize_, 0.01f);
}
