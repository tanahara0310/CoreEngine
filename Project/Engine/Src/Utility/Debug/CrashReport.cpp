#include "pch.h"
#include "CrashReport.h"
#include <cpptrace/basic.hpp>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <exception>
#include <format>

#pragma comment(lib, "Dbghelp.lib")

namespace CoreEngine
{
    namespace
    {
        constexpr DWORD kCppExceptionCode = 0xE06D7363;
        constexpr DWORD kHeapCorruptionCode = 0xC0000374;
        constexpr std::size_t kMaxFrames = 128;

        // MSVC の x64 で、投げた C++ の例外が持つ型の表（位置はモジュールの先頭からのずれ）
        struct ThrowInfo {
            std::uint32_t attributes;
            std::int32_t unwind;
            std::int32_t forwardCompat;
            std::int32_t catchableTypeArray;
        };
        struct CatchableTypeArray {
            std::int32_t count;
            std::int32_t types[1];
        };
        struct CatchableType {
            std::uint32_t properties;
            std::int32_t type;
            std::int32_t mdisp;
            std::int32_t pdisp;
            std::int32_t vdisp;
            std::int32_t size;
            std::int32_t copyFunction;
        };
        struct TypeDescriptor {
            const void* vftable;
            void* spare;
            char name[1];
        };

        /// @brief std::exception を継いだ C++ の例外なら、what() の文を buffer へ写す
        bool CopyStdExceptionMessage(const EXCEPTION_RECORD& record, char* buffer, std::size_t size) noexcept
        {
            buffer[0] = '\0';
            if (record.ExceptionCode != kCppExceptionCode || record.NumberParameters < 4) {
                return false;
            }
            __try {
                const std::uintptr_t imageBase = record.ExceptionInformation[3];
                const auto* object = reinterpret_cast<const std::byte*>(record.ExceptionInformation[1]);
                const auto* throwInfo = reinterpret_cast<const ThrowInfo*>(record.ExceptionInformation[2]);
                if (!object || !throwInfo || throwInfo->catchableTypeArray == 0) {
                    return false;
                }
                const auto* types = reinterpret_cast<const CatchableTypeArray*>(imageBase + throwInfo->catchableTypeArray);
                for (std::int32_t i = 0; i < types->count; ++i) {
                    const auto* type = reinterpret_cast<const CatchableType*>(imageBase + types->types[i]);
                    const auto* descriptor = reinterpret_cast<const TypeDescriptor*>(imageBase + type->type);
                    if (type->pdisp < 0 && std::strcmp(descriptor->name, ".?AVexception@std@@") == 0) {
                        const auto* exception = reinterpret_cast<const std::exception*>(object + type->mdisp);
                        strncpy_s(buffer, size, exception->what(), _TRUNCATE);
                        return true;
                    }
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                buffer[0] = '\0';
            }
            return false;
        }

        /// @brief 例外の番号を言葉にする
        std::string DescribeCode(const EXCEPTION_RECORD& record)
        {
            const DWORD code = record.ExceptionCode;
            switch (code) {
            case EXCEPTION_ACCESS_VIOLATION:
            case EXCEPTION_IN_PAGE_ERROR: {
                if (record.NumberParameters < 2) {
                    return std::format("アクセス違反（0x{:08X}）", code);
                }
                const ULONG_PTR kind = record.ExceptionInformation[0];
                const char* action = (kind == 1) ? "書こう" : (kind == 8) ? "実行しよう" : "読もう";
                return std::format("アクセス違反（0x{:08X}）: 0x{:016X} を{}とした", code,
                    static_cast<std::uint64_t>(record.ExceptionInformation[1]), action);
            }
            case EXCEPTION_STACK_OVERFLOW:
                return std::format("スタックがあふれた（0x{:08X}）", code);
            case kCppExceptionCode:
                return std::format("C++ の例外がどこでも受け止められなかった（0x{:08X}）", code);
            case EXCEPTION_INT_DIVIDE_BY_ZERO:
                return std::format("整数を 0 で割った（0x{:08X}）", code);
            case EXCEPTION_ILLEGAL_INSTRUCTION:
                return std::format("実行できない命令（0x{:08X}）", code);
            case EXCEPTION_PRIV_INSTRUCTION:
                return std::format("特権命令（0x{:08X}）", code);
            case EXCEPTION_BREAKPOINT:
                return std::format("ブレークポイント（0x{:08X}）", code);
            case kHeapCorruptionCode:
                return std::format("ヒープが壊れている（0x{:08X}）", code);
            default:
                return std::format("例外 0x{:08X}", code);
            }
        }

        /// @brief UTF-16 の文字を UTF-8 へ
        std::string WideToUtf8(const wchar_t* text, int length)
        {
            const int size = ::WideCharToMultiByte(CP_UTF8, 0, text, length, nullptr, 0, nullptr, nullptr);
            std::string result(static_cast<std::size_t>(std::max(size, 0)), '\0');
            if (size > 0) {
                ::WideCharToMultiByte(CP_UTF8, 0, text, length, result.data(), size, nullptr, nullptr);
            }
            return result;
        }

        /// @brief システムの文字コード（dbghelp が返す名前とパス）を UTF-8 へ
        std::string AnsiToUtf8(const std::string& text)
        {
            if (text.empty()) {
                return {};
            }
            const int length = static_cast<int>(text.size());
            const int size = ::MultiByteToWideChar(CP_ACP, 0, text.data(), length, nullptr, 0);
            if (size <= 0) {
                return text;
            }
            std::wstring wide(static_cast<std::size_t>(size), L'\0');
            ::MultiByteToWideChar(CP_ACP, 0, text.data(), length, wide.data(), size);
            return WideToUtf8(wide.data(), size);
        }

        /// @brief 番地を「モジュールの名前+先頭からのずれ」で表す
        std::string DescribeModuleOffset(std::uintptr_t address)
        {
            HMODULE module = nullptr;
            if (!::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCWSTR>(address), &module)) {
                return std::format("0x{:016X}", static_cast<std::uint64_t>(address));
            }
            wchar_t path[MAX_PATH] = {};
            const DWORD length = ::GetModuleFileNameW(module, path, MAX_PATH);
            const wchar_t* name = path;
            for (DWORD i = 0; i < length; ++i) {
                if (path[i] == L'\\' || path[i] == L'/') {
                    name = path + i + 1;
                }
            }
            const std::uintptr_t offset = address - reinterpret_cast<std::uintptr_t>(module);
            return std::format("{}+0x{:X}", WideToUtf8(name, static_cast<int>(std::wcslen(name))),
                static_cast<std::uint64_t>(offset));
        }

