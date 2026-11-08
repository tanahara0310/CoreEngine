#pragma once

#include <d3d12.h>
#include <dxcapi.h>
#include <wrl.h>
#include <cstdint>
#include "Graphics/Light/Light.h"
#include "Graphics/Water/WaterPhysics.h"
#include "Math/Vector/Vector3.h"
#include "Math/Matrix/Matrix4x4.h"
#include "RayTracingDispatchInfo.h"
#include "RayTracingHitData.h"
#include "RayTracingOutputViewSet.h"
#include "RayTracingPassBase.h"
#include "Graphics/RayTracing/Effect/RTEffect.h"
#include "Graphics/RayTracing/Denoise/RTDenoiser.h"
#include "RayTracingPipelineBuilder.h"
#include "ShaderTableBuilder.h"

namespace CoreEngine
{
    class GraphicsCore;
    class DescriptorAllocator;
    class AccelerationStructureManager;
    class LightManager;
    struct ViewInfo;

    /// @brief DXR レイトレーシングシャドウを管理するクラス
    /// @details State Object / Shader Table / UAV テクスチャの作成と DispatchRays を担当
    /// @brief DXRシャドウのパラメータ設定
    struct RayTracingShadowSettings {
        float shadowBias = 0.05f;          ///< セルフシャドウ防止バイアス
        float maxRayDistance = 1000.0f;    ///< シャドウレイの基準射程距離

        /// @brief 射程距離を太陽高度に応じて伸ばす
        /// @details 光源が低いほどレイはほぼ水平に走るため、固定距離では遠くの遮蔽物へ届かない。
        ///          高度 10°（sin=0.174）だと 1000 のレイで稼げる高さは 174 しかなく、
        ///          朝夕の長い影が原理的に出せなかった。
        ///          有効時は maxRayDistance / max(sin(高度), 1/kMaxRayDistanceScale) を使う。
        bool  scaleRayDistanceBySunElevation = true;
        float lightRadius = 0.02f;         ///< 光源の角半径（ラジアン）
        ///< 実際の太陽: ~0.0046 rad
        ///< 0.02 = わずかにソフトな影（ペナンブラが狭くVariance Clampingが効く）
        ///< 0.15 はペナンブラが広すぎてゴーストが発生するため禁止
        int   softShadowSamples = 1;       ///< 基本のレイ本数（本影・日向は 1 で十分）
        ///< RayGen は R2 低食い違い列で時間方向に層化サンプルするため 1spp でも収束が速い

        /// @brief ペナンブラ（影の縁）で使うレイ本数
        /// @details 1spp のショットノイズ分散 p(1-p) は影の縁でしか発生しない。前フレームの
        ///          蓄積結果が中間値（縁）または蓄積が浅いピクセルだけ本数を増やすことで、
        ///          コストは縁の面積ぶんの増加で済み、縁のノイズ σ は 1/sqrt(本数) に落ちる。
        int   penumbraSamples = 8;
        ///< 高品質なソフトシャドウが必要な場合は 2〜4 程度まで増やす（GPUコストはサンプル数に比例）
        /// @brief テンポラル蓄積フレーム数の上限
        /// @details ピクセルごとの蓄積カウント N による適応ブレンド（α = 1/N）を行い、
        ///          静止時は α = 1/この値 まで収束する。旧実装の固定 α（historyAlpha）は
        ///          定常状態でも入力ノイズの sqrt(α/(2-α)) が残り続け、静止カメラでも
        ///          影のペナンブラが毎フレームちらつく主因だったため廃止した。
        ///          影の変化（ライト移動等）への追従は、再投影の深度検証と
        ///          統計的クランプの棄却でカウントがリセットされることで担保される。
        ///          クランプ帯を統計的に正しく取るようにしたので、N は実際にこの上限まで育つ
        ///          （旧実装は毎フレーム棄却されて 20 前後で頭打ちだった）。
        ///          定常残差（縁の這うようなうねり）は 1/sqrt(N) でしか減らないため、
        ///          縁の静止を優先して 64 にする。微小変化への追従は伸びるが、
        ///          大きな変化はクランプ棄却で 1 フレーム追従なので実害は小さい。
        int   maxHistoryFrames = 64;

