#pragma once

#include <d3d12.h>
#include <dxcapi.h>
#include <wrl.h>
#include <cstdint>
#include "Math/Vector/Vector3.h"
#include "Math/Matrix/Matrix4x4.h"
#include "GlobalRootSignatureManager.h"
#include "RayTracingDispatchInfo.h"
#include "RayTracingOutputViewSet.h"
#include "RayTracingPassBase.h"
#include "RayTracingPipelineBuilder.h"
#include "ShaderTableBuilder.h"

namespace CoreEngine
{
    class DirectXCommon;
    class DescriptorManager;
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
        int   softShadowSamples = 1;       ///< ソフトシャドウのサンプル数（A-Trousデノイザーで補完するため1で十分）
        ///< A-Trous 3パスデノイザー適用済みのため 1 で十分な品質が得られる
        ///< 高品質なソフトシャドウが必要な場合は 2〜4 程度まで増やす（GPUコストはサンプル数に比例）
        /// @brief テンポラル蓄積フレーム数の上限
        /// @details ピクセルごとの蓄積カウント N による適応ブレンド（α = 1/N）を行い、
        ///          静止時は α = 1/この値 まで収束する。旧実装の固定 α（historyAlpha）は
        ///          定常状態でも入力ノイズの sqrt(α/(2-α)) が残り続け、静止カメラでも
        ///          影のペナンブラが毎フレームちらつく主因だったため廃止した。
        ///          影の変化（ライト移動等）への追従は Variance Clamping の棄却で
        ///          カウントがリセットされることで担保される。
        int   maxHistoryFrames = 32;

        /// @brief A-Trous デノイズのパス数（0 = デノイズ無効）
        /// @details Stage 3 で専用の ping/pong スクラッチを 2 枚持たせたため、
        ///          「偶数のみ」という旧制約は無くなり 0〜4 の任意値が指定できる。
        ///          既定 2: step 1,2（実効 7x7）。1spp の粒はテンポラル蓄積が主に均すため、
        ///          step 8（実効 31x31）まで広げても、得られる差よりコスト増のほうが大きい。
        int   atrousPassCount = 2;

        /// @brief トレース〜デノイズをハーフ解像度で行い、最後にバイラテラルアップサンプルする
        /// @details レイ本数・デノイズの帯域がともに 1/4 になる。最終マスク（DeferredLighting が
        ///          読む実体）はフル解像度のまま。フレームごとに 2x2 のサンプル位置を巡回させ、
        ///          テンポラル蓄積で 4 サブピクセルぶんの情報を回収する。
        bool  halfResolutionTrace = true;

        /// @brief アップサンプル時の深度エッジ重み係数
        /// @details 大きいほど深度が近いトレース結果だけを採用する（＝境界がシャープになるが
        ///          採用サンプルが減ってエイリアスが出やすい）。
        float upsamplePhiDepth = 8.0f;

        /// @brief A-Trous / テンポラルの深度エッジ重み係数
        /// @details 大きいほどエッジ検出が厳しくなり（＝ぼけにくく）、小さいほど広くぼける。
        ///          Stage 1 で深度指標を「ワールド原点からの距離」から
        ///          「線形ビュー深度」へ変更したため再較正が必要になった値。
        float denoisePhiDepth = 1.0f;

