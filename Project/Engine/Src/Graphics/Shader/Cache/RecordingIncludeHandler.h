#pragma once

#include <dxcapi.h>

#include <atomic>
#include <filesystem>
#include <vector>

namespace CoreEngine
{
    /// @brief DXC の include ハンドラを包んで「実際に開けたファイル」を記録する
    ///
    /// @details シェーダキャッシュの依存追跡に使う。`.hlsli` を編集したのに
    ///          キャッシュが無効化されないと、共通ヘッダで cbuffer レイアウトを変えたとき
    ///          「片方のシェーダだけ再コンパイルされて両者の解釈がずれる」という
    ///          極めて追いにくいバグになるため、依存の取りこぼしは許されない。
    ///          自前の #include スキャンではなく DXC が本当に開いたファイルを採る。
    ///
    /// @note DXC は `-I` を順に試すので **失敗する LoadSource が大量に来る**。
    ///       記録するのは成功した分だけ。
    /// @note 同じファイルが複数回開かれるので重複は除去する。
    /// @note 渡ってくるパスは正規化されていないので weakly_canonical を通す。
    ///
    /// @warning このクラスは ShaderCompiler がメンバとして所有する。
    ///          Release() で自身を delete しないのはそのため（COM の作法からは外れるが、
    ///          寿命は所有者が持つ設計にしてコンパイルごとのヒープ確保を避けている）。
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