        /// @brief A-Trous デノイズのパス数（0 = デノイズ無効）
        /// @details Stage 3 で専用の ping/pong スクラッチを 2 枚持たせたため、
        ///          「偶数のみ」という旧制約は無くなり 0〜4 の任意値が指定できる。
        ///          カーネルは 5x5 の B3 スプライン。既定 3: step 1,2,4（実効 17x17）。
        ///          4 パス目（step 8）は数値実験でほとんど改善しなかったので既定には含めない。
        int   atrousPassCount = 3;

        /// @brief トレース〜デノイズをハーフ解像度で行い、最後にバイラテラルアップサンプルする
        /// @details レイ本数・デノイズの帯域がともに 1/4 になる。最終マスク（DeferredLighting が
        ///          読む実体）はフル解像度のまま。フレームごとに 2x2 のサンプル位置を巡回させ、
        ///          テンポラル蓄積で 4 サブピクセルぶんの情報を回収する。
        bool  halfResolutionTrace = true;

        /// @brief ハーフ解像度時に 2x2 のサンプル位置をフレームごとに巡回させるか
        /// @details 巡回させると細い遮蔽物を拾えるが、影の縁では毎フレーム別のサブピクセル
        ///          （＝別の遮蔽率）をトレースすることになり、テンポラル蓄積から見た入力が
        ///          4 フレーム周期で振動する。クランプ棄却で蓄積カウントが落ち、
        ///          静止カメラでも縁が這うように動く。既定は false（中央代表点固定）。
        bool  cycleTraceOffset = false;

        /// @brief アップサンプル時の深度エッジ重み係数
        /// @details 大きいほど深度が近いトレース結果だけを採用する（＝境界がシャープになるが
        ///          採用サンプルが減ってエイリアスが出やすい）。
        float upsamplePhiDepth = 8.0f;

        /// @brief A-Trous / テンポラルの深度エッジ重み係数
        /// @details 大きいほどエッジ検出が厳しくなり（＝ぼけにくく）、小さいほど広くぼける。
        ///          Stage 1 で深度指標を「ワールド原点からの距離」から
        ///          「線形ビュー深度」へ変更したため再較正が必要になった値。
        float denoisePhiDepth = 1.0f;

        /// @brief A-Trous のエッジ停止関数の σ 倍率
        /// @details 影値の差を「その値がもつ推定標準偏差」で割って評価する（SVGF 方式）。
        ///          大きいほど「ノイズとみなしてぼかす」範囲が広がる。固定閾値ではないので、
        ///          収束したピクセルは自動的にぼけにくくなる。
        float denoisePhiShadow = 6.0f;

        /// @brief 静止化しきい値（σ の倍数、0 で無効）
        /// @details |現フレーム推定 - 履歴| が σ×この値 未満の更新は統計的にノイズと
        ///          区別できないため捨て、履歴をそのまま出力する。α=1/N の EMA が定常状態でも
        ///          動き続けることによる「影の縁の這うようなうねり」を止める最後の一段。
        float stillnessSigma = 1.25f;

        /// @brief 履歴クランプ帯の広さ（推定標準偏差の何倍まで許容するか）
        /// @details 帯 = この値 × sqrt(現フレーム推定分散 + 履歴推定分散) + 固定マージン。
        ///          小さすぎるとモンテカルロの揺らぎで履歴が棄却され、蓄積が収束しない
        ///          （旧実装の固定マージン 0.05 がまさにこれで、影の縁のノイズの主因だった）。
        ///          大きすぎると影が動いたときにゴーストが残る。
        float historyClampSigma = 3.0f;

        /// @brief 再投影した履歴を採用する相対深度しきい値
        /// @details |履歴の深度 - 現在の深度| が この値 × 深度 を超えたらディスオクルージョンとみなす。
        ///          斜面での勾配ぶんは別途シェーダー側で加算される。
        float historyDepthTolerance = 0.05f;

