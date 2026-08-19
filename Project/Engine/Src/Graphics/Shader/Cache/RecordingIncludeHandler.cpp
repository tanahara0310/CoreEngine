#include "pch.h"
#include "RecordingIncludeHandler.h"

#include <algorithm>

namespace CoreEngine
{
    HRESULT STDMETHODCALLTYPE RecordingIncludeHandler::LoadSource(
        LPCWSTR fileName, IDxcBlob** includeSource)
    {
        if (!inner_) {
            return E_FAIL;
        }

        const HRESULT hr = inner_->LoadSource(fileName, includeSource);

        // 失敗した探索は依存ではない（DXC は -I を順に試すので空振りが大量に来る）
        if (SUCCEEDED(hr) && fileName) {
            std::error_code errorCode;
            std::filesystem::path resolved =
                std::filesystem::weakly_canonical(std::filesystem::path(fileName), errorCode);
            if (errorCode) {
                resolved = std::filesystem::path(fileName);
            }

            // 同じヘッダは何度も開かれる。依存一覧としては 1 回あれば足りる
            if (std::find(openedFiles_.begin(), openedFiles_.end(), resolved) == openedFiles_.end()) {
                openedFiles_.push_back(std::move(resolved));
            }
        }

        return hr;
    }

    HRESULT STDMETHODCALLTYPE RecordingIncludeHandler::QueryInterface(REFIID riid, void** object)
    {
        if (!object) {
            return E_POINTER;
        }

        if (riid == __uuidof(IDxcIncludeHandler) || riid == __uuidof(IUnknown)) {
            *object = static_cast<IDxcIncludeHandler*>(this);
            AddRef();
            return S_OK;
        }

        *object = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE RecordingIncludeHandler::AddRef()
    {
        return ++referenceCount_;
    }

    ULONG STDMETHODCALLTYPE RecordingIncludeHandler::Release()
    {
        // 実体は ShaderCompiler のメンバなのでここでは解放しない（クラス説明の warning 参照）
        const ULONG remaining = --referenceCount_;
        return remaining;
    }
}
