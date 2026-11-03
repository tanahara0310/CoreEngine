#pragma once

#include <memory>
#include <vector>

#include "GameObject/Component/Core/ObjectRef.h"
#include "Graphics/Water/System/WaterAppearanceModule.h"
#include "Graphics/Water/System/WaterCausticsModule.h"
#include "Graphics/Water/System/WaterFoamModule.h"
#include "Graphics/Water/System/WaterRayTracingModule.h"
#include "Graphics/Water/System/WaterSeaMapModule.h"
#include "Graphics/Water/System/WaterSettings.h"
#include "Graphics/Water/System/WaterShoreModule.h"
#include "Graphics/Water/System/WaterTideModule.h"
#include "Graphics/Water/System/WaterWaveModule.h"
#include "Graphics/Water/System/WaterWetnessModule.h"
#include "Graphics/Water/WaterSurfaceData.h"
#include "Math/Vector/Vector3.h"
#include "Scene/Feature/ISceneFeature.h"

namespace CoreEngine
{
    class LightManager;
    class RenderDomainContext;
    class WaterEditorPanel;
    class WaterWhitecapFoam;
    class WaterSurfaceComponent;
    struct WaterFrameBinding;
    struct WaterFrameContext;

    /// @brief 水面一式（水面オブジェクト・波・潮・岸・映り込みなどの結線）を持つ Feature
    /// @details シーンの水面のうち 1 枚を主の水（「主の水にする」の付いた水面。無ければ最初の水面）にする。
    ///          主の水は毎フレーム、時刻 → 潮 → 設定 → 水面の状態（スナップショット）→ 描画側への受け渡しの順に
    ///          各モジュールを呼び、描画側が読む水面の状態はこのスナップショット。
    ///          ほかの水面は、潮・見た目と水質・Gerstner の波・シーンの色と空の結線だけを渡す
    ///          （FFT の海・泡・岸・海の範囲・RT・濡れ跡は主の水だけ）。
    ///          結線を PostLogic で行うのは、大気散乱（EnvironmentFeature）の更新後でないと
    ///          LUT と SH の準備完了が確定しないため。
    class WaterSystem : public ISceneFeature {
    public:
        /// @brief 水面オブジェクトの生成パラメータ
        struct Config {
            /// @brief メッシュのローカル一辺長 [m]
            float size = 100.0f;
            /// @brief XZ 分割数
            /// @details FFT カスケードの波形をジオメトリで解像するには頂点間隔が
            ///          カスケード波長より十分細かい必要がある。
            ///          ローカル 100m × 分割 256 × スケール 40 = 一辺 4km / 頂点間隔 15.6m。
            uint32_t resolution = 256;
            /// @brief ワールド位置（既定の無限遠タイル床 y=0 に合わせる）
            Vector3 translate{ 0.0f, 0.0f, 0.0f };
            /// @brief ワールドスケール
            Vector3 scale{ 40.0f, 1.0f, 40.0f };
        };

        explicit WaterSystem(const Config& config = {});
        ~WaterSystem() override;

        const char* GetName() const override { return "WaterRender"; }

        void Initialize(SceneContext& ctx) override;
        void PostSceneInitialize(SceneContext& ctx) override;
        void Update(SceneContext& ctx, SceneUpdatePhase phase) override;

        /// @brief 停止中も回す
        /// @details 設定の反映と描画側への受け渡しは毎フレーム要る。
        ///          波の時刻は Time::DeltaTime() で進めるので、停止中は止まる。
        bool RunsWhileStopped() const override { return true; }
        void Finalize(SceneContext& ctx) override;

        /// @brief 主の水の水面を返す（未生成か、消えていれば nullptr）
        WaterSurfaceComponent* GetWaterPlane() const;

        /// @brief 沖の白波の泡を返す（描画側に無ければ nullptr）
        const WaterWhitecapFoam* GetWaterWhitecapFoam() const;

        /// @brief 現在の水面高さ（ワールド Y。潮位を含む）を返す
        float GetWaterHeight() const;

        /// @brief このフレームの水面の状態を返す（水面が無効なら regionValid=0）
        const WaterSurfaceData& GetWaterSurfaceState() const { return snapshot_; }