        /// @brief テンポラル蓄積の履歴参照を強制的に無効化する（デバッグ用）
        /// @details true にすると毎フレーム現フレームの空間前処理結果だけを使う。
        ///          ゴーストや残像がテンポラル由来かを切り分けるためのトグル。
        bool  disableHistory = false;
    };

    /// @brief 水中の受光点へ屈折して届く光の経路を求めるための水面
    /// @details enabled のとき、水域の水面より下の受光点は、水面の入射点から光源へ向けて
    ///          水より上の遮蔽だけを調べる（水中の区間は RT コースティクスが調べる）。
    struct RayTracingShadowWaterSurface {
        bool  enabled = false;
        float height = 0.0f;                          ///< 平らな水面の高さ
        float refractiveIndex = WaterPhysics::kRefractiveIndex; ///< 水の屈折率
        float regionCenterXZ[2] = { 0.0f, 0.0f };     ///< 水域の中心（XZ）
        float regionHalfExtentXZ[2] = { 0.0f, 0.0f }; ///< 水域の半径（XZ）
    };

    /// @brief 影を落とすライトの形（RTShadow.hlsl の ShadowRayConstants に詰める）
    struct RTShadowLightShape {
        LightType type = LightType::Directional;
        Vector3 direction = { 0.0f, -1.0f, 0.0f }; ///< 平行光源・スポットは光の進む向き、エリアは発光面の法線
        Vector3 position = { 0.0f, 0.0f, 0.0f };   ///< 点光源・スポット・エリアの位置
        float range = 0.0f;                        ///< 点光源・スポット・エリアの届く距離 [m]
        float sourceRadius = 0.0f;                 ///< 点光源・スポットの光る球の半径 [m]
        float cosOuterCone = -1.0f;                ///< スポットの外角の余弦
        Vector3 areaRight = { 1.0f, 0.0f, 0.0f };  ///< エリアの発光面の右向き
        Vector3 areaUp = { 0.0f, 0.0f, 1.0f };     ///< エリアの発光面の上向き
        float areaHalfWidth = 0.0f;                ///< エリアの発光面の半分の幅 [m]
        float areaHalfHeight = 0.0f;               ///< エリアの発光面の半分の高さ [m]
        uint64_t key = 0;                          ///< ライトを見分ける値（枠の前のフレームの値と違えば履歴を捨てる）
    };

    /// @brief DXR レイトレーシングシャドウ
    /// @details 影の枠ごとに RayGen が光の届いた割合を撃ち、RTDenoiser のチャンネル（枠 1 つに 1 つ）がデノイズする。
    class RayTracingShadowManager : public RayTracingPassBase, public RTEffect {
    public:
        /// @brief ビュー識別子
        enum class ViewID : uint32_t {
            GameView = 0,
            Count
        };

        static constexpr uint32_t kViewCount = static_cast<uint32_t>(ViewID::Count);
        static constexpr uint32_t kMaxDirectionalLights = 4; ///< LightManager::MAX_DIRECTIONAL_LIGHTS と合わせる
        static constexpr uint32_t kMaxLocalLights = 4;       ///< 同時に影を落とす点光源・スポット・エリアの数の上限
        static constexpr uint32_t kLocalSlotBegin = kMaxDirectionalLights; ///< 局所ライトの最初の影の枠
        /// @brief 影の枠の数（0〜3 は平行光源の正準順、4〜7 は局所ライト）
        static constexpr uint32_t kMaxShadowSlots = kMaxDirectionalLights + kMaxLocalLights;
        /// @brief 局所ライトの枠にライトが無いときの LocalShadowSlot::shaderKey
        static constexpr uint32_t kNoLocalShadowKey = 0xFFFFFFFFu;

