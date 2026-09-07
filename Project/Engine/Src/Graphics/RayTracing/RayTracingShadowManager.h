#pragma once

#include <d3d12.h>
#include <dxcapi.h>
#include <wrl.h>
#include <cstdint>
#include "Math/Vector/Vector3.h"
#include "Math/Matrix/Matrix4x4.h"
#include "RayTracingDispatchInfo.h"
#include "RayTracingOutputViewSet.h"
#include "RayTracingPassBase.h"
#include "RayTracingPipelineBuilder.h"
#include "ShaderTableBuilder.h"

namespace CoreEngine
{
    class GraphicsCore;
    class DescriptorAllocator;
    class AccelerationStructureManager;

    /// @brief DXR レイトレーシングシャドウを管理するクラス
    /// @details State Object / Shader Table / UAV テクスチャの作成と DispatchRays を担当
    ///          GameView / ReflectionView など View ごとに独立した結果を保持できるようにする
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

    /// @brief DXR レイトレーシングシャドウ
    /// @details 出力テクスチャ・ガード判定・DXR オブジェクトは基盤 RayTracingPassBase 側が持つ。
    ///          ここに残るのは RayGen / テンポラル / A-Trous の 3 ステージと、view × ライトの状態だけ。
    class RayTracingShadowManager : public RayTracingPassBase {
    public:
        /// @brief ビュー識別子
        enum class ViewID : uint32_t {
            GameView = 0,
            ReflectionView = 1,
            Count
        };

        static constexpr uint32_t kViewCount = static_cast<uint32_t>(ViewID::Count);
        static constexpr uint32_t kMaxDirectionalLights = 4; ///< LightManager::MAX_DIRECTIONAL_LIGHTS と合わせる

        /// @brief 初期化（State Object / Shader Table / UAV テクスチャの構築）
        /// @return 成功した場合 true
        bool Initialize(GraphicsCore* dxCommon, DescriptorAllocator* descriptorAllocator,
            AccelerationStructureManager* asMgr,
            ShaderProgramCache* shaderProgramCache);

        /// @brief シャドウレイをディスパッチする（3 ステージの最初。ここで解像度が確定する）
        /// @param lightIndex ディレクショナルライトのインデックス（0〜kMaxDirectionalLights-1）
        /// @param sceneDepthSRV WorldPosition ターゲット廃止に伴い深度から復元する
        /// @param invViewProj 深度復元用 View*Projection の逆行列
        /// @param width,height フル解像度。ハーフ解像度時のトレース解像度はここから導出する
        void Dispatch(ID3D12GraphicsCommandList* cmdList,
            D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSRV,
            D3D12_GPU_DESCRIPTOR_HANDLE normalRoughnessSRV,
            const Vector3& lightDirection,
            const Matrix4x4& invViewProj,
            UINT width, UINT height,
            ViewID viewId = ViewID::GameView,
            uint32_t lightIndex = 0);

        /// @brief 空間前処理＋テンポラル蓄積パスを実行する（Dispatch の直後に呼ぶ）
        /// @param projection 投影行列。深度重みの線形化にのみ使う（Stage 1 で invViewProj から変更）
        /// @note 解像度は Dispatch が確定させたものを使うので受け取らない
        void ApplyTemporal(ID3D12GraphicsCommandList* cmdList,
            D3D12_GPU_DESCRIPTOR_HANDLE normalRoughnessSRV,
            D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSRV,
            D3D12_GPU_DESCRIPTOR_HANDLE motionVectorSRV,
            const Matrix4x4& projection,
            ViewID viewId = ViewID::GameView,
            uint32_t lightIndex = 0);

        /// @brief A-Trous デノイズ＋フル解像度への解決を実行する（ApplyTemporal の直後に呼ぶ）
        /// @param projection 投影行列。深度重みの線形化にのみ使う（Stage 1 で invViewProj から変更）
        /// @note 解像度は Dispatch が確定させたものを使うので受け取らない
        void Denoise(ID3D12GraphicsCommandList* cmdList,
            D3D12_GPU_DESCRIPTOR_HANDLE normalRoughnessSRV,
            D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSRV,
            const Matrix4x4& projection,
            ViewID viewId = ViewID::GameView,
            uint32_t lightIndex = 0);

        /// @brief 指定ビュー・ライトのシャドウ結果テクスチャの SRV を取得
        D3D12_GPU_DESCRIPTOR_HANDLE GetShadowSRVHandle(ViewID viewId = ViewID::GameView,
            uint32_t lightIndex = 0) const;

        // ──────────────────────────────────────────────────────────
        // デバッグ表示用（Stage 0: RayTracingDebugPanel が参照する）
        // ──────────────────────────────────────────────────────────

