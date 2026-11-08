#pragma once

#ifdef CORE_EDITOR

#include <d3d12.h>
#include <wrl.h>

#include <array>
#include <cstdint>
#include <vector>

#include "Graphics/RHI/Command/FrameSync.h"
#include "Graphics/RHI/Descriptor/DescriptorHandle.h"
#include "Graphics/RHI/Resource/GpuResource.h"
#include "Graphics/RayTracing/Debug/RTDebugRegistry.h"
#include "Graphics/RayTracing/RayTracingOutputViewSet.h"
#include "Graphics/RootSignature/RootSignatureManager.h"
#include "Graphics/Shader/ShaderBindingContract.h"
#include "Math/Vector/Vector3.h"

namespace CoreEngine
{
    class DescriptorAllocator;
    class ShaderProgramCache;

    /// @brief 見え方で書き出す値（カメラから撃ったレイが何に当たったか）
    enum class RTDebugSceneMode : uint32_t {
        Instance = 0,     ///< TLAS の行の番号
        SubMesh,          ///< サブメッシュ（BLAS の形）の番号
        Triangle,         ///< 形の中の三角形の番号
        Distance,         ///< 当たった距離
        Normal,           ///< 面の法線（レイが来た側を向く）
        BaseColor,        ///< 材質のベースカラー
        Mask,             ///< インスタンスの印（RTInstanceMask）
        FrontFace,        ///< 表から当たったか
        Motion,           ///< 動き（0 止まっている・1 動く形の決まった物・2 毎フレーム形を作る物）
        DepthDifference,  ///< RT の距離 − ラスタの深度の距離
        Candidates,       ///< 不透明な物も含めて調べた三角形の候補の数
        AlphaTests,       ///< アルファを調べた候補の数
        Count
    };

    /// @brief 見え方の表示名
    const char* ToDisplayName(RTDebugSceneMode mode);

    /// @brief 見え方の既定の色の付け方と範囲
    struct RTDebugSceneModeInfo {
        RTDebugViewMode viewMode = RTDebugViewMode::Mono;
        float rangeMin = 0.0f;
        float rangeMax = 1.0f;
        const char* description = "";
    };
    RTDebugSceneModeInfo GetSceneModeInfo(RTDebugSceneMode mode);

    /// @brief 中間結果に色を付けて見る頼み
    struct RTDebugViewRequest {
        int channel = -1;                 ///< RTDebugRegistry::Channels() の番号（-1 なら見ない）
        uint32_t variant = 0;             ///< 変種（影ならライトの番号）
        RTDebugViewMode mode = RTDebugViewMode::Mono;
        uint32_t component = 0;           ///< 1 成分の色の付け方で見る成分
        uint32_t componentMask = 0x7;     ///< 色そのままで出す成分（RGBA のビット）
        float rangeMin = 0.0f;
        float rangeMax = 1.0f;
        bool alphaMarksMiss = false;      ///< A = 0 の画素を模様で出す
        bool probeEnabled = false;        ///< probeX, probeY の生の値を読むか
        uint32_t probeX = 0;              ///< 中間結果の画素の座標
        uint32_t probeY = 0;
    };

    /// @brief 見え方の頼み
    struct RTDebugSceneRequest {
        RTDebugSceneMode mode = RTDebugSceneMode::Instance;
        uint32_t instanceMask = 0xFF;     ///< カメラのレイが当てる印
        bool alphaTest = true;            ///< アルファで抜ける所を通すか
    };

    /// @brief 色を付けた 1 枚の数値のまとめ（GPU で数えた値）
    struct RTDebugImageStats {
        bool valid = false;
        uint64_t frame = 0;               ///< 数えたフレーム
        int channel = -1;
        uint32_t variant = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t nanCount = 0;            ///< 見ている成分に NaN がある画素
        uint32_t infCount = 0;            ///< 見ている成分に Inf がある画素（NaN の画素を除く）
        uint32_t finiteCount = 0;         ///< 見ている成分が全部有限の画素
        bool hasRange = false;            ///< minValue / maxValue が有効か
        float minValue = 0.0f;            ///< 見ている成分の有限の値の最小
        float maxValue = 0.0f;            ///< 見ている成分の有限の値の最大
        bool probeValid = false;
        uint32_t probeX = 0;
        uint32_t probeY = 0;
        float probe[4] = {};              ///< probeX, probeY の生の値
    };

    /// @brief 追跡したレイ 1 本
    struct RTDebugTracedRay {
        uint32_t kind = 0;                ///< 0 カメラ・1〜4 ライト 0〜3 への影・5 映り込み
        Vector3 origin{};
        Vector3 direction{};
        float tMax = 0.0f;
        bool hit = false;
        float hitT = 0.0f;
        bool frontFace = false;
        uint32_t row = 0;                 ///< 当たった TLAS の行
        uint32_t geometry = 0;            ///< 当たったサブメッシュ
        uint32_t primitive = 0;           ///< 当たった三角形
        uint32_t instanceMask = 0;        ///< 撃った印
        uint32_t candidates = 0;          ///< シェーダーへ来た候補の数
        uint32_t alphaTests = 0;          ///< アルファを調べた数
    };

