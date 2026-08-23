#include "pch.h"
#include "Graphics/RHI/Device/DeviceRemovedHandler.h"

#include "Utility/Logger/Logger.h"

#include <wrl.h>
#include <string>

using Microsoft::WRL::ComPtr;

namespace CoreEngine
{
    namespace
    {
        /// @brief GPU が実行していた命令の種類を名前にする
        /// @details 数値のままだと調査のたびにヘッダを引く必要があるので、ここで名前に開く。
        const char* ToString(D3D12_AUTO_BREADCRUMB_OP op)
        {
            switch (op) {
            case D3D12_AUTO_BREADCRUMB_OP_SETMARKER:                                        return "SetMarker";
            case D3D12_AUTO_BREADCRUMB_OP_BEGINEVENT:                                       return "BeginEvent";
            case D3D12_AUTO_BREADCRUMB_OP_ENDEVENT:                                         return "EndEvent";
            case D3D12_AUTO_BREADCRUMB_OP_DRAWINSTANCED:                                    return "DrawInstanced";
            case D3D12_AUTO_BREADCRUMB_OP_DRAWINDEXEDINSTANCED:                             return "DrawIndexedInstanced";
            case D3D12_AUTO_BREADCRUMB_OP_EXECUTEINDIRECT:                                  return "ExecuteIndirect";
            case D3D12_AUTO_BREADCRUMB_OP_DISPATCH:                                         return "Dispatch";
            case D3D12_AUTO_BREADCRUMB_OP_COPYBUFFERREGION:                                 return "CopyBufferRegion";
            case D3D12_AUTO_BREADCRUMB_OP_COPYTEXTUREREGION:                                return "CopyTextureRegion";
            case D3D12_AUTO_BREADCRUMB_OP_COPYRESOURCE:                                     return "CopyResource";
            case D3D12_AUTO_BREADCRUMB_OP_COPYTILES:                                        return "CopyTiles";
            case D3D12_AUTO_BREADCRUMB_OP_RESOLVESUBRESOURCE:                               return "ResolveSubresource";
            case D3D12_AUTO_BREADCRUMB_OP_CLEARRENDERTARGETVIEW:                            return "ClearRenderTargetView";
            case D3D12_AUTO_BREADCRUMB_OP_CLEARUNORDEREDACCESSVIEW:                         return "ClearUnorderedAccessView";
            case D3D12_AUTO_BREADCRUMB_OP_CLEARDEPTHSTENCILVIEW:                            return "ClearDepthStencilView";
            case D3D12_AUTO_BREADCRUMB_OP_RESOURCEBARRIER:                                  return "ResourceBarrier";
            case D3D12_AUTO_BREADCRUMB_OP_EXECUTEBUNDLE:                                    return "ExecuteBundle";
            case D3D12_AUTO_BREADCRUMB_OP_PRESENT:                                          return "Present";
            case D3D12_AUTO_BREADCRUMB_OP_RESOLVEQUERYDATA:                                 return "ResolveQueryData";
            case D3D12_AUTO_BREADCRUMB_OP_BEGINSUBMISSION:                                  return "BeginSubmission";
            case D3D12_AUTO_BREADCRUMB_OP_ENDSUBMISSION:                                    return "EndSubmission";
            case D3D12_AUTO_BREADCRUMB_OP_DECODEFRAME:                                      return "DecodeFrame";
            case D3D12_AUTO_BREADCRUMB_OP_PROCESSFRAMES:                                    return "ProcessFrames";
            case D3D12_AUTO_BREADCRUMB_OP_ATOMICCOPYBUFFERUINT:                             return "AtomicCopyBufferUInt";
            case D3D12_AUTO_BREADCRUMB_OP_ATOMICCOPYBUFFERUINT64:                           return "AtomicCopyBufferUInt64";
            case D3D12_AUTO_BREADCRUMB_OP_RESOLVESUBRESOURCEREGION:                         return "ResolveSubresourceRegion";
            case D3D12_AUTO_BREADCRUMB_OP_WRITEBUFFERIMMEDIATE:                             return "WriteBufferImmediate";
            case D3D12_AUTO_BREADCRUMB_OP_DECODEFRAME1:                                     return "DecodeFrame1";
            case D3D12_AUTO_BREADCRUMB_OP_SETPROTECTEDRESOURCESESSION:                      return "SetProtectedResourceSession";
            case D3D12_AUTO_BREADCRUMB_OP_DECODEFRAME2:                                     return "DecodeFrame2";
            case D3D12_AUTO_BREADCRUMB_OP_PROCESSFRAMES1:                                   return "ProcessFrames1";
            case D3D12_AUTO_BREADCRUMB_OP_BUILDRAYTRACINGACCELERATIONSTRUCTURE:             return "BuildRaytracingAccelerationStructure";
            case D3D12_AUTO_BREADCRUMB_OP_EMITRAYTRACINGACCELERATIONSTRUCTUREPOSTBUILDINFO: return "EmitRaytracingASPostbuildInfo";
            case D3D12_AUTO_BREADCRUMB_OP_COPYRAYTRACINGACCELERATIONSTRUCTURE:              return "CopyRaytracingAccelerationStructure";
            case D3D12_AUTO_BREADCRUMB_OP_DISPATCHRAYS:                                     return "DispatchRays";
            case D3D12_AUTO_BREADCRUMB_OP_INITIALIZEMETACOMMAND:                            return "InitializeMetaCommand";
            case D3D12_AUTO_BREADCRUMB_OP_EXECUTEMETACOMMAND:                               return "ExecuteMetaCommand";
            case D3D12_AUTO_BREADCRUMB_OP_ESTIMATEMOTION:                                   return "EstimateMotion";
            case D3D12_AUTO_BREADCRUMB_OP_RESOLVEMOTIONVECTORHEAP:                          return "ResolveMotionVectorHeap";
            case D3D12_AUTO_BREADCRUMB_OP_SETPIPELINESTATE1:                                return "SetPipelineState1";
            case D3D12_AUTO_BREADCRUMB_OP_INITIALIZEEXTENSIONCOMMAND:                       return "InitializeExtensionCommand";
            case D3D12_AUTO_BREADCRUMB_OP_EXECUTEEXTENSIONCOMMAND:                          return "ExecuteExtensionCommand";
            case D3D12_AUTO_BREADCRUMB_OP_DISPATCHMESH:                                     return "DispatchMesh";
            case D3D12_AUTO_BREADCRUMB_OP_ENCODEFRAME:                                      return "EncodeFrame";
            case D3D12_AUTO_BREADCRUMB_OP_RESOLVEENCODEROUTPUTMETADATA:                     return "ResolveEncoderOutputMetadata";
            case D3D12_AUTO_BREADCRUMB_OP_BARRIER:                                          return "Barrier";
            case D3D12_AUTO_BREADCRUMB_OP_BEGIN_COMMAND_LIST:                               return "BeginCommandList";
            case D3D12_AUTO_BREADCRUMB_OP_DISPATCHGRAPH:                                    return "DispatchGraph";
            case D3D12_AUTO_BREADCRUMB_OP_SETPROGRAM:                                       return "SetProgram";
            default:                                                                        return "Unknown";
            }
        }

