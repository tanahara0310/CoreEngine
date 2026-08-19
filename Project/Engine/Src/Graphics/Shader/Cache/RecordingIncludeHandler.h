#pragma once

#include <dxcapi.h>

#include <atomic>
#include <filesystem>
#include <vector>

namespace CoreEngine
{
    /// @brief DXC の include ハンドラを包んで「実際に開けたファイル」を記録する
    /// @details シェーダキャッシュの依存追跡用。自前の #include スキャンでは取りこぼすため、
    ///          DXC が本当に開いたパスだけを weakly_canonical して重複排除しつつ集める。
    /// @note 寿命は所有者（ShaderCompiler）が持つので Release() で自身を delete しない。
    class RecordingIncludeHandler final : public IDxcIncludeHandler {
    public:
        RecordingIncludeHandler() = default;

        /// @brief 実処理を委譲する既定ハンドラを設定する
        void SetInner(IDxcIncludeHandler* inner) { inner_ = inner; }

        /// @brief 記録を空にする（1 コンパイルごとに呼ぶ）
        void Reset() { openedFiles_.clear(); }

        /// @brief 直前のコンパイルで開けた include ファイル一覧
        const std::vector<std::filesystem::path>& GetOpenedFiles() const { return openedFiles_; }

        // ===== IDxcIncludeHandler =====
        HRESULT STDMETHODCALLTYPE LoadSource(LPCWSTR fileName, IDxcBlob** includeSource) override;

        // ===== IUnknown =====
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override;
        ULONG STDMETHODCALLTYPE AddRef() override;
        ULONG STDMETHODCALLTYPE Release() override;

    private:
        IDxcIncludeHandler* inner_ = nullptr;
        std::vector<std::filesystem::path> openedFiles_;

        // 所有者は ShaderCompiler なので、この参照数が 0 になっても delete しない
        std::atomic<ULONG> referenceCount_{ 1 };
    };
}
