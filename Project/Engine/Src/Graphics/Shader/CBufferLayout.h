#pragma once

//========================================================================================
// CBufferLayout.h
//
// HLSL の cbuffer packing 規則（メンバは 16 バイト境界をまたげず、またぐ場合は次の境界へ
// 押し出される）を C++ 側で再現し、定数バッファ構造体の配置がシェーダーと食い違ったら
// ビルドを失敗させる。C++ の構造体レイアウト規則とは別物なので、MSVC の C4820
// （C++ パディング警告）では検出できない。
//
//   struct MyConstants { Matrix4x4 viewProj; Vector3 sunDirection; float time; };
//
//   static constexpr Cb::Field kMyConstantsFields[] = {
//       CB_FIELD(MyConstants, viewProj), CB_FIELD(MyConstants, sunDirection),
//       CB_FIELD(MyConstants, time),
//   };
//   CB_VERIFY_LAYOUT(MyConstants, kMyConstantsFields);
//
// HLSL の型は C++ のメンバ型から推論する（float3 / int3 / uint3 はレイアウト上すべて同一な
// ので書き分ける必要が無い）。推論できないものだけ CB_FIELD_AS で明示する。
//
// 検証されるのは「各メンバのオフセット」「各メンバの型サイズ」「全体サイズ」の 3 点。
// 表に書き漏らすと全体サイズ検査が必ず落ちるので、この表は腐らない。
//
// 背景・エラーの読み方・段階③④（パディング撤廃 / シェーダー実体との照合）は
//   Docs/Engine/Graphics/Shader/CBufferLayout_Verification.md
//========================================================================================