        /// @brief ページフォルト周辺にあったオブジェクトの種類を名前にする
        const char* ToString(D3D12_DRED_ALLOCATION_TYPE type)
        {
            switch (type) {
            case D3D12_DRED_ALLOCATION_TYPE_COMMAND_QUEUE:              return "CommandQueue";
            case D3D12_DRED_ALLOCATION_TYPE_COMMAND_ALLOCATOR:          return "CommandAllocator";
            case D3D12_DRED_ALLOCATION_TYPE_PIPELINE_STATE:             return "PipelineState";
            case D3D12_DRED_ALLOCATION_TYPE_COMMAND_LIST:               return "CommandList";
            case D3D12_DRED_ALLOCATION_TYPE_FENCE:                      return "Fence";
            case D3D12_DRED_ALLOCATION_TYPE_DESCRIPTOR_HEAP:            return "DescriptorHeap";
            case D3D12_DRED_ALLOCATION_TYPE_HEAP:                       return "Heap";
            case D3D12_DRED_ALLOCATION_TYPE_QUERY_HEAP:                 return "QueryHeap";
            case D3D12_DRED_ALLOCATION_TYPE_COMMAND_SIGNATURE:          return "CommandSignature";
            case D3D12_DRED_ALLOCATION_TYPE_PIPELINE_LIBRARY:           return "PipelineLibrary";
            case D3D12_DRED_ALLOCATION_TYPE_VIDEO_DECODER:              return "VideoDecoder";
            case D3D12_DRED_ALLOCATION_TYPE_VIDEO_PROCESSOR:            return "VideoProcessor";
            case D3D12_DRED_ALLOCATION_TYPE_RESOURCE:                   return "Resource";
            case D3D12_DRED_ALLOCATION_TYPE_PASS:                       return "Pass";
            case D3D12_DRED_ALLOCATION_TYPE_CRYPTOSESSION:              return "CryptoSession";
            case D3D12_DRED_ALLOCATION_TYPE_CRYPTOSESSIONPOLICY:        return "CryptoSessionPolicy";
            case D3D12_DRED_ALLOCATION_TYPE_PROTECTEDRESOURCESESSION:   return "ProtectedResourceSession";
            case D3D12_DRED_ALLOCATION_TYPE_VIDEO_DECODER_HEAP:         return "VideoDecoderHeap";
            case D3D12_DRED_ALLOCATION_TYPE_COMMAND_POOL:               return "CommandPool";
            case D3D12_DRED_ALLOCATION_TYPE_COMMAND_RECORDER:           return "CommandRecorder";
            case D3D12_DRED_ALLOCATION_TYPE_STATE_OBJECT:               return "StateObject";
            case D3D12_DRED_ALLOCATION_TYPE_METACOMMAND:                return "MetaCommand";
            case D3D12_DRED_ALLOCATION_TYPE_SCHEDULINGGROUP:            return "SchedulingGroup";
            case D3D12_DRED_ALLOCATION_TYPE_VIDEO_MOTION_ESTIMATOR:     return "VideoMotionEstimator";
            case D3D12_DRED_ALLOCATION_TYPE_VIDEO_MOTION_VECTOR_HEAP:   return "VideoMotionVectorHeap";
            case D3D12_DRED_ALLOCATION_TYPE_VIDEO_EXTENSION_COMMAND:    return "VideoExtensionCommand";
            case D3D12_DRED_ALLOCATION_TYPE_VIDEO_ENCODER:              return "VideoEncoder";
            case D3D12_DRED_ALLOCATION_TYPE_VIDEO_ENCODER_HEAP:         return "VideoEncoderHeap";
            case D3D12_DRED_ALLOCATION_TYPE_INVALID:                    return "Invalid";
            default:                                                    return "Unknown";
            }
        }

