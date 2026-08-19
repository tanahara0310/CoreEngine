#pragma once

//========================================================================================
// CBufferReflectionCheck.h
//
// CBufferLayout.h の static_assert は「C++ 側が自己整合しているか」しか見ない。HLSL 側で
// メンバを増やした・並べ替えた場合は C++ がどれだけ正しくてもビルドが通ってしまうので、
// DXC のリフレクション情報から実際の cbuffer レイアウトを読み、フィールド表と突き合わせる。
// シェーダーロード時に 1 回だけ走る。Debug / Development のみ有効。
//
//   CB_BIND_HLSL(VignetteParams, kVignetteParamsFields, "VignetteParams");
//
// 第 3 引数は HLSL 側の cbuffer 名（ConstantBuffer<T> 形式なら変数名 "gAtmosphere" など）。
// 未登録の cbuffer は素通りするので段階的に増やしてよい。関数内で定義した構造体には書けない。
// 詳細: Docs/Engine/Graphics/Shader/CBufferLayout_Verification.md
//========================================================================================

#include "Graphics/Shader/CBufferLayout.h"

#include <d3d12shader.h>
#include <cstddef>
#include <iterator>
#include <string>

// Release 構成には USE_IMGUI が無い（Debug / Development にはある）ので、
// 開発ビルドだけで走らせるための判定に使う
#if defined(_DEBUG) || defined(USE_IMGUI)
#define CB_REFLECTION_CHECK_ENABLED 1
#else
#define CB_REFLECTION_CHECK_ENABLED 0
#endif

namespace CoreEngine::Cb {

    /// @brief HLSL の cbuffer と C++ のフィールド表の対応 1 件
    struct HlslBinding {
        const char*  hlslName = "";    ///< HLSL 側の cbuffer 名（ConstantBuffer<T> なら変数名）
        const char*  cppName = "";     ///< C++ 側の構造体名（ログ用）
        const Field* fields = nullptr; ///< フィールド表
        size_t       fieldCount = 0;
        size_t       cppSize = 0;      ///< sizeof(構造体)
        bool         tight = false;    ///< true = StructuredBuffer / 頂点バッファの詰め込み規則
    };

    /// @brief 対応表へ登録する。フィールド表の隣に static inline で置く（CB_BIND_HLSL 経由）
    struct Registrar {
        /// @brief 静的初期化時にバインド情報をレジストリへ登録する
        explicit Registrar(const HlslBinding& binding);
    };

    /// @brief シェーダーの cbuffer 1 個を、登録済みの C++ フィールド表と突き合わせる
    /// @param cbuffer    DXC リフレクションの cbuffer
    /// @param bufferDesc その cbuffer の desc（Name / Size / Variables）
    /// @param shaderName ログ用のシェーダー識別名
    /// @return 一致した、または未登録なら true。食い違ったら false（ログ出力済み）
    bool CheckAgainstReflection(
        ID3D12ShaderReflectionConstantBuffer* cbuffer,
        const D3D12_SHADER_BUFFER_DESC& bufferDesc,
        const std::string& shaderName);

    /// @brief 登録済み対応の件数（テスト・診断用）
    size_t RegisteredBindingCount();

} // namespace CoreEngine::Cb

#define CB_DETAIL_BIND_CONCAT2(a, b) a##b
#define CB_DETAIL_BIND_CONCAT(a, b) CB_DETAIL_BIND_CONCAT2(a, b)

#if CB_REFLECTION_CHECK_ENABLED

/// @brief フィールド表を HLSL の cbuffer 名と結びつける（定数バッファ用）
/// @note 変数名は __COUNTER__ で作る。__LINE__ だと別ヘッダーの同じ行番号が
///       同一 namespace に落ちたとき再定義エラーになる
#define CB_BIND_HLSL(TYPE, FIELDS, HLSL_NAME)                                    \
    static inline const ::CoreEngine::Cb::Registrar                              \
        CB_DETAIL_BIND_CONCAT(kCbHlslBinding_, __COUNTER__) {                    \
            ::CoreEngine::Cb::HlslBinding{                                       \
                HLSL_NAME, #TYPE, FIELDS, std::size(FIELDS), sizeof(TYPE), false } }

/// @brief StructuredBuffer / 頂点バッファ要素用
#define CB_BIND_HLSL_STRIDE(TYPE, FIELDS, HLSL_NAME)                             \
    static inline const ::CoreEngine::Cb::Registrar                              \
        CB_DETAIL_BIND_CONCAT(kCbHlslBinding_, __COUNTER__) {                    \
            ::CoreEngine::Cb::HlslBinding{                                       \
                HLSL_NAME, #TYPE, FIELDS, std::size(FIELDS), sizeof(TYPE), true } }

#else

// Release では登録そのものを消す（実行時チェックが無いので保持する意味が無い）
#define CB_BIND_HLSL(TYPE, FIELDS, HLSL_NAME)        static_assert(true, "")
#define CB_BIND_HLSL_STRIDE(TYPE, FIELDS, HLSL_NAME) static_assert(true, "")

#endif