        /// @brief RayGen が書いた生マスク（デノイズ前）の SRV を取得
        /// @details 専用の Raw スロットなので、A-Trous を何パス回しても上書きされない。
        D3D12_GPU_DESCRIPTOR_HANDLE GetRawShadowSRVHandle(ViewID viewId = ViewID::GameView,
            uint32_t lightIndex = 0) const;

        /// @brief テンポラル蓄積の履歴テクスチャ（今フレームの書き込み先）の SRV を取得
        D3D12_GPU_DESCRIPTOR_HANDLE GetHistorySRVHandle(ViewID viewId = ViewID::GameView,
            uint32_t lightIndex = 0) const;

        /// @brief 直近のディスパッチ診断情報を取得（全 RT パス共通の型）
        const RayTracingDispatchInfo& GetDispatchInfo(ViewID viewId = ViewID::GameView,
            uint32_t lightIndex = 0) const;

        /// @brief 実際に適用される A-Trous パス数（0〜kMaxAtrousPassCount へのクランプ後）
        int GetEffectiveAtrousPassCount() const;

        /// @brief トレース解像度の縮小率（1 = フル解像度 / 2 = ハーフ解像度）
        UINT GetTraceScale() const;

        /// @brief 中間バッファ（生マスク・履歴）を ImGui で表示することを要求する
        /// @details そのままでは UAV / NON_PIXEL_SHADER_RESOURCE のままなので、
        ///          要求すると PrepareDebugViews が PIXEL_SHADER_RESOURCE へ遷移させる。
        /// @note 1 フレーム限りの要求（PrepareDebugViews が消費してクリアする）
        void RequestDebugViewTransition() { debugViewRequested_ = true; }

        /// @brief RequestDebugViewTransition の要求を消費し、中間バッファを表示可能状態へ遷移させる
        /// @details RT シャドウの全ステージ完了後（Denoise パスの末尾）に呼ぶ。
        void PrepareDebugViews(ID3D12GraphicsCommandList* cmdList);

        /// @brief 指定ビュー・ライトのシャドウ結果テクスチャを取得する
        /// @param viewId 参照するビュー ID
        /// @param lightIndex 参照するディレクショナルライト番号
        /// @return シャドウ結果テクスチャ（実体＋現在ステート）。未確保なら実体は nullptr
        GpuResource& GetShadowResource(
            ViewID viewId = ViewID::GameView,
            uint32_t lightIndex = 0);

        /// @brief 指定ビュー・ライトで今フレームにディスパッチ済みか
        bool IsDispatchedThisFrame(ViewID viewId = ViewID::GameView,
            uint32_t lightIndex = 0) const;

        /// @brief フレーム開始時に全ビュー・全ライトの状態をリセット
        void ResetFrameState();

        /// @brief 出力テクスチャを指定サイズで確保する
        void Resize(UINT width, UINT height, ViewID viewId = ViewID::GameView,
            uint32_t lightIndex = 0);

        /// @brief 生成済みの全ビュー・全ライトの出力テクスチャを新サイズで再作成する
        /// @details ウィンドウリサイズ時に呼ぶ。Dispatch 中の遅延再作成に任せると、
        ///          フレーム先頭で Blackboard に登録済みの旧リソースポインタが
        ///          グラフ実行中に解放され、後続パスのバリアが解放済みリソースを
        ///          参照してしまう（D3D12 ERROR #524）
        void ResizeAllExisting(UINT width, UINT height);

        /// @brief シャドウパラメータを設定する
        /// @brief 設定を一括で反映する
        /// @details 実体は CVar（"r.RTShadow.*"）が保持するため、ここへ書くと
        ///          UI 表示・自動保存にも反映される。設計: Docs/Engine/Editor/CVar_Design.md
        void SetSettings(const RayTracingShadowSettings& settings);

        /// @brief 現在のシャドウパラメータを取得する
        const RayTracingShadowSettings& GetSettings() const { return settings_; }

    private:
        /// @brief (view, light) の全テクスチャを確保する
        /// @param width,height           フル解像度（Mask のサイズ）
        /// @param traceWidth,traceHeight トレース解像度（Mask 以外のサイズ）
        /// @param traceScale             フル / トレースの比
        bool EnsureOutputTexture(UINT width, UINT height, UINT traceWidth, UINT traceHeight,
            UINT traceScale, uint32_t viewIndex, uint32_t lightIndex);

        // dxCommon_ / descriptorAllocator_ / asMgr_ / globalRootSigMgr_ / stateObject_ /
        // stateObjectProperties_ / shaderTableBuilder_ / outputViews_ / isInitialized_ は
        // 全て RayTracingPassBase が持つ（Stage 2c で重複を削除した）。

        // シェーダーバイトコード（State Object 構築後に解放するのでここに置く）
        IDxcBlob* shaderBlob_ = nullptr;  ///< 所有者は ShaderProgramCache