        /// @brief 名前が引けたか（引けないと空か「??」になる）
        bool HasSymbolName(const std::string& symbol)
        {
            const std::size_t first = symbol.find_first_not_of(' ');
            if (first == std::string::npos) {
                return false;
            }
            const std::size_t last = symbol.find_last_not_of(' ');
            return symbol.compare(first, last - first + 1, "??") != 0;
        }

        /// @brief 落ちたときのレジスタから呼び出し元へさかのぼり、各段の番地を frames へ入れる
        /// @return 入れた段の数
        std::size_t WalkCallStack(const CONTEXT& crashContext, cpptrace::frame_ptr* frames, std::size_t capacity) noexcept
        {
            CONTEXT context = crashContext;
            std::size_t count = 0;
            __try {
                // 落ちた場所の段は番地が 0 でも入れる（空の関数ポインタを呼んだとき）
                while (count < capacity && (count == 0 || context.Rip != 0)) {
                    // 呼び出し元の段は、戻り先の 1 つ前（呼び出しの命令の中）で名前と行を引く
                    frames[count] = static_cast<cpptrace::frame_ptr>(count == 0 ? context.Rip : context.Rip - 1);
                    ++count;
                    const DWORD64 stackBefore = context.Rsp;
                    DWORD64 imageBase = 0;
                    if (const PRUNTIME_FUNCTION function = ::RtlLookupFunctionEntry(context.Rip, &imageBase, nullptr)) {
                        void* handlerData = nullptr;
                        DWORD64 establisherFrame = 0;
                        ::RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, context.Rip, function, &context, &handlerData,
                            &establisherFrame, nullptr);
                    } else {
                        // 巻き戻しの表が無い末端の関数は、スタックの先頭が戻り先
                        context.Rip = *reinterpret_cast<const DWORD64*>(context.Rsp);
                        context.Rsp += sizeof(DWORD64);
                    }
                    if (context.Rsp <= stackBefore) {
                        break;
                    }
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
            }
            return count;
        }