#include "Math/Matrix/Matrix4x4.h"
#include "Math/Vector/Vector2.h"
#include "Math/Vector/Vector3.h"
#include "Math/Vector/Vector4.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace CoreEngine::Cb {

    constexpr size_t AlignUp(size_t value, size_t alignment) {
        return (value + alignment - 1) / alignment * alignment;
    }

    /// @brief HLSL 側の型 1 個分の性質
    struct TypeDesc {
        size_t hlslSize; ///< HLSL 上で占めるバイト数
        size_t align;    ///< 開始境界
        bool   bump;     ///< 16B 境界をまたぐと次の境界へ押し出される型か
        size_t cppSize;  ///< C++ 側で期待される sizeof

        // ---- 転送（Cb::Upload）用 ----
        // HLSL の配列は 1 要素ごとに 16B 単位を占めるので、C++ の詰まった配列から
        // まとめて 1 回でコピーはできず、1 要素ずつ運ぶ必要がある
        size_t elementCount;   ///< 配列でなければ 1
        size_t elementStride;  ///< HLSL 側の 1 要素あたりの間隔
        size_t elementCppSize; ///< C++ 側の 1 要素あたりのサイズ
    };

    /// @brief 配列でないスカラー / ベクトル型を作る
    constexpr TypeDesc Simple(size_t bytes, size_t align = 4, bool bump = true) {
        return TypeDesc{ bytes, align, bump, bytes, 1, bytes, bytes };
    }

    //--------------------------------------------------
    // HLSL の型。テンプレートではなく、ただの定数
    //--------------------------------------------------
    inline constexpr TypeDesc Float = Simple(4);
    inline constexpr TypeDesc Float2 = Simple(8);
    inline constexpr TypeDesc Float3 = Simple(12);
    inline constexpr TypeDesc Float4 = Simple(16);
    inline constexpr TypeDesc Int = Simple(4);
    inline constexpr TypeDesc Int2 = Simple(8);
    inline constexpr TypeDesc Int3 = Simple(12);
    inline constexpr TypeDesc Int4 = Simple(16);
    inline constexpr TypeDesc Uint = Simple(4);
    inline constexpr TypeDesc Uint2 = Simple(8);
    inline constexpr TypeDesc Uint3 = Simple(12);
    inline constexpr TypeDesc Uint4 = Simple(16);
    /// HLSL の bool は cbuffer 上 4 バイト。C++ 側は int32_t / uint32_t を使うこと
    inline constexpr TypeDesc Bool = Simple(4);
    inline constexpr TypeDesc Float4x4 = Simple(64, 16, false);

    /// @brief HLSL の配列（1 要素ごとに 16B 単位を占める）
    constexpr TypeDesc Array(TypeDesc element, size_t count) {
        const size_t stride = AlignUp(element.hlslSize, 16);
        return TypeDesc{
            (count - 1) * stride + element.hlslSize,
            16,
            false,
            count * element.cppSize,
            count,
            stride,
            element.cppSize,
        };
    }

    /// @brief 入れ子の構造体（16B 境界開始・サイズは 16B 切り上げ）
    /// @param cppSizeof その型の sizeof。型自身が CB_VERIFY_LAYOUT 済みなら
    ///                  sizeof == HLSL サイズが保証されているのでそのまま使える
    constexpr TypeDesc Struct(size_t cppSizeof) {
        return TypeDesc{
            AlignUp(cppSizeof, 16), 16, false, cppSizeof,
            1, AlignUp(cppSizeof, 16), cppSizeof,
        };
    }

    //========================================================================================
    // C++ の型 → HLSL の型の推論
    //
    // レイアウト上、HLSL の float3 / int3 / uint3 はすべて「12 バイト・4B 境界・16B またぎ押し出し」で
    // 同一なので、成分の型を書き分ける必要が無い。つまり C++ のメンバ型だけで HLSL 型が決まる。
    // そのため CB_FIELD は型を書かせない。
    //
    // 推論できない型（float[16] を float4x4 として使う等）はコンパイルエラーになるので、
    // その場合だけ CB_FIELD_AS で明示する。
    //========================================================================================

    /// @brief 配列メンバの推論
    template<class T, size_t N>
    constexpr TypeDesc DeduceArray() {
        if constexpr (std::is_class_v<T>) {
            // 構造体の配列 = HLSL の配列（1 要素ごとに 16B 単位）
            return Array(Struct(sizeof(T)), N);
        } else {
            // スカラーが 2〜4 個並んでいるものは HLSL のベクトル（float3 等）
            static_assert(N >= 2 && N <= 4 && sizeof(T) == 4,
                "[CBufferLayout] この配列は HLSL 型を推論できません。"
                "float[16] を float4x4 として使う場合などは CB_FIELD_AS で明示してください");
            return Simple(sizeof(T) * N);
        }
    }

    /// @brief C++ の型 → HLSL の型
    template<class T> struct HlslTypeOf {
        static_assert(std::is_arithmetic_v<T> && sizeof(T) == 4,
            "[CBufferLayout] この型は HLSL 型を推論できません。"
            "bool は使えません（HLSL では 4 バイトなので int32_t / uint32_t にすること）。"
            "意図的な型を使いたい場合は CB_FIELD_AS で明示してください");
        static constexpr TypeDesc kType = Simple(4);
    };

    template<> struct HlslTypeOf<Vector2> { static constexpr TypeDesc kType = Float2; };
    template<> struct HlslTypeOf<Vector3> { static constexpr TypeDesc kType = Float3; };
    template<> struct HlslTypeOf<Vector4> { static constexpr TypeDesc kType = Float4; };
    template<> struct HlslTypeOf<Matrix4x4> { static constexpr TypeDesc kType = Float4x4; };

    template<class T, size_t N> struct HlslTypeOf<T[N]> {
        static constexpr TypeDesc kType = DeduceArray<T, N>();
    };
    template<class T, size_t N> struct HlslTypeOf<std::array<T, N>> {
        static constexpr TypeDesc kType = DeduceArray<T, N>();
    };

    /// @brief フィールド 1 個分。CB_FIELD マクロが作る
    struct Field {
        TypeDesc    hlsl;      ///< HLSL 側の型
        size_t      cppOffset; ///< C++ 側の実オフセット（offsetof）
        size_t      cppSize;   ///< C++ 側の実サイズ（sizeof）
        const char* name;      ///< メンバ名（将来のシェーダーリフレクション照合用）
    };

    /// @brief HLSL cbuffer のメンバ配置規則そのもの
    /// @param cursor 直前のメンバの終端オフセット
    constexpr size_t Place(size_t cursor, const TypeDesc& type) {
        size_t offset = AlignUp(cursor, type.align);
        if (type.bump && type.hlslSize > 0) {
            // 16 バイト境界をまたぐなら次の境界へ押し出す（HLSL 固有の規則）
            if ((offset / 16) != ((offset + type.hlslSize - 1) / 16)) {
                offset = AlignUp(offset, 16);
            }
        }
        return offset;
    }

    /// @brief 検証結果。丸ごとテンプレート引数にして、失敗時に実値を診断へ出す
    struct Report {
        size_t badFieldIndex;     ///< 食い違ったフィールド番号（0 始まり）。無ければフィールド数
        size_t actualOffset;
        size_t expectedOffset;
        size_t actualFieldSize;
        size_t expectedFieldSize;
        size_t actualTotalSize;
        size_t expectedTotalSize;
    };

    /// @brief 詰め込み規則を適用した実効の型情報を返す
    constexpr TypeDesc EffectiveType(const Field& field, bool tight) {
        return tight
            ? TypeDesc{ field.hlsl.cppSize, 4, false, field.hlsl.cppSize,
                        field.hlsl.elementCount, field.hlsl.elementCppSize, field.hlsl.elementCppSize }
            : field.hlsl;
    }

    /// @brief フィールド表と実際の構造体を突き合わせる
    /// @param tight true = StructuredBuffer / 頂点バッファの詰め込み規則
    ///              （16B 境界またぎの押し出しも全体 16B 切り上げも無い）
    constexpr Report Check(const Field* fields, size_t count, size_t structSize, bool tight = false) {
        Report report{ count, 0, 0, 0, 0, structSize, 0 };
        size_t cursor = 0;
        size_t maxAlign = 1;

        for (size_t i = 0; i < count; ++i) {
            const TypeDesc type = EffectiveType(fields[i], tight);
            const size_t expectedOffset = Place(cursor, type);

            // 最初に食い違ったフィールドだけ報告する（後続は連鎖するだけなので）
            if (report.badFieldIndex == count &&
                (fields[i].cppOffset != expectedOffset || fields[i].cppSize != type.cppSize)) {
                report.badFieldIndex = i;
                report.actualOffset = fields[i].cppOffset;
                report.expectedOffset = expectedOffset;
                report.actualFieldSize = fields[i].cppSize;
                report.expectedFieldSize = type.cppSize;
            }

            cursor = expectedOffset + type.hlslSize;
            if (type.align > maxAlign) {
                maxAlign = type.align;
            }
        }

        report.expectedTotalSize = AlignUp(cursor, tight ? maxAlign : 16);
        return report;
    }

    template<size_t N>
    constexpr Report Check(const Field(&fields)[N], size_t structSize, bool tight = false) {
        return Check(fields, N, structSize, tight);
    }

    /// @brief 型だけを検査する（Cb::Upload で転送する「パディング無し構造体」用）
    /// @details 転送でオフセットを合わせるので C++ 側のレイアウトは HLSL と一致しない。
    ///          それでも「bool を使っている」「配列長が違う」は検出したいのでサイズだけ見る。
    constexpr Report CheckTypesOnly(const Field* fields, size_t count, bool tight = false) {
        Report report{ count, 0, 0, 0, 0, 0, 0 };
        for (size_t i = 0; i < count; ++i) {
            const size_t expected = EffectiveType(fields[i], tight).cppSize;
            if (report.badFieldIndex == count && fields[i].cppSize != expected) {
                report.badFieldIndex = i;
                report.actualFieldSize = fields[i].cppSize;
                report.expectedFieldSize = expected;
            }
        }
        return report;
    }

    template<size_t N>
    constexpr Report CheckTypesOnly(const Field(&fields)[N], bool tight = false) {
        return CheckTypesOnly(fields, N, tight);
    }

    /// @brief i 番目のフィールドが HLSL 上で始まるオフセット
    /// @note シェーダーリフレクションとの突き合わせ（CBufferReflectionCheck）から実行時に呼ぶ
    constexpr size_t ExpectedOffsetOf(const Field* fields, size_t count, size_t index, bool tight = false) {
        size_t cursor = 0;
        for (size_t i = 0; i < count; ++i) {
            const TypeDesc type = EffectiveType(fields[i], tight);
            const size_t offset = Place(cursor, type);
            if (i == index) {
                return offset;
            }
            cursor = offset + type.hlslSize;
        }
        return 0;
    }

    /// @brief HLSL 上での全体サイズ
    constexpr size_t ExpectedTotalSize(const Field* fields, size_t count, bool tight = false) {
        return Check(fields, count, 0, tight).expectedTotalSize;
    }

    /// @brief C++ 構造体を HLSL の cbuffer レイアウトへ詰め替えて書き込む
    /// @details パディング無しの素直な構造体のまま GPU へ送れる（手打ちパディングが不要になる）。
    ///          代償はフィールド単位の memcpy なので、オブジェクトごとに毎フレーム何千回も書く
    ///          バッファ（TransformationMatrix 等）は 1:1 レイアウト＋一括コピーのままにすること。
    /// @param dst    マップ済みの定数バッファ先頭
    /// @param src    C++ 構造体の先頭
    /// @param fields フィールド表
    inline void Upload(void* dst, const void* src, const Field* fields, size_t count, bool tight = false) {
        auto* destination = static_cast<std::byte*>(dst);
        const auto* source = static_cast<const std::byte*>(src);

        for (size_t i = 0; i < count; ++i) {
            const TypeDesc type = EffectiveType(fields[i], tight);
            const size_t offset = ExpectedOffsetOf(fields, count, i, tight);

            for (size_t element = 0; element < type.elementCount; ++element) {
                std::memcpy(
                    destination + offset + element * type.elementStride,
                    source + fields[i].cppOffset + element * type.elementCppSize,
                    type.elementCppSize);
            }
        }
    }

    template<class T, size_t N>
    void Upload(void* dst, const T& src, const Field(&fields)[N], bool tight = false) {
        Upload(dst, &src, fields, N, tight);
    }

    /// @brief HLSL 上での全体サイズ（転送先バッファの確保サイズに使う）
    template<size_t N>
    constexpr size_t HlslSizeOf(const Field(&fields)[N], bool tight = false) {
        return ExpectedTotalSize(fields, N, tight);
    }

    //--------------------------------------------------
    // 検査ごとに型を分けてある。失敗したときテンプレート実引数に実値が出るので、
    // 診断のテンプレート名を見れば「何が」「いくつずれたか」がそのまま読める
    //--------------------------------------------------

    template<size_t FieldIndex, size_t ActualOffset, size_t ExpectedOffset>
    struct FieldOffsetMismatch {
        static_assert(ActualOffset == ExpectedOffset,
            "[CBufferLayout] オフセット不一致。実引数は <フィールド番号, 実オフセット, 期待オフセット>。"
            "フィールド表のその番号（0 始まり）の行を見て、手前にパディングを足すか宣言順を直すこと");
        static constexpr bool kOk = true;
    };

    template<size_t FieldIndex, size_t ActualSize, size_t ExpectedSize>
    struct FieldSizeMismatch {
        static_assert(ActualSize == ExpectedSize,
            "[CBufferLayout] サイズ不一致。実引数は <フィールド番号, C++ の実サイズ, HLSL 型が要求するサイズ>。"
            "bool を使っている / 配列長が違う / HLSL 型の書き間違い のいずれか");
        static constexpr bool kOk = true;
    };

    template<size_t ActualSize, size_t ExpectedSize>
    struct TotalSizeMismatch {
        static_assert(ActualSize == ExpectedSize,
            "[CBufferLayout] 全体サイズ不一致。実引数は <構造体の実サイズ, HLSL 側のサイズ>。"
            "フィールド表の書き漏れ、または末尾パディング不足");
        static constexpr bool kOk = true;
    };

    template<Report R>
    struct Verify {
        static constexpr bool kOk =
            FieldOffsetMismatch<R.badFieldIndex, R.actualOffset, R.expectedOffset>::kOk &&
            FieldSizeMismatch<R.badFieldIndex, R.actualFieldSize, R.expectedFieldSize>::kOk &&
            TotalSizeMismatch<R.actualTotalSize, R.expectedTotalSize>::kOk;
    };

} // namespace CoreEngine::Cb