        /// @brief デバイスが失われた理由を名前にする
        const char* ReasonToString(HRESULT reason)
        {
            switch (reason) {
            case DXGI_ERROR_DEVICE_HUNG:            return "DEVICE_HUNG（GPU が応答しない＝シェーダの無限ループや過大な作業。TDR）";
            case DXGI_ERROR_DEVICE_REMOVED:         return "DEVICE_REMOVED（アダプタが取り外された／ドライバが更新された）";
            case DXGI_ERROR_DEVICE_RESET:           return "DEVICE_RESET（不正なコマンドでデバイスがリセットされた）";
            case DXGI_ERROR_DRIVER_INTERNAL_ERROR:  return "DRIVER_INTERNAL_ERROR（ドライバ内部エラー）";
            case DXGI_ERROR_INVALID_CALL:           return "INVALID_CALL（不正な API 呼び出し）";
            case E_OUTOFMEMORY:                     return "E_OUTOFMEMORY（VRAM 不足）";
            default:                                return "（既知の分類に該当しない）";
            }
        }

        /// @brief WCHAR* / char* のどちらかに入っている名前を std::string にする
        /// @details DRED のノードは名前を ANSI と WIDE の 2 系統で持ち、
        ///          SetName / SetPrivateData のどちらを使ったかで入る側が変わる。
        std::string PickName(const char* narrow, const wchar_t* wide)
        {
            if (narrow && narrow[0] != '\0') {
                return std::string(narrow);
            }
            if (wide && wide[0] != L'\0') {
                const int needed = ::WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
                if (needed > 1) {
                    std::string out(static_cast<size_t>(needed - 1), '\0');
                    ::WideCharToMultiByte(CP_UTF8, 0, wide, -1, out.data(), needed, nullptr, nullptr);
                    return out;
                }
            }
            return "(名前なし)";
        }