        /// @brief 局所ライトの影の枠 1 つに割り当てたライト
        struct LocalShadowSlot {
            bool active = false;
            LightType type = LightType::Point;
            uint32_t gpuIndex = 0;                  ///< 種類ごとの GPU のライトの配列の番号
            LightHandle handle{};
            uint32_t shaderKey = kNoLocalShadowKey; ///< DeferredLighting がライトと枠を突き合わせる値（種類 << 16 | gpuIndex）
            RTShadowLightShape shape{};
        };

        /// @brief 初期化（State Object / Shader Table / UAV テクスチャの構築）
        /// @return 成功した場合 true
        bool Initialize(const RTEffectInitContext& init) override;

        /// @brief 別スレッドで作っているパイプラインの完成を待つ
        bool WaitForPipelines() override { return WaitForPipeline(); }

        /// @brief 影を落とす点光源・スポット・エリアを選び、局所ライトの枠へ割り当てる（フレームの頭で 1 回呼ぶ）
        /// @details 影を落とす設定で、届く範囲が視錐台に入っているライトから、カメラに近い順に
        ///          r.RTShadow.MaxLocalLights 灯まで選ぶ。前のフレームと同じライトは同じ枠に残す。
        void SelectLocalShadowLights(const LightManager& lights, const ViewInfo& view);

        /// @brief 局所ライトの枠（0〜kMaxLocalLights-1）に割り当てたライト
        const LocalShadowSlot& GetLocalShadowSlot(uint32_t localIndex) const;

        /// @brief シャドウレイをディスパッチする（3 ステージの最初。ここで解像度が確定する）
        /// @param sceneDepthSRV ワールド座標を復元する深度
        /// @param light 影を落とすライトの形
        /// @param invViewProj 深度復元用 View*Projection の逆行列
        /// @param width,height フル解像度。ハーフ解像度時のトレース解像度はここから導出する
        /// @param shadowSlot 影の枠（0〜kMaxShadowSlots-1）
        /// @param waterSurface 水中の受光点を屈折した経路で調べるための水面
        void Dispatch(ID3D12GraphicsCommandList* cmdList,
            D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSRV,
            D3D12_GPU_DESCRIPTOR_HANDLE normalRoughnessSRV,
            const RTShadowLightShape& light,
            const Matrix4x4& invViewProj,
            UINT width, UINT height,
            ViewID viewId = ViewID::GameView,
            uint32_t shadowSlot = 0,
            const RayTracingShadowWaterSurface& waterSurface = {},
            const RTHitTableIndices& hitTables = {});

        /// @brief 時間方向の蓄積を実行する（Dispatch の直後に呼ぶ）
        void ApplyTemporal(ID3D12GraphicsCommandList* cmdList,
            const RTDenoiseGuides& guides,
            ViewID viewId = ViewID::GameView,
            uint32_t shadowSlot = 0);

        /// @brief A-Trous とフル解像度のマスクへの仕上げを実行する（ApplyTemporal の直後に呼ぶ）
        void Denoise(ID3D12GraphicsCommandList* cmdList,
            const RTDenoiseGuides& guides,
            ViewID viewId = ViewID::GameView,
            uint32_t shadowSlot = 0);

        /// @brief 指定ビュー・ライトのシャドウ結果テクスチャの SRV を取得
        D3D12_GPU_DESCRIPTOR_HANDLE GetShadowSRVHandle(ViewID viewId = ViewID::GameView,
            uint32_t shadowSlot = 0) const;

        /// @brief 影の枠のデノイザ（デバッグ窓が中間結果を引く）
        RTDenoiser& GetDenoiser() { return denoiser_; }

        /// @brief ビューと影の枠に対応するデノイザのチャンネル
        static constexpr uint32_t GetDenoiseChannel(ViewID viewId, uint32_t shadowSlot)
        {
            return static_cast<uint32_t>(viewId) * kMaxShadowSlots + shadowSlot;
        }

        /// @brief 直近のディスパッチ診断情報を取得（全 RT パス共通の型）
        const RayTracingDispatchInfo& GetDispatchInfo(ViewID viewId = ViewID::GameView,
            uint32_t shadowSlot = 0) const;