/// フィールド表を短く書くためのエイリアス
namespace Cb = CoreEngine::Cb;

//========================================================================================
// マクロはこの 3 つだけ
//========================================================================================

/// @brief フィールド表の 1 行を作る（HLSL 型は C++ のメンバ型から推論する）
/// @param TYPE   構造体の型
/// @param MEMBER メンバ名
#define CB_FIELD(TYPE, MEMBER)                                              \
    ::CoreEngine::Cb::Field{                                                \
        ::CoreEngine::Cb::HlslTypeOf<decltype(TYPE::MEMBER)>::kType,        \
        offsetof(TYPE, MEMBER), sizeof(TYPE::MEMBER), #MEMBER }

/// @brief 推論と違う HLSL 型として扱いたいときだけ使う（float[16] を float4x4 として使う等）
#define CB_FIELD_AS(TYPE, MEMBER, HLSL) \
    ::CoreEngine::Cb::Field{ HLSL, offsetof(TYPE, MEMBER), sizeof(TYPE::MEMBER), #MEMBER }

/// @brief 定数バッファ構造体のレイアウトを HLSL cbuffer の packing 規則で検証する
#define CB_VERIFY_LAYOUT(TYPE, FIELDS)                                            \
    static_assert(                                                                \
        ::CoreEngine::Cb::Verify<::CoreEngine::Cb::Check(FIELDS, sizeof(TYPE))>::kOk, \
        "[CBufferLayout] " #TYPE " のレイアウトが HLSL cbuffer と一致しません")

/// @brief Cb::Upload で転送する「パディング無し構造体」を検証する
/// @note 転送でオフセットを合わせるので C++ 側のレイアウト一致は要求しない。
///       型の取り違え（bool 混入・配列長違い）だけを見る。
///       HLSL 側との突き合わせは CB_BIND_HLSL（実行時リフレクション照合）が担当する。
#define CB_VERIFY_TYPES(TYPE, FIELDS)                                             \
    static_assert(                                                                \
        ::CoreEngine::Cb::Verify<::CoreEngine::Cb::CheckTypesOnly(FIELDS)>::kOk,  \
        "[CBufferLayout] " #TYPE " のフィールド型指定が誤っています")

/// @brief StructuredBuffer / 頂点バッファの要素構造体を「詰め込み規則」で検証する
/// @note cbuffer と違い 16B 境界またぎの押し出しも全体 16B 切り上げも無い。
///       型の取り違え（bool 混入・配列長違い）と想定外の C++ パディングを検出する。
#define CB_VERIFY_STRIDE(TYPE, FIELDS)                                                  \
    static_assert(                                                                      \
        ::CoreEngine::Cb::Verify<::CoreEngine::Cb::Check(FIELDS, sizeof(TYPE), true)>::kOk, \
        "[CBufferLayout] " #TYPE " のレイアウトが HLSL 側と一致しません")