        /// @brief テンポラル蓄積の履歴参照を強制的に無効化する（デバッグ用）
        /// @details true にすると毎フレーム現フレームの空間前処理結果だけを使う。
        ///          ゴーストや残像がテンポラル由来かを切り分けるためのトグル。
        bool  disableHistory = false;
    };

    /// @brief DXR レイトレーシングシャドウ
    /// @details Stage 2c で共通基盤 RayTracingPassBase の上に載せ替えた。
    ///          出力テクスチャ・ガード判定・DXR オブジェクト（State Object / Shader Table /
    ///          グローバルルートシグネチャ）は基盤側が持つ。
    ///          ここに残るのは「シャドウ固有」＝ RayGen / テンポラル / A-Trous の 3 ステージと、
    ///          view × ライトごとの履歴有効フラグ・診断情報だけ。
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
        bool Initialize(DirectXCommon* dxCommon, DescriptorManager* descriptorManager,
            AccelerationStructureManager* asMgr);

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
        /// @details Raw はパス終了時 UNORDERED_ACCESS、履歴テクスチャは
        ///          NON_PIXEL_SHADER_RESOURCE で残るため、そのまま ImGui::Image に渡すと
        ///          ピクセルシェーダから不正な状態で読むことになる。
        ///          表示したいフレームに本関数を呼ぶと、PrepareDebugViews が
        ///          PIXEL_SHADER_RESOURCE へ遷移させる。
        /// @note 1 フレーム限りの要求。PrepareDebugViews が消費してクリアする
        ///       （パネルを閉じたら自動的にバリアが消える）。
        void RequestDebugViewTransition() { debugViewRequested_ = true; }

        /// @brief RequestDebugViewTransition の要求を消費し、中間バッファを表示可能状態へ遷移させる
        /// @details RT シャドウの全ステージ完了後（Denoise パスの末尾）に呼ぶ。
        void PrepareDebugViews(ID3D12GraphicsCommandList* cmdList);

        /// @brief 指定ビュー・ライトのシャドウ結果テクスチャを取得する
        /// @param viewId 参照するビュー ID
        /// @param lightIndex 参照するディレクショナルライト番号
        /// @return シャドウ結果テクスチャ。未確保なら nullptr
        ID3D12Resource* GetShadowResource(
            ViewID viewId = ViewID::GameView,
            uint32_t lightIndex = 0) const;

        /// @brief 指定ビュー・ライトのシャドウ結果リソース状態参照を取得する
        /// @param viewId 参照するビュー ID
        /// @param lightIndex 参照するディレクショナルライト番号
        /// @return 自動遷移処理が共有する状態変数への参照
        D3D12_RESOURCE_STATES& GetShadowCurrentState(
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

        // dxCommon_ / descriptorManager_ / asMgr_ / globalRootSigMgr_ / stateObject_ /
        // stateObjectProperties_ / shaderTableBuilder_ / outputViews_ / isInitialized_ は
        // 全て RayTracingPassBase が持つ（Stage 2c で重複を削除した）。

        // シェーダーバイトコード（State Object 構築後に解放するのでここに置く）
        Microsoft::WRL::ComPtr<IDxcBlob> shaderBlob_;

        /// @brief グローバルルートシグネチャのパラメータ番号（Initialize で 1 回だけ解決する）
        /// @details GetRootParameterIndex は std::map<std::string> 検索で、
        ///          しかも呼ぶたびに std::string の一時オブジェクトを作る。
        ///          ディスパッチごとに 6 回叩いていたのでキャッシュした（Stage 2d）。
        struct RootParamIndices {
            UINT shadowOutput = 0;
            UINT scene = 0;
            UINT sceneDepth = 0;
            UINT normalRoughness = 0;
            UINT constants = 0;
        };
        RootParamIndices rootParams_{};

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
        ID3D12Resource* SlotResource(uint32_t vi, uint32_t li, TextureSlot s) const {
            return outputViews_.GetResource(MakeSlotIndex(vi, li, s));
        }
        D3D12_RESOURCE_STATES& SlotState(uint32_t vi, uint32_t li, TextureSlot s) {
            return outputViews_.GetCurrentState(MakeSlotIndex(vi, li, s));
        }

        /// @brief A-Trous デノイズの最大パス数（kSteps / kPhi* テーブルの要素数）
        static constexpr int kMaxAtrousPassCount = 4;

        /// @brief シャドウ関連テクスチャのフォーマット
        /// @details マスクは 0〜1 の 1 チャンネルなので R8_UNORM で足り、
        ///          R32_FLOAT 比で全パスの帯域が 1/4 になる（Stage 3）。
        ///          テンポラル蓄積の量子化が気になる場合はここを R16_FLOAT へ変えるだけでよい。
        static constexpr DXGI_FORMAT kShadowTextureFormat = DXGI_FORMAT_R8_UNORM;

        /// @brief テンポラル履歴（HistoryA/B）専用フォーマット
        /// @details R = シャドウ値, G = 蓄積フレーム数 N/255。適応ブレンド α=1/N の
        ///          カウントをピクセル単位で持ち歩く。後段（A-Trous / Resolve）は
        ///          Texture2D<float> で R だけ読むので変更不要。
        static constexpr DXGI_FORMAT kShadowHistoryFormat = DXGI_FORMAT_R8G8_UNORM;

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

        // A-Trous デノイズ用コンピュートパイプライン
        Microsoft::WRL::ComPtr<ID3D12RootSignature> denoiseRootSignature_;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> denoisePipelineState_;
        bool denoiseInitialized_ = false;

        // テンポラル蓄積用コンピュートパイプライン
        Microsoft::WRL::ComPtr<ID3D12RootSignature> temporalRootSignature_;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> temporalPipelineState_;
        bool temporalInitialized_ = false;

        // トレース解像度 → フル解像度への解決（バイラテラルアップサンプル）パイプライン
        Microsoft::WRL::ComPtr<ID3D12RootSignature> resolveRootSignature_;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> resolvePipelineState_;
        bool resolveInitialized_ = false;

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

        /// @brief コンピュートパイプライン（RS + PSO）をまとめて構築する共通ヘルパー
        /// @param srvCount  連続する t0..t(srvCount-1) のディスクリプタテーブル数
        /// @param constantDwordCount b0 のルート定数の dword 数
        /// @details デノイズ / テンポラル / 解決の 3 本が「SRV テーブル n 個 + UAV テーブル 1 個
        ///          + ルート定数 1 個」という完全に同じ形をしていたので 1 か所へまとめた。
        ///          ルートパラメータ番号は t0..=0..n-1 / u0=n / b0=n+1 で固定される。
        bool CreateComputePass(
            const wchar_t* shaderPath,
            UINT srvCount,
            UINT constantDwordCount,
            const char* debugLabel,
            Microsoft::WRL::ComPtr<ID3D12RootSignature>& outRootSignature,
            Microsoft::WRL::ComPtr<ID3D12PipelineState>& outPipelineState);
    };
}
