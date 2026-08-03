#pragma once

struct ID3D12GraphicsCommandList;

namespace CoreEngine
{
    /// @brief PIX / GPU デバッガ向けのイベントマーカー
    ///
    /// @details PIX キャプチャは既定では名前のない Dispatch / Draw の羅列になり、
    ///          どのパスの命令かを読み取れない。RenderGraph とコンピュートパスの
    ///          要所へマーカーを積むことで、キャプチャ側でパス名の階層として見える。
    ///
    ///          実装は .cpp 側に閉じており、pix3.h をヘッダーへ波及させない。
    ///          USE_PIX 未定義のビルド（Release）では全て空関数になる。
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