    /// @brief レイの追跡の結果
    struct RTDebugTraceResult {
        bool valid = false;
        uint64_t frame = 0;
        uint32_t x = 0;
        uint32_t y = 0;
        bool hit = false;                 ///< カメラのレイが当たったか
        Vector3 hitPosition{};
        Vector3 hitNormal{};
        float baseColor[4] = {};
        float uv[2] = {};
        std::vector<RTDebugTracedRay> rays;
    };

    /// @brief 見え方の画面全体の合計
    struct RTDebugSceneStats {
        bool valid = false;
        uint64_t frame = 0;
        RTDebugSceneMode mode = RTDebugSceneMode::Instance;
        uint64_t candidates = 0;
        uint64_t alphaTests = 0;
        uint64_t hitPixels = 0;
        uint64_t pixels = 0;
    };

    /// @brief 中間結果 1 枚の NaN・Inf の数え
    struct RTDebugScanEntry {
        int channel = -1;
        uint32_t variant = 0;
        uint64_t frame = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t nanCount = 0;
        uint32_t infCount = 0;
        uint32_t finiteCount = 0;
    };

    /// @brief レイトレーシングのデバッグ表示の GPU 側
    /// @details 窓が毎フレーム出す頼みに従い、中間結果の色付けと数え・RT から見たシーン・
    ///          1 画素のレイの追跡・中間結果の NaN の見回りを積む。結果は GPU が終えたフレームから読み戻す。
    ///          頼みが kRequestKeepFrames フレーム来なければ何も積まない。
    ///          パイプラインとバッファは初めて頼まれたフレームに作る。
    class RTDebugSystem {
    public:
        RTDebugSystem() = default;
        ~RTDebugSystem() = default;
        RTDebugSystem(const RTDebugSystem&) = delete;
        RTDebugSystem& operator=(const RTDebugSystem&) = delete;

        /// @brief 取り口を控える（パイプラインは初めて頼まれたときに作る）
        void Initialize(GraphicsCore* graphics, ShaderProgramCache* shaderProgramCache);
        /// @brief パイプラインとバッファができているか
        bool IsReady() const { return created_; }

        // ---- 窓から毎フレーム出す頼み ----
        void RequestView(const RTDebugViewRequest& request);
        void RequestScene(const RTDebugSceneRequest& request);
        /// @brief 1 画素のレイを追う（画面の画素の座標）
        void RequestTrace(uint32_t x, uint32_t y);
        /// @brief 中間結果を 1 フレームに 1 枚ずつ回って NaN と Inf を数える
        void RequestScan();
        /// @brief GPU が終えたフレームの結果を読む
        void CollectResults();

        // ---- 描画の途中で呼ぶ ----
        /// @brief RT から見たシーンとレイの追跡（TLAS とシーンの深度ができた後）
        void ExecuteScene(const RenderContext& context);
        /// @brief 中間結果の色付けと NaN の見回り（全部の効果の後）
        void ExecuteVisualize(const RenderContext& context);

        /// @brief 見え方の画像を中間結果として渡す（今フレームに書いていなければ false）
        bool ResolveSceneSource(RTDebugSource& out);

        // ---- 結果 ----
        /// @brief 色を付けた画像の SRV（無ければ ptr = 0）
        D3D12_GPU_DESCRIPTOR_HANDLE GetViewSRV() const;
        uint32_t GetViewWidth() const;
        uint32_t GetViewHeight() const;
        /// @brief 最後に色を付けたフレーム
        uint64_t GetViewFrame() const { return viewFrame_; }
        /// @brief 最後に色を付けた中間結果
        int GetViewChannel() const { return viewChannel_; }
        uint32_t GetViewVariant() const { return viewVariant_; }
        const RTDebugImageStats& GetViewStats() const { return viewStats_; }
        const RTDebugSceneStats& GetSceneStats() const { return sceneStats_; }
        const RTDebugTraceResult& GetTraceResult() const { return traceResult_; }
        const std::vector<RTDebugScanEntry>& GetScanResults() const { return scanResults_; }
        /// @brief 見回りを最後に頼まれたフレーム（0 なら頼まれていない）
        uint64_t GetScanRequestFrame() const { return scanRequestFrame_; }

        /// @brief 行の印（RTDebugScene.CS.hlsl の kRowFlag* と同じ）
        static constexpr uint32_t kRowFlagAlphaTested = 1u << 0;
        static constexpr uint32_t kRowFlagDynamicShape = 1u << 1;
        static constexpr uint32_t kRowFlagMoving = 1u << 2;