        /// @brief 実際に適用される A-Trous パス数（0〜RTDenoiser::kMaxAtrousPassCount）
        int GetEffectiveAtrousPassCount() const;

        /// @brief トレース解像度の縮小率（1 = フル解像度 / 2 = ハーフ解像度）
        UINT GetTraceScale() const;

        /// @brief 指定ビュー・ライトのシャドウ結果テクスチャを取得する
        /// @param viewId 参照するビュー ID
        /// @param shadowSlot 参照する影の枠
        /// @return シャドウ結果テクスチャ（実体＋現在ステート）。未確保なら実体は nullptr
        GpuResource& GetShadowResource(
            ViewID viewId = ViewID::GameView,
            uint32_t shadowSlot = 0);

        /// @brief 指定ビュー・ライトで今フレームにディスパッチ済みか
        bool IsDispatchedThisFrame(ViewID viewId = ViewID::GameView,
            uint32_t shadowSlot = 0) const;

        /// @brief フレーム開始時に全ビュー・全ライトの状態をリセット
        void BeginFrame() override;

        /// @brief 生成済みの全ビュー・全ライトの出力テクスチャを新サイズで作り直す（GPU は止まっている）
        void OnResize(uint32_t width, uint32_t height) override;

        /// @brief シャドウパラメータを設定する
        /// @brief 設定を一括で反映する
        /// @details 実体は CVar（"r.RTShadow.*"）が保持するため、ここへ書くと
        ///          UI 表示・自動保存にも反映される。
        void SetSettings(const RayTracingShadowSettings& settings);

        /// @brief 現在のシャドウパラメータを取得する
        const RayTracingShadowSettings& GetSettings() const { return settings_; }

    private:
        // シェーダーバイトコード（State Object 構築後に解放するのでここに置く）
        IDxcBlob* shaderBlob_ = nullptr;  ///< 所有者は ShaderProgramCache

        static_assert(kViewCount * kMaxShadowSlots <= RTDenoiser::kMaxChannels,
            "RTDenoiser のチャンネルの数が影の枠の数に足りていない");

        /// @brief ビュー × 影の枠ごとの、RayGen の状態
        struct ShadowView {
            UINT traceOffsetX = 0;   ///< 今フレームの 2x2 サンプル位置（ハーフ解像度時のみ非ゼロ）
            UINT traceOffsetY = 0;
            bool dispatchedThisFrame = false;

            /// @brief この view × light のディスパッチ回数（2x2 サンプル位相・レイジッターの種）
            uint32_t frameCount = 0;
            RayTracingDispatchInfo dispatchInfo{}; ///< デバッグ表示用（Dispatch のたびに更新）
        };
        ShadowView views_[kViewCount][kMaxShadowSlots]{};
        LocalShadowSlot localSlots_[kMaxLocalLights]{}; ///< 局所ライトの枠に割り当てたライト

        /// @brief 光の届いた割合をデノイズする部品（影の枠 1 つに 1 チャンネル）
        RTDenoiser denoiser_;

        /// @brief 太陽高度による射程距離の最大倍率（高度 ~5.7° 相当で頭打ち）
        /// @details 際限なく伸ばすと地平線近くでトラバースが爆発するため上限を設ける。
        static constexpr float kMaxRayDistanceScale = 10.0f;

        /// @brief ライト方向から実効レイ射程を求める
        float ResolveEffectiveRayDistance(const Vector3& lightDirection) const;

        /// @brief CVar（"r.RTShadow.*"）の現在値を settings_ へ取り込む
        /// @details settings_ は内部の複数箇所から参照されるため CVar のキャッシュとして残し、
        ///          Dispatch の先頭で毎フレーム同期する（同期を怠ると設定復元が描画に反映されない）
        void SyncSettingsFromCVars();

        /// @brief settings_ からデノイザの調整値を作る
        RTDenoiseSettings MakeDenoiseSettings() const;

        // パラメータ
        RayTracingShadowSettings settings_;

        uint32_t dispatchLogCount_ = 0;
    };
}