        /// @brief 1 つの (view, light) が持つテクスチャの用途
        /// @details 実体は共通基盤の RayTracingOutputViewSet が持ち、
        ///          ここではスロット番号への写像だけを定義する（Stage 2c）。
        ///          Mask 以外は「トレース解像度」（ハーフ解像度時はフルの半分）で確保する。
        enum class TextureSlot : uint32_t {
            Mask = 0,        ///< 最終シャドウマスク（フル解像度・DeferredLighting が読む）
            Raw = 1,         ///< RayGen の生出力（デバッグ表示用に温存する）
            DenoiseA = 2,    ///< A-Trous の ping
            DenoiseB = 3,    ///< A-Trous の pong
            HistoryA = 4,    ///< テンポラル履歴（偶数フレーム）
            HistoryB = 5,    ///< テンポラル履歴（奇数フレーム）
            Count
        };
        static constexpr uint32_t kTextureSlotCount = static_cast<uint32_t>(TextureSlot::Count);

        /// @brief (view, light, 用途) を RayTracingOutputViewSet の平坦なスロット番号へ写す
        static constexpr uint32_t MakeSlotIndex(uint32_t viewIndex, uint32_t lightIndex, TextureSlot slot)
        {
            return ((viewIndex * kMaxDirectionalLights) + lightIndex) * kTextureSlotCount
                + static_cast<uint32_t>(slot);
        }
        static_assert(kViewCount * kMaxDirectionalLights * kTextureSlotCount
            <= RayTracingOutputViewSet::kMaxSlotCount,
            "RayTracingOutputViewSet::kMaxSlotCount が RT シャドウの必要数に足りていない");

        /// @brief ビュー × ライトごとの、テクスチャ以外の状態
        struct ShadowView {
            UINT width = 0;          ///< フル解像度（Mask のサイズ）
            UINT height = 0;
            UINT traceWidth = 0;     ///< トレース解像度（Mask 以外のサイズ）
            UINT traceHeight = 0;
            UINT traceScale = 1;     ///< フル / トレースの比（1 or 2）
            UINT traceOffsetX = 0;   ///< 今フレームの 2x2 サンプル位置（ハーフ解像度時のみ非ゼロ）
            UINT traceOffsetY = 0;
            bool dispatchedThisFrame = false;
            bool isHistoryValid = false;   ///< 履歴テクスチャが初回フレーム書き込み済みか
            uint32_t historyParity = 0;    ///< 今フレームの書き込み先（0 = HistoryA / 1 = HistoryB）

            /// @brief この view × light のディスパッチ回数（2x2 サンプル位相・レイジッターの種）
            /// @details 以前はマネージャ共有の frameIndex_ を使っていたが、1 フレームに
            ///          複数回 Dispatch される構成（GameView + ReflectionView や複数ライト）では
            ///          各ビューの位相が 2 や 4 ずつ進み、2x2 サンプル位置の一部しか
            ///          巡回しなくなっていた。
            uint32_t frameCount = 0;
            RayTracingDispatchInfo dispatchInfo{}; ///< デバッグ表示用（Dispatch のたびに更新）

            /// @brief 今フレームのテンポラル出力先（次フレームの履歴）
            TextureSlot CurrentHistorySlot() const {
                return (historyParity == 0) ? TextureSlot::HistoryA : TextureSlot::HistoryB;
            }
            /// @brief 前フレームのテンポラル出力（今フレームが読む履歴）
            TextureSlot PreviousHistorySlot() const {
                return (historyParity == 0) ? TextureSlot::HistoryB : TextureSlot::HistoryA;
            }
        };
        ShadowView views_[kViewCount][kMaxDirectionalLights]{};

        // ---- スロットアクセスの短縮ヘルパー（実体は基盤の outputViews_） ----
        D3D12_GPU_DESCRIPTOR_HANDLE SlotSRV(uint32_t vi, uint32_t li, TextureSlot s) const {
            return outputViews_.GetSRVHandle(MakeSlotIndex(vi, li, s));
        }
        D3D12_GPU_DESCRIPTOR_HANDLE SlotUAV(uint32_t vi, uint32_t li, TextureSlot s) const {
            return outputViews_.GetUAVHandle(MakeSlotIndex(vi, li, s));
        }
        /// @brief スロットをステート追跡つきで返す（バリア発行はこれを渡す）
        GpuResource& Slot(uint32_t vi, uint32_t li, TextureSlot s) {
            return outputViews_.Resource(MakeSlotIndex(vi, li, s));
        }

        /// @brief A-Trous デノイズの最大パス数（kSteps / kPhi* テーブルの要素数）
        static constexpr int kMaxAtrousPassCount = 4;