        /// @brief 名前の最後にある括弧の組（引数の型の並び）を外す
        std::string RemoveParameterList(const std::string& signature)
        {
            if (signature.empty() || signature.back() != ')') {
                return signature;
            }
            int depth = 0;
            for (std::size_t i = signature.size(); i-- > 0;) {
                if (signature[i] == ')') {
                    ++depth;
                } else if (signature[i] == '(' && --depth == 0) {
                    return signature.substr(0, i);
                }
            }
            return signature;
        }

        /// @brief 1 段を「関数  ファイル:行  [モジュール+ずれ]」の 1 行にする
        std::string DescribeFrame(std::size_t index, const cpptrace::stacktrace_frame& frame)
        {
            std::string line = std::format("#{:<4}", index);
            if (HasSymbolName(frame.symbol)) {
                // 行が分かった段は、cpptrace が名前の後ろに足した引数の型の並びを外す
                line += AnsiToUtf8(frame.line.has_value() ? RemoveParameterList(frame.symbol) : frame.symbol);
            } else {
                line += "（関数の名前が分からない）";
            }
            if (!frame.filename.empty()) {
                line += "  " + AnsiToUtf8(frame.filename);
                if (frame.line.has_value()) {
                    line += std::format(":{}", frame.line.value());
                }
            }
            line += "  [" + DescribeModuleOffset(static_cast<std::uintptr_t>(frame.raw_address)) + "]";
            return line;
        }
    }

    std::string CrashReport::DescribeException(const _EXCEPTION_POINTERS& exception)
    {
        const EXCEPTION_RECORD& record = *exception.ExceptionRecord;
        std::string text = DescribeCode(record);
        char message[1024];
        if (CopyStdExceptionMessage(record, message, sizeof(message)) && message[0] != '\0') {
            text += std::format("「{}」", message);
        }
        return text;
    }

    std::vector<std::string> CrashReport::DescribeCallStack(const _EXCEPTION_POINTERS& exception)
    {
        std::array<cpptrace::frame_ptr, kMaxFrames> frames{};
        const std::size_t count = WalkCallStack(*exception.ContextRecord, frames.data(), frames.size());
        cpptrace::raw_trace raw;
        raw.frames.assign(frames.begin(), frames.begin() + static_cast<std::ptrdiff_t>(count));
        if (raw.frames.empty()) {
            raw.frames.push_back(reinterpret_cast<cpptrace::frame_ptr>(exception.ExceptionRecord->ExceptionAddress));
        }

        const cpptrace::stacktrace trace = raw.resolve();
        std::vector<std::string> lines;
        lines.reserve(trace.frames.size() + 1);
        for (std::size_t i = 0; i < trace.frames.size();) {
            // 同じ番地の段が続くとき（自分を呼び続けたとき）は 1 行にまとめる
            std::size_t same = 1;
            while (i + same < trace.frames.size() && trace.frames[i + same].raw_address == trace.frames[i].raw_address) {
                ++same;
            }
            lines.push_back(DescribeFrame(i, trace.frames[i]));
            if (same > 1) {
                lines.push_back(std::format("      （同じ段があと {} 段続く）", same - 1));
            }
            i += same;
        }
        if (count == kMaxFrames) {
            lines.push_back(std::format("（{} 段で打ち切った。続きはダンプで見る）", kMaxFrames));
        }
        return lines;
    }
}
