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
        float historyAlpha = 0.15f;        ///< テンポラル蓄積ブレンド係数
        ///< Variance Clampingと組み合わせて使用する固定値

        /// @brief A-Trous デノイズのパス数（0 = デノイズ無効）
        /// @details ping-pong の都合で偶数のみ有効（奇数を渡すと切り捨てられる）。
        ///          この制約は Stage 3 の履歴 ping-pong 化で解消予定。
        ///          実測では 1 パスあたり約 0.28ms（1920x991）。
        ///          既定 2: step 1,2（実効 7x7）。1spp の粒はテンポラル蓄積が主に均すため、
        ///          step 8（実効 31x31）まで広げても、得られる差よりコスト増のほうが大きい。
        int   atrousPassCount = 2;

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

        /// @brief シャドウレイをディスパッチする
        /// @param lightIndex ディレクショナルライトのインデックス（0〜kMaxDirectionalLights-1）
        /// @param sceneDepthSRV WorldPosition ターゲット廃止に伴い深度から復元する
        /// @param invViewProj 深度復元用 View*Projection の逆行列
        void Dispatch(ID3D12GraphicsCommandList* cmdList,
            D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSRV,
            D3D12_GPU_DESCRIPTOR_HANDLE normalRoughnessSRV,
            D3D12_GPU_DESCRIPTOR_HANDLE motionVectorSRV,
            const Vector3& lightDirection,
            const Matrix4x4& invViewProj,
            UINT width, UINT height,
            ViewID viewId = ViewID::GameView,
            uint32_t lightIndex = 0);

        /// @brief A-Trous デノイズパスを実行する（Dispatch の直後に呼ぶ）
        /// @param projection 投影行列。深度重みの線形化にのみ使う（Stage 1 で invViewProj から変更）
        void Denoise(ID3D12GraphicsCommandList* cmdList,
            D3D12_GPU_DESCRIPTOR_HANDLE normalRoughnessSRV,
            D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSRV,
            const Matrix4x4& projection,
            UINT width, UINT height,
            ViewID viewId = ViewID::GameView,
            uint32_t lightIndex = 0);

        /// @brief 空間前処理＋テンポラル蓄積パスを実行する（Dispatch と Denoise の間に呼ぶ）
        /// @param projection 投影行列。深度重みの線形化にのみ使う（Stage 1 で invViewProj から変更）
        void ApplyTemporal(ID3D12GraphicsCommandList* cmdList,
            D3D12_GPU_DESCRIPTOR_HANDLE normalRoughnessSRV,
            D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSRV,
            D3D12_GPU_DESCRIPTOR_HANDLE motionVectorSRV,
            const Matrix4x4& projection,
            UINT width, UINT height,
            ViewID viewId = ViewID::GameView,
            uint32_t lightIndex = 0);

        /// @brief 指定ビュー・ライトのシャドウ結果テクスチャの SRV を取得
        D3D12_GPU_DESCRIPTOR_HANDLE GetShadowSRVHandle(ViewID viewId = ViewID::GameView,
            uint32_t lightIndex = 0) const;

        // ──────────────────────────────────────────────────────────
        // デバッグ表示用（Stage 0: RayTracingDebugPanel が参照する）
        // ──────────────────────────────────────────────────────────

        /// @brief RayGen が書いた生マスク（デノイズ前）の SRV を取得
        /// @details RayGen の出力先は denoiseTemp。テンポラル・A-Trous の前段を見るために公開する。
        D3D12_GPU_DESCRIPTOR_HANDLE GetRawShadowSRVHandle(ViewID viewId = ViewID::GameView,
            uint32_t lightIndex = 0) const;

        /// @brief テンポラル蓄積の履歴テクスチャの SRV を取得
        D3D12_GPU_DESCRIPTOR_HANDLE GetHistorySRVHandle(ViewID viewId = ViewID::GameView,
            uint32_t lightIndex = 0) const;

        /// @brief 直近のディスパッチ診断情報を取得（全 RT パス共通の型）
        const RayTracingDispatchInfo& GetDispatchInfo(ViewID viewId = ViewID::GameView,
            uint32_t lightIndex = 0) const;

        /// @brief 実際に適用される A-Trous パス数（偶数への切り捨て後）
        int GetEffectiveAtrousPassCount() const;

        /// @brief 中間バッファ（生マスク・履歴）を ImGui で表示することを要求する
        /// @details denoiseTemp はパス終了時 UNORDERED_ACCESS、historyTexture は
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
        void SetSettings(const RayTracingShadowSettings& settings) { settings_ = settings; }

        /// @brief 現在のシャドウパラメータを取得する
        const RayTracingShadowSettings& GetSettings() const { return settings_; }

    private:
        bool EnsureOutputTexture(UINT width, UINT height, uint32_t viewIndex, uint32_t lightIndex);

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
            UINT historyShadow = 0;
            UINT motionVector = 0;
            UINT constants = 0;
        };
        RootParamIndices rootParams_{};

        /// @brief 1 つの (view, light) が持つテクスチャの用途
        /// @details 実体は共通基盤の RayTracingOutputViewSet が持ち、
        ///          ここではスロット番号への写像だけを定義する（Stage 2c）。
        enum class TextureSlot : uint32_t {
            Mask = 0,        ///< 最終シャドウマスク（DeferredLighting が読む）
            History = 1,     ///< テンポラル蓄積の履歴（UAV 不要・COPY_DEST + SRV）
            DenoiseTemp = 2, ///< RayGen の生出力兼 A-Trous の ping-pong 相手
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
            UINT width = 0;
            UINT height = 0;
            bool dispatchedThisFrame = false;
            bool isHistoryValid = false; ///< 履歴テクスチャが初回フレーム書き込み済みか
            RayTracingDispatchInfo dispatchInfo{}; ///< デバッグ表示用（Dispatch のたびに更新）
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

        /// @brief 太陽高度による射程距離の最大倍率（高度 ~5.7° 相当で頭打ち）
        /// @details 際限なく伸ばすと地平線近くでトラバースが爆発するため上限を設ける。
        static constexpr float kMaxRayDistanceScale = 10.0f;

        /// @brief ライト方向から実効レイ射程を求める
        float ResolveEffectiveRayDistance(const Vector3& lightDirection) const;

        // パラメータ
        RayTracingShadowSettings settings_;

        uint32_t frameIndex_ = 0;
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
    };
}
