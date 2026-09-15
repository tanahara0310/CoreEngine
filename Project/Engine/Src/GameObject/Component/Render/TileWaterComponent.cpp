#include "pch.h"
#include "TileWaterComponent.h"

#include "EngineSystem/EngineSystem.h"
#include "GameObject/GameObject.h"
#include "GameObject/Component/Core/ComponentFactory.h"
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
#include "Utility/FrameRate/Time.h"
#include "Utility/Logger/Logger.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <string>

#ifdef USE_IMGUI
#include "Editor/ImGui/ImGuiAll.h"
#endif

COMPONENT_REGISTER(CoreEngine::TileWaterComponent)

namespace CoreEngine
{
    /// @brief 水面の板へ差すカスタムシェーダー一式
    /// @details 頂点シェーダーだけ独自にして、ピクセルシェーダーは既定のまま使う。
    ///          こうすると通常のモデルと同じライティング・影・フォグにそのまま乗る。
    /// @note 波の定数バッファは MaterialBase が確保・マップする1本を使い回す。
    ///       全ての板が同じ波を評価するので、板ごとにバッファを持つ必要はない。
    class TileWaterWaveShaderProvider final
        : public ICustomShaderProvider,
          public MaterialBase<WaterConstants> {
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
            const CustomShaderPipeline* pipeline) const override {
            if (!cmdList || !pipeline || !materialData_) { return; }

            EnsureResolved(pipeline);
            ShaderBinder binder(cmdList, ShaderBinder::Pipeline::Graphics);
            binder.Set(waveConstantsSlot_, GetGPUVirtualAddress());
        }