    private:
        /// @brief 水面オブジェクトをシーンから採用、無ければ生成する
        void AcquireWaterPlane(SceneContext& ctx);

        /// @brief 保存データで置かれた水面があれば、この Feature が作った水面を消してそちらを使う
        /// @note 保存データのオブジェクトは AcquireWaterPlane の時点ではまだ生まれていない。
        void AdoptSceneWaterPlane(SceneContext& ctx);

        /// @brief シーンの水面を集め、主の水とほかの水に分ける
        void CollectWaterPlanes(SceneContext& ctx);

        /// @brief 主の水とほかの水の水面を out へ並べる（主の水が先頭）
        void GatherWaterPlanes(std::vector<WaterSurfaceComponent*>& out) const;

        /// @brief ほかの水の水面へ、潮・見た目と水質・Gerstner の波・シーンの色と空の結線を渡し、
        ///        水面の状態を secondarySurfaceStates_ へ作る
        void UpdateSecondaryWaterPlanes(
            SceneContext& ctx, RenderDomainContext& domain, const LightManager* lightManager);

        /// @brief 水面の波・潮・範囲・格子の分割数を out へ書く（主の水とほかの水で共通の部分）
        void CaptureSurface(RenderDomainContext& domain, const WaterSurfaceComponent& waterPlane, WaterSurfaceData& out) const;

        /// @brief 生成直後の水面マテリアル既定値を設定する
        void ConfigureDefaultMaterial() const;

        /// @brief 水面の WaterProfile・シーンの風・プロジェクトの品質の設定から settings_ を作り直す
        const WaterSettings& RefreshSettings();

        /// @brief 水面の今の状態から snapshot_ を作り直す
        /// @param deltaSeconds 前の記録から進んだ時間（濡れ跡を乾かす）
        void CaptureSnapshot(RenderDomainContext& domain, const WaterSettings& settings, float deltaSeconds);

        /// @brief このフレームの外部リソースの結線を組み立てる
        WaterFrameBinding BuildFrameBinding(const WaterFrameContext& frame) const;

        /// @brief 結線結果の診断ログ（デバッグ表示中のみ・低頻度）
        void LogFrameDiagnostics(const WaterFrameBinding& binding) const;

        Config config_{};

        /// @brief 主の水の水面（所有権は GameObjectManager。使うたびに ID から引き直す）
        ObjectRef<WaterSurfaceComponent> waterPlane_;

        /// @brief ほかの水の水面（池・プール・川。毎フレーム集め直す）
        std::vector<ObjectRef<WaterSurfaceComponent>> secondaryWaterPlanes_;

        /// @brief 集めた水面の置き場（毎フレーム使い回す）
        std::vector<WaterSurfaceComponent*> waterPlaneScratch_;

        /// @brief この Feature が水面を作ったか（シーンが自前の水面を置いていたら引っ込める）
        bool ownsWaterPlane_ = false;

        /// @brief このフレームの設定（水面の WaterProfile・シーンの風・プロジェクトの品質の設定から作る）
        WaterSettings settings_{};

        /// @brief このフレームの主の水の状態（描画側へ渡す実体）
        WaterSurfaceData snapshot_{};

        /// @brief このフレームのほかの水の水面の状態（水面の高さの問い合わせへ渡す実体）
        std::vector<WaterSurfaceData> secondarySurfaceStates_;

        WaterWaveModule wave_{};
        WaterTideModule tide_{};
        WaterAppearanceModule appearance_{};
        WaterFoamModule foam_{};
        WaterCausticsModule caustics_{};
        WaterRayTracingModule rayTracing_{};
        WaterShoreModule shore_{};
        WaterSeaMapModule seaMap_{};
        WaterWetnessModule wetness_{};

        /// @brief 受け渡し先（Finalize で取り消すために保持。非所有）
        RenderDomainContext* publishTarget_ = nullptr;

#ifdef CORE_EDITOR
        /// @brief 水面の調整画面（水面コンポーネントのインスペクタへ出す）
        /// @details 実体はエディタ側にあるので前方宣言のまま持つ
        ///          （コンストラクタとデストラクタは .cpp で定義する）。
        std::unique_ptr<WaterEditorPanel> editorPanel_;
#endif
    };
}