        /// @brief パンくずを 1 ノード分ログへ出す
        /// @details 完了数（pLastBreadcrumbValue）が「GPU がここまでは終えた」の意味なので、
        ///          その次の命令が **落ちた命令** の第一候補になる。
        ///          全部出すと数千行になるので、完了点の前後だけを窓で切って出す。
        void LogBreadcrumbNode(const D3D12_AUTO_BREADCRUMB_NODE1* node, uint32_t nodeIndex)
        {
            Logger& logger = Logger::GetInstance();

            const uint32_t total = node->BreadcrumbCount;
            const uint32_t completed = node->pLastBreadcrumbValue ? *node->pLastBreadcrumbValue : 0;

            // 全命令が完了しているノードは落ちていない（犯人ではない）
            const bool finished = (completed >= total);

            logger.Errorf(LogCategory::Graphics, LogSubCategory::Device,
                "  [{}] CommandList=\"{}\" Queue=\"{}\" 完了 {}/{} {}",
                nodeIndex,
                PickName(node->pCommandListDebugNameA, node->pCommandListDebugNameW),
                PickName(node->pCommandQueueDebugNameA, node->pCommandQueueDebugNameW),
                completed, total,
                finished ? "（このリストは完走している）" : "★ここで止まった★");

            if (finished || total == 0 || node->pCommandHistory == nullptr) {
                return;
            }

            // 完了点の前後を出す（直前 8 件 → 未完了 4 件）
            constexpr uint32_t kBefore = 8;
            constexpr uint32_t kAfter = 4;
            const uint32_t begin = (completed > kBefore) ? (completed - kBefore) : 0;
            const uint32_t end = (completed + kAfter < total) ? (completed + kAfter) : total;

            for (uint32_t i = begin; i < end; ++i) {
                // PIX マーカー等の文脈文字列が付いている命令はそれも出す
                std::string context;
                for (uint32_t c = 0; c < node->BreadcrumbContextsCount; ++c) {
                    if (node->pBreadcrumbContexts[c].BreadcrumbIndex == i) {
                        context = " <" + PickName(nullptr, node->pBreadcrumbContexts[c].pContextString) + ">";
                        break;
                    }
                }

                logger.Errorf(LogCategory::Graphics, LogSubCategory::Device,
                    "      {} #{:<5} {}{}",
                    (i < completed) ? "  " : ((i == completed) ? "->" : "  "),
                    i, ToString(node->pCommandHistory[i]), context);
            }
        }

        /// @brief ページフォルト周辺のリソース一覧をログへ出す
        void LogAllocationList(const D3D12_DRED_ALLOCATION_NODE1* head, const char* title)
        {
            if (!head) {
                return;
            }
            Logger& logger = Logger::GetInstance();
            logger.Errorf(LogCategory::Graphics, LogSubCategory::Device, "  {}", title);

            uint32_t index = 0;
            for (const D3D12_DRED_ALLOCATION_NODE1* node = head; node != nullptr; node = node->pNext) {
                logger.Errorf(LogCategory::Graphics, LogSubCategory::Device,
                    "      [{}] {} \"{}\"",
                    index++, ToString(node->AllocationType),
                    PickName(node->ObjectNameA, node->ObjectNameW));
                // 数百件になることがあるので上限を設ける
                if (index >= 32) {
                    logger.Errorf(LogCategory::Graphics, LogSubCategory::Device,
                        "      …（以降は省略）");
                    break;
                }
            }
        }
    }