        /// @brief RayGen 生出力のフォーマット（R = シャドウ値, G = レイ本数）
        /// @details レイ本数はペナンブラ適応サンプリングでピクセルごとに変わるため、
        ///          テンポラルパスが分散を正しく数えられるよう値と一緒に持ち歩く。
        static constexpr DXGI_FORMAT kShadowRawFormat = DXGI_FORMAT_R16G16_FLOAT;

        /// @brief 単チャンネルのシャドウテクスチャ（Mask）のフォーマット
        /// @details R8_UNORM だと量子化幅が 1/255。適応ブレンド α=1/N は N が大きいほど
        ///          1 フレームあたりの更新量が小さくなるので、8bit では収束しきる前に
        ///          丸めで更新が消える（＝残差が固定ディザとして残る）。R16_FLOAT にする。
        static constexpr DXGI_FORMAT kShadowTextureFormat = DXGI_FORMAT_R16_FLOAT;

        /// @brief デノイズ経路を流れる信号のフォーマット（HistoryA/B・DenoiseA/B 共通）
        /// @details チャンネル定義は RTShadowTemporal.CS.hlsl の先頭コメントと共通:
        ///          R = シャドウ値 / G = 推定分散 / B = 蓄積フレーム数 N / A = 線形ビュー深度。
        ///          A-Trous も同じレイアウトで読み書きするので、テンポラル出力と
        ///          ping-pong バッファを同一宣言（float4）で扱える。
        static constexpr DXGI_FORMAT kShadowSignalFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

        /// @brief 太陽高度による射程距離の最大倍率（高度 ~5.7° 相当で頭打ち）
        /// @details 際限なく伸ばすと地平線近くでトラバースが爆発するため上限を設ける。
        static constexpr float kMaxRayDistanceScale = 10.0f;

        /// @brief ライト方向から実効レイ射程を求める
        float ResolveEffectiveRayDistance(const Vector3& lightDirection) const;

        /// @brief CVar（"r.RTShadow.*"）の現在値を settings_ へ取り込む
        /// @details settings_ は内部の複数箇所から参照されるため CVar のキャッシュとして残し、
        ///          Dispatch の先頭で毎フレーム同期する（同期を怠ると設定復元が描画に反映されない）
        void SyncSettingsFromCVars();

        // パラメータ
        RayTracingShadowSettings settings_;

        uint32_t dispatchLogCount_ = 0;
        // isInitialized_ は基底 RayTracingPassBase が持つ。ここで再宣言すると基底のものを
        // 隠してしまい、Initialize() が派生側を true にする一方で
        // IsInitialized()（基底の実装）が false を返し続ける（Stage 2c で実際に踏んだ）。
        bool debugViewRequested_ = false; ///< 中間バッファの ImGui 表示要求（1 フレーム限り）

        /// @brief レイの後段コンピュートパス 1 本ぶんの実体
        /// @details ルートシグネチャはシェーダーのリフレクションから作り、bindings で
        ///          「宣言表の添字 → ルートスロット」を引く。呼び出し側にレジスタ番号も
        ///          ルートパラメータ番号も出てこないのが要点。
        struct ComputePass {
            RootSignatureManager rootSigMgr;                          ///< リフレクション由来の RS
            Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState;
            BindingTable bindings;                                    ///< 解決済みスロット表
            bool initialized = false;

            ID3D12RootSignature* GetRootSignature() { return rootSigMgr.GetRootSignature(); }
        };

        ComputePass denoisePass_;   ///< A-Trous 空間デノイズ
        ComputePass temporalPass_;  ///< テンポラル蓄積
        ComputePass resolvePass_;   ///< トレース解像度 → フル解像度のバイラテラルアップサンプル

        /// @brief トレース解像度の結果をフル解像度 Mask へ書き出す
        /// @param sourceSlot 解決元（A-Trous の最終出力、またはパス数 0 なら履歴）
        void ResolveToFullResolution(
            ID3D12GraphicsCommandList* cmdList,
            D3D12_GPU_DESCRIPTOR_HANDLE normalRoughnessSRV,
            D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSRV,
            const Matrix4x4& projection,
            uint32_t viewIndex,
            uint32_t lightIndex,
            TextureSlot sourceSlot);

        /// @brief コンピュートパス（RS + PSO + 解決済みバインド表）をリフレクションから構築する
        /// @param shaderPath       CS のパス
        /// @param decls            バインド契約（そのシェーダーが持つリソースの宣言表）
        /// @param declCount        宣言数
        /// @param rootConstantName ルート定数にする cbuffer 名（dword 数はリフレクションが決める）
        /// @param debugLabel       ログ・PSO 名に使う識別名
        /// @param outPass          構築先
        /// @return 成功したら true。コンパイル失敗・契約違反はログ済み
        bool CreateComputePass(
            const wchar_t* shaderPath,
            const ShaderBindingDecl* decls,
            size_t declCount,
            const char* rootConstantName,
            const char* debugLabel,
            ComputePass& outPass);
    };
}
