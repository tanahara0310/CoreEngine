#pragma once

struct ID3D12GraphicsCommandList;

namespace CoreEngine
{
    /// @brief PIX / GPU デバッガ向けのイベントマーカー
    /// @details 積んでおくとキャプチャ側でパス名の階層として見える。
    /// @note 実装は .cpp に閉じており、USE_PIX 未定義のビルドでは全て空関数になる。
    void BeginGpuMarker(ID3D12GraphicsCommandList* cmdList, const char* name);

    /// @brief BeginGpuMarker と対で呼ぶ
    void EndGpuMarker(ID3D12GraphicsCommandList* cmdList);

    /// @brief BeginGpuMarker / EndGpuMarker の RAII ラッパー
    class GpuMarkerScope
    {
    public:
        GpuMarkerScope(ID3D12GraphicsCommandList* cmdList, const char* name)
            : cmdList_((cmdList && name) ? cmdList : nullptr)
        {
            // Begin が省略された場合に End だけ呼んで階層が崩れないよう、
            // 実際に積んだときだけ cmdList_ を保持する。
            BeginGpuMarker(cmdList_, name);
        }
        ~GpuMarkerScope()
        {
            EndGpuMarker(cmdList_);
        }
        GpuMarkerScope(const GpuMarkerScope&) = delete;
        GpuMarkerScope& operator=(const GpuMarkerScope&) = delete;

    private:
        ID3D12GraphicsCommandList* cmdList_ = nullptr;
    };
}