    bool EnableDeviceRemovedExtendedData()
    {
        ComPtr<ID3D12DeviceRemovedExtendedDataSettings1> settings;
        if (FAILED(D3D12GetDebugInterface(IID_PPV_ARGS(&settings)))) {
            Logger::GetInstance().Warnf(LogCategory::Graphics, LogSubCategory::Device,
                "DRED: この環境では利用できません（OS / ドライバが非対応）");
            return false;
        }

        // FORCED_ON = 「デバッグレイヤの有無に関わらず必ず記録する」
        settings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
        settings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);

        Logger::GetInstance().Infof(LogCategory::Graphics, LogSubCategory::Device,
            "DRED: 有効化しました（Auto-Breadcrumbs / PageFault）。"
            "GPU クラッシュ時に落ちた命令とリソース名がログへ出ます");
        return true;
    }

    bool ReportIfDeviceRemoved(ID3D12Device* device, std::string_view whenLabel)
    {
        if (!device) {
            return false;
        }

        const HRESULT reason = device->GetDeviceRemovedReason();
        if (SUCCEEDED(reason)) {
            return false; // 正常。ここが毎フレーム通る道なので早期に返す
        }

        Logger& logger = Logger::GetInstance();
        logger.Errorf(LogCategory::Graphics, LogSubCategory::Device,
            "================ GPU クラッシュ（デバイスロスト）================");
        logger.Errorf(LogCategory::Graphics, LogSubCategory::Device,
            "検出地点: {}", whenLabel);
        logger.Errorf(LogCategory::Graphics, LogSubCategory::Device,
            "理由: 0x{:08X} {}", static_cast<uint32_t>(reason), ReasonToString(reason));

        ComPtr<ID3D12DeviceRemovedExtendedData1> dred;
        if (FAILED(device->QueryInterface(IID_PPV_ARGS(&dred)))) {
            logger.Errorf(LogCategory::Graphics, LogSubCategory::Device,
                "DRED データを取得できません。config の enableDRED を true にして再現させると"
                "「どの命令で落ちたか」まで分かります");
            logger.Flush();
            return true;
        }

        // ── どの命令まで完了していたか ──────────────────────────
        D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 breadcrumbs{};
        if (SUCCEEDED(dred->GetAutoBreadcrumbsOutput1(&breadcrumbs))) {
            logger.Errorf(LogCategory::Graphics, LogSubCategory::Device,
                "-- Auto-Breadcrumbs（GPU が実行していた命令）--");
            uint32_t nodeIndex = 0;
            for (const D3D12_AUTO_BREADCRUMB_NODE1* node = breadcrumbs.pHeadAutoBreadcrumbNode;
                node != nullptr; node = node->pNext) {
                LogBreadcrumbNode(node, nodeIndex++);
            }
            if (nodeIndex == 0) {
                logger.Errorf(LogCategory::Graphics, LogSubCategory::Device,
                    "  （記録なし。DRED が無効のまま落ちた可能性があります）");
            }
        }

        // ── どのアドレスを踏んだか ──────────────────────────────
        D3D12_DRED_PAGE_FAULT_OUTPUT1 pageFault{};
        if (SUCCEEDED(dred->GetPageFaultAllocationOutput1(&pageFault)) && pageFault.PageFaultVA != 0) {
            logger.Errorf(LogCategory::Graphics, LogSubCategory::Device,
                "-- PageFault（GPU が触れなかった仮想アドレス）--");
            logger.Errorf(LogCategory::Graphics, LogSubCategory::Device,
                "  VA = 0x{:016X}", static_cast<uint64_t>(pageFault.PageFaultVA));
            LogAllocationList(pageFault.pHeadExistingAllocationNode,
                "このアドレス付近に存在するリソース:");
            LogAllocationList(pageFault.pHeadRecentFreedAllocationNode,
                "このアドレス付近で最近解放されたリソース（解放後に使った疑い）:");
        }

        logger.Errorf(LogCategory::Graphics, LogSubCategory::Device,
            "==============================================================");

        // この直後にプロセスが死んでもディスクに残るよう、ここで確実に吐き出す
        logger.Flush();
        return true;
    }
}