    private:
        /// @brief 頼みが来なくなってから積むのをやめるまでのフレーム数
        static constexpr uint64_t kRequestKeepFrames = 2;
        /// @brief 追跡で記録するレイの最大数
        static constexpr uint32_t kMaxTracedRays = 8;

        /// @brief 読み戻しの枠 1 つ分の記録
        struct PendingFrame {
            bool pending = false;
            uint64_t fenceBeforeSubmit = 0;
            uint64_t frame = 0;
            bool view = false;
            int viewChannel = -1;
            uint32_t viewVariant = 0;
            uint32_t viewWidth = 0;
            uint32_t viewHeight = 0;
            bool viewProbe = false;
            uint32_t probeX = 0;
            uint32_t probeY = 0;
            bool scan = false;
            int scanChannel = -1;
            uint32_t scanVariant = 0;
            uint32_t scanWidth = 0;
            uint32_t scanHeight = 0;
            bool scene = false;
            RTDebugSceneMode sceneMode = RTDebugSceneMode::Instance;
            bool trace = false;
        };

        /// @brief コンピュートのパイプライン 1 本
        struct ComputePipeline {
            RootSignatureManager rootSignature;
            Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState;
            BindingTable bindings;
        };

        /// @brief パイプラインとバッファを作る（作れなければ以後は作り直さない）
        bool EnsureCreated();
        bool CreatePipeline(ShaderProgramCache* cache, const wchar_t* path, const char* label,
            const ShaderBindingDecl* decls, size_t declCount, bool directHeap, ComputePipeline& out);
        bool CreateBuffers();

        /// @brief 今のフレームの番号
        uint64_t CurrentFrame() const;
        bool IsRequested(uint64_t requestFrame) const;

        /// @brief 数える場所を初期値へ戻す（バイト位置と長さ）
        void ResetData(ID3D12GraphicsCommandList* cmdList, UINT offset, UINT bytes);
        /// @brief 今フレームにまだ積んでいなければ、この枠の記録を始める
        PendingFrame& BeginPendingFrame();

        /// @brief 行の情報（印と行の印）を送り、SRV を返す
        D3D12_GPU_DESCRIPTOR_HANDLE UploadRowInfo(const RenderContext& context, uint32_t& outRowCount);

        /// @brief 1 枚に色を付けるか数える
        bool DispatchVisualize(const RenderContext& context, const RTDebugChannelDesc& channel, uint32_t variant,
            const RTDebugViewRequest& request, bool writeOutput, UINT statsOffset, uint32_t& outWidth, uint32_t& outHeight);

        /// @brief 見回りで次に数える中間結果を選ぶ（無ければ false）
        bool NextScanTarget(const RenderContext& context, int& outChannel, uint32_t& outVariant, RTDebugSource& outSource);

        void ReadFrame(uint32_t frameIndex);

        GraphicsCore* graphics_ = nullptr;
        DescriptorAllocator* descriptorAllocator_ = nullptr;
        ShaderProgramCache* shaderProgramCache_ = nullptr;
        bool created_ = false;
        bool createFailed_ = false;

        ComputePipeline visualizePipeline_;
        ComputePipeline scenePipeline_;

        // 数えた値を書くバッファと、その初期値・読み戻し
        GpuResource data_{};
        DescriptorHandle dataUav_{};
        Microsoft::WRL::ComPtr<ID3D12Resource> dataInit_;
        std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kMaxFramesInFlight> readback_{};
        std::array<PendingFrame, kMaxFramesInFlight> pendingFrames_{};

        // 行の情報（フレームインフライトぶんのリング）
        std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kMaxFramesInFlight> rowInfoBuffers_{};
        std::array<DescriptorHandle, kMaxFramesInFlight> rowInfoSrvs_{};
        std::array<uint32_t, kMaxFramesInFlight> rowInfoCapacities_{};
        std::vector<uint32_t> rowInfoScratch_;

        // 0 = 色を付けた画像・1 = 見え方の画像
        RayTracingOutputViewSet outputs_;

        // 頼み
        RTDebugViewRequest viewRequest_{};
        uint64_t viewRequestFrame_ = 0;
        RTDebugSceneRequest sceneRequest_{};
        uint64_t sceneRequestFrame_ = 0;
        uint32_t traceX_ = 0;
        uint32_t traceY_ = 0;
        uint64_t traceRequestFrame_ = 0;
        uint64_t scanRequestFrame_ = 0;
        size_t scanCursor_ = 0;

        // 今フレームの状態
        uint64_t sceneWrittenFrame_ = 0;
        uint64_t viewFrame_ = 0;
        int viewChannel_ = -1;
        uint32_t viewVariant_ = 0;

        // 読み戻した結果
        RTDebugImageStats viewStats_{};
        RTDebugSceneStats sceneStats_{};
        RTDebugTraceResult traceResult_{};
        std::vector<RTDebugScanEntry> scanResults_;
    };
}

#endif // CORE_EDITOR