    private:
        /// @brief ルートパラメータ番号をリフレクション結果から引き当てる
        /// @note b8 という番号を C++ 側へ直書きしないこと。シェーダーを書き換えた
        ///       ときに片方だけずれて、静かに別のリソースを潰す事故になる。
        void EnsureResolved(const CustomShaderPipeline* pipeline) const {
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
    struct TileWaterFallConstants {
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
    static_assert(sizeof(TileWaterFallConstants) == sizeof(WaveParams) * kMaxWaterWaveCount + 32,
        "TileWaterFallConstants の並びが HLSL の cbuffer とずれている");

    /// @brief 落水カーテンへ差すカスタムシェーダー一式
    /// @details 水面と違い、ピクセルシェーダーも独自にする。
    ///          流れの筋・白泡・下端の霧散は、頂点では出せない粒度だから。
    /// @note 定数バッファは水面用とは別に 1 本持つ。中身の波は同じでも、
    ///       落差や流速は落水側にしか無いので、同じ構造体には収まらない。
    class TileWaterFallShaderProvider final
        : public ICustomShaderProvider,
          public MaterialBase<TileWaterFallConstants> {
    public:
        void Initialize(ID3D12Device* device) { InitializeBuffer(device); }

        bool IsReady() const { return materialData_ != nullptr; }

        void Upload(const TileWaterFallConstants& constants) {
            if (!materialData_) { return; }
            *materialData_ = constants;
        }

        std::wstring GetVertexShaderPath() const override { return L"WaterFall.VS.hlsl"; }
        std::wstring GetPixelShaderPath() const override { return L"WaterFall.PS.hlsl"; }
        // 奥端のカーテンはカメラへ裏面を向けるので、両面描いて PS 側で法線を向け直す
        D3D12_CULL_MODE GetCullMode() const override { return D3D12_CULL_MODE_NONE; }

        void BindCustomResources(
            ID3D12GraphicsCommandList* cmdList,
            const CustomShaderPipeline* pipeline) const override {
            if (!cmdList || !pipeline || !materialData_) { return; }

            EnsureResolved(pipeline);
            ShaderBinder binder(cmdList, ShaderBinder::Pipeline::Graphics);
            binder.Set(fallConstantsSlot_, GetGPUVirtualAddress());
        }

    private:
        /// @brief ルートパラメータ番号をリフレクション結果から引き当てる
        /// @note VS と PS の両方が同じ名前で宣言しているので、反射結果は
        ///       visibility=ALL の 1 本へ統合される。番号を直書きしないこと。
        void EnsureResolved(const CustomShaderPipeline* pipeline) const {
            const void* rootSignature = pipeline->GetForwardRootSignature();
            if (rootSignature == resolvedRootSignature_) { return; }

            fallConstantsSlot_ = pipeline->GetRootSlot("WaterFallConstants");
            resolvedRootSignature_ = rootSignature;
        }

        mutable RootSlot fallConstantsSlot_{};
        mutable const void* resolvedRootSignature_ = nullptr;
    };

    namespace {
        /// @brief 水面の描画順
        /// @details RenderManager::ResetPassTypePriorities() の並びに合わせた値。
        ///          SkyBox=300 の後、ModelParticle=400・Sprite=700・UI=800 より前。
        ///          エンジン純正の水面パス（WaterSurface）と同じ位置に置いている。
        constexpr int kWaterRenderOrder = 350;

        /// @brief 板 1 枚あたりの分割数
        /// @details 1 枚 1m なら頂点間隔 0.167m。最短波長 0.95m でも 5 頂点以上で拾える。
        ///          上げるほど滑らかになるが、頂点数は 2 乗で増える。
        constexpr uint32_t kPlaneSubdivision = 6;

        /// @brief 落水カーテンの縦方向の分割数
        /// @details 縦は波を拾う必要がないので粗くてよいが、下端のフェードと
        ///          法線の揺らぎが階段状に見えない程度は要る。
        constexpr uint32_t kFallSubdivisionY = 10;

        /// @brief 板を立てるための回転量[rad]
        constexpr float kQuarterTurn = 1.57079632679489661923f;

        /// @brief 重ね合わせる Gerstner 波 1 本ぶんの設定（板 1 枚 = 1m を基準にした値）
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
        /// @note steepness（横ずれ）は必ず 0 にすること。
        ///       Gerstner 本来の横ずれは頂点を XZ 方向へも動かすが、ここは水域を
        ///       1 マス 1 枚の板で敷き詰めている。板の縁が自分のマスの外へはみ出すと、
        ///       隣の地面ブロックの側面を突き抜けて水が岸へ乗り上げて見える。
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
        ///          64bit へ詰める。
        std::uint64_t MakePositionKey(float worldX, float worldZ) {
            const auto x = static_cast<std::uint32_t>(
                static_cast<std::int32_t>(std::llround(worldX * 100.0)));
            const auto z = static_cast<std::uint32_t>(
                static_cast<std::int32_t>(std::llround(worldZ * 100.0)));
            return (static_cast<std::uint64_t>(x) << 32) | static_cast<std::uint64_t>(z);
        }
    }

    TileWaterComponent::TileWaterComponent() = default;

    TileWaterComponent::TileWaterComponent(std::size_t initialCapacity)
        : initialCapacity_(initialCapacity) {
    }

    TileWaterComponent::~TileWaterComponent() = default;

    json TileWaterComponent::OnSerialize() const {
        return {
            { "tileSize", tileSize_ },
            { "surfaceHeight", surfaceHeight_ },
            { "initialCapacity", initialCapacity_ },
            { "waveHeightScale", waveHeightScale_ },
            { "waveSpeedScale", waveSpeedScale_ },
            { "waterRoughness", waterRoughness_ },
            { "waterColor", JsonManager::Vector4ToJson(waterColor_) },
            { "fallLengthRatio", fallLengthRatio_ },
            { "fallFlowSpeed", fallFlowSpeed_ },
            { "fallFoamStrength", fallFoamStrength_ }
        };
    }

    void TileWaterComponent::OnDeserialize(const json& j) {
        tileSize_ = std::max(0.01f, JsonManager::SafeGet<float>(j, "tileSize", tileSize_));
        surfaceHeight_ = JsonManager::SafeGet<float>(j, "surfaceHeight", surfaceHeight_);
        initialCapacity_ = std::max<std::size_t>(1,
            JsonManager::SafeGet<std::size_t>(j, "initialCapacity", initialCapacity_));
        waveHeightScale_ = std::max(0.0f,
            JsonManager::SafeGet<float>(j, "waveHeightScale", waveHeightScale_));
        waveSpeedScale_ = std::max(0.0f,
            JsonManager::SafeGet<float>(j, "waveSpeedScale", waveSpeedScale_));
        waterColor_ = JsonManager::SafeGetVector4(j, "waterColor", waterColor_);
        waterRoughness_ = std::clamp(
            JsonManager::SafeGet<float>(j, "waterRoughness", waterRoughness_), 0.0f, 1.0f);
        fallLengthRatio_ = std::max(0.01f,
            JsonManager::SafeGet<float>(j, "fallLengthRatio", fallLengthRatio_));
        fallFlowSpeed_ = std::max(0.0f,
            JsonManager::SafeGet<float>(j, "fallFlowSpeed", fallFlowSpeed_));
        fallFoamStrength_ = std::clamp(
            JsonManager::SafeGet<float>(j, "fallFoamStrength", fallFoamStrength_), 0.0f, 4.0f);
        ApplyMaterialToEntries();
    }

#ifdef USE_IMGUI
    bool TileWaterComponent::DrawInspector() {
        bool changed = false;
        changed |= ImGui::DragFloat("板の大きさ", &tileSize_, 0.01f, 0.01f, 10.0f);
        changed |= ImGui::DragFloat("水面の高さ", &surfaceHeight_, 0.005f);
        changed |= ImGui::DragFloat("波の高さ", &waveHeightScale_, 0.01f, 0.0f, 5.0f);
        changed |= ImGui::DragFloat("波の速さ", &waveSpeedScale_, 0.01f, 0.0f, 5.0f);

        // || で繋ぐと短絡して後ろのウィジェットが描かれなくなるので、必ず別々に呼ぶ
        const bool colorEdited = ImGui::ColorEdit4("色", &waterColor_.x);
        const bool roughnessEdited =
            ImGui::DragFloat("粗さ", &waterRoughness_, 0.005f, 0.0f, 1.0f);
        if (colorEdited || roughnessEdited) {
            ApplyMaterialToEntries();
            changed = true;
        }

        ImGui::SeparatorText("落水カーテン");
        changed |= ImGui::DragFloat("落差（板）", &fallLengthRatio_, 0.05f, 0.5f, 40.0f);
        changed |= ImGui::DragFloat("流れの速さ", &fallFlowSpeed_, 0.02f, 0.0f, 20.0f);
        changed |= ImGui::DragFloat("泡の強さ", &fallFoamStrength_, 0.01f, 0.0f, 4.0f);

        ImGui::TextDisabled("板: 水面 %zu 枚 / 滝 %zu 枚",
            surfacePool_.entries.size(), fallPool_.entries.size());
        return changed;
    }
#endif

    void TileWaterComponent::Awake() {
        GameObject* owner = GetOwner();
        EngineSystem* engine = owner ? owner->GetEngineSystem() : nullptr;
        auto* graphics = engine ? engine->GetService<GraphicsCore>() : nullptr;
        if (!graphics || !graphics->GetDevice()) {
            Logger::GetInstance().Errorf(
                LogCategory::Graphics,
                "TileWaterComponent: 描画デバイスを取得できませんでした");
            SetEnabled(false);
            return;
        }

        shaderProvider_ = std::make_unique<TileWaterWaveShaderProvider>();
        shaderProvider_->Initialize(graphics->GetDevice());
        fallShaderProvider_ = std::make_unique<TileWaterFallShaderProvider>();
        fallShaderProvider_->Initialize(graphics->GetDevice());
        if (!shaderProvider_->IsReady() || !fallShaderProvider_->IsReady()) {
            Logger::GetInstance().Errorf(
                LogCategory::Graphics,
                "TileWaterComponent: 波の定数バッファを確保できませんでした");
            SetEnabled(false);
            return;
        }
        UploadWaveConstants();

        // 足りなくなれば DrawFall が都度足すため、滝の板は初期確保の目安でよい
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
                        LogCategory::Graphics,
                        "TileWaterComponent: {}の板の事前生成に失敗しました ({}/{})",
                        setup.label, setup.pool->entries.size(), setup.capacity);
                    SetEnabled(false);
                    return;
                }
            }
        }
    }

    void TileWaterComponent::Update() {
        if (!shaderProvider_ || !fallShaderProvider_) {
            return;
        }

        const std::uint64_t frame = Time::FrameCount();
        BeginFrameIfNeeded(surfacePool_, frame);
        BeginFrameIfNeeded(fallPool_, frame);

        // 板は動かさず、頂点シェーダーがこの時刻で変位させる
        elapsedTime_ += Time::DeltaTime();
        UploadWaveConstants();

        HideUnusedEntries(surfacePool_, frame);
        HideUnusedEntries(fallPool_, frame);
    }

    void TileWaterComponent::OnDestroy() {
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

    void TileWaterComponent::DrawSurface(float worldX, float worldZ) {
        if (!shaderProvider_) {
            return;
        }
        const std::uint64_t frame = Time::FrameCount();
        BeginFrameIfNeeded(surfacePool_, frame);
        Entry* entry = AcquireEntry(
            surfacePool_, PlaneKind::Surface, MakePositionKey(worldX, worldZ), frame);
        if (entry == nullptr) {
            return;
        }

        auto& transform = entry->transform->Get();
        transform.translate = { worldX, surfaceHeight_, worldZ };
        transform.rotate = { 0.0f, 0.0f, 0.0f };
        transform.scale = { tileSize_, 1.0f, tileSize_ };
        transform.TransferMatrix();
    }

    void TileWaterComponent::DrawFall(float worldX, float edgeZ, bool facingNegativeZ) {
        if (!fallShaderProvider_) {
            return;
        }
        const std::uint64_t frame = Time::FrameCount();
        BeginFrameIfNeeded(fallPool_, frame);
        Entry* entry = AcquireEntry(
            fallPool_, PlaneKind::Fall, MakePositionKey(worldX, edgeZ), frame);
        if (entry == nullptr) {
            return;
        }

        // 板はローカル XZ 平面（法線 +Y）なので、X 軸まわりに 1/4 回すと立ち上がる。
        // -90 度で法線が -Z、+90 度で +Z を向き、
        // どちらもローカル Z がワールド Y へ写るので、縦の長さは scale.z で決まる。
        const float fallLength = GetFallLength();
        auto& transform = entry->transform->Get();
        transform.translate = {
            worldX,
            // 上端をちょうど静止水面へ合わせる。シェーダーはこの前提で
            // 「上端 0・下端 1」の落下率を求めている。
            surfaceHeight_ - fallLength * 0.5f,
            edgeZ
        };
        transform.rotate = { facingNegativeZ ? -kQuarterTurn : kQuarterTurn, 0.0f, 0.0f };
        transform.scale = { tileSize_, 1.0f, fallLength };
        transform.TransferMatrix();
    }

    void TileWaterComponent::SetTileSize(float size) {
        tileSize_ = std::max(0.01f, size);
    }

    void TileWaterComponent::SetWaveHeightScale(float scale) {
        waveHeightScale_ = std::max(0.0f, scale);
    }

    void TileWaterComponent::SetWaveSpeedScale(float scale) {
        waveSpeedScale_ = std::max(0.0f, scale);
    }

    void TileWaterComponent::SetWaterMaterial(const Vector4& color, float roughness) {
        waterColor_ = color;
        waterRoughness_ = std::clamp(roughness, 0.0f, 1.0f);
        ApplyMaterialToEntries();
    }

    void TileWaterComponent::SetFallLengthRatio(float ratio) {
        fallLengthRatio_ = std::max(0.01f, ratio);
    }

    void TileWaterComponent::SetFallFlowSpeed(float speed) {
        fallFlowSpeed_ = std::max(0.0f, speed);
    }

    void TileWaterComponent::SetFallFoamStrength(float strength) {
        fallFoamStrength_ = std::clamp(strength, 0.0f, 4.0f);
    }

    void TileWaterComponent::HideUnusedEntries(PlanePool& pool, std::uint64_t frame) {
        for (Entry& entry : pool.entries) {
            if (!entry.object || entry.object->IsMarkedForDestroy()) {
                continue;
            }
            if (entry.lastSubmittedFrame != frame && entry.object->IsActive()) {
                entry.object->SetActive(false);
            }
        }
    }

    TileWaterComponent::Entry* TileWaterComponent::CreateEntry(PlanePool& pool, PlaneKind kind) {
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
            ? static_cast<ICustomShaderProvider*>(fallShaderProvider_.get())
            : static_cast<ICustomShaderProvider*>(shaderProvider_.get()));
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

    void TileWaterComponent::BeginFrameIfNeeded(PlanePool& pool, std::uint64_t frame) {
        if (pool.allocationFrame == frame) {
            return;
        }
        pool.allocationFrame = frame;
        pool.nextEntryIndex = 0;
        pool.prevEntryByPosition = std::move(pool.entryByPosition);
        pool.entryByPosition.clear();
    }

    TileWaterComponent::Entry* TileWaterComponent::AcquireEntry(
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
                    LogCategory::Graphics,
                    "TileWaterComponent: 板が足りません (kind={}, capacity={})",
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

    void TileWaterComponent::UploadWaveConstants() {
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
            // 波の形は板の大きさへ追従させる
            wave.wavelength = preset.wavelength * tileSize_;
            wave.amplitude = preset.amplitude * tileSize_ * waveHeightScale_;
            wave.speed = preset.speed * tileSize_ * waveSpeedScale_;
            wave.steepness = preset.steepness;
            wave.phaseOffset = 0.0f;
        }

        shaderProvider_->Upload(constants);

        // 落水カーテンの上端は水面板の縁と一致していなければならない。
        // 波は必ず同じ値をそのまま渡し、落水側で作り直さないこと。
        TileWaterFallConstants fallConstants{};
        std::copy(std::begin(constants.waves), std::end(constants.waves),
            std::begin(fallConstants.waves));
        fallConstants.activeWaveCount = constants.activeWaveCount;
        fallConstants.time = constants.time;
        fallConstants.surfaceY = surfaceHeight_;
        fallConstants.fallLength = GetFallLength();
        fallConstants.flowSpeed = fallFlowSpeed_ * tileSize_;
        fallConstants.foamStrength = fallFoamStrength_;
        // 模様の細かさも板の大きさへ追従させる（波と揃える）
        fallConstants.patternScale = 1.0f / tileSize_;

        fallShaderProvider_->Upload(fallConstants);
    }

    void TileWaterComponent::ApplyMaterialToEntry(Entry& entry) {
        Model* model = entry.renderer ? entry.renderer->GetModel() : nullptr;
        if (!model) {
            return;
        }

        // 水面と落水カーテンに同じ色・粗さを入れる。落水らしさは PS 側の筋・泡・下端フェードで付ける。
        // α は 1 のまま。下端のフェードは PS が自分で書き込む。
        model->ForEachMaterial([this](MaterialInstance* material) {
            material->SetColor(waterColor_);
            material->SetMetallic(0.0f);
            material->SetRoughness(waterRoughness_);
            // ディザリングが有効だと半透明にしたときアルファテスト扱いになる。
            // ここは常にブレンドで出したいので切っておく。
            material->SetDitheringEnabled(false);
        });
    }

    void TileWaterComponent::ApplyMaterialToEntries() {
        for (PlanePool* pool : { &surfacePool_, &fallPool_ }) {
            for (Entry& entry : pool->entries) {
                if (!entry.object || entry.object->IsMarkedForDestroy()) {
                    continue;
                }
                ApplyMaterialToEntry(entry);
            }
        }
    }

    float TileWaterComponent::GetFallLength() const {
        return (std::max)(fallLengthRatio_ * tileSize_, 0.01f);
    }
}
