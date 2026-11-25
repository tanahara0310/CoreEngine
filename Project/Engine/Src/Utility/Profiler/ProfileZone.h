#pragma once

/// @file ProfileZone.h
/// @brief Tracy による区間の計測
/// @details エディタのあるビルド（Debug・Development）だけで Tracy を使い、Release では全部何もしない。
///          Tracy のヘッダは必ずこのファイルから読む（Tracy の設定をここだけで決め、
///          Tracy の本体の TracyClientUnit.cpp も同じ設定でコンパイルする）。
///          設定は次の 2 つ。
///          - TRACY_ON_DEMAND … Tracy の画面がつないでいる間だけ記録する（つないでいなければほぼ止まっている）
///          - TRACY_ONLY_LOCALHOST … 同じマシンからだけつなげる
///          CORE_NO_TRACY を定義すると、エディタのあるビルドでも Tracy を外す。

#if defined(CORE_EDITOR) && !defined(CORE_NO_TRACY)
#define CORE_TRACY_ENABLED 1
#else
#define CORE_TRACY_ENABLED 0
#endif

#if CORE_TRACY_ENABLED

#ifndef TRACY_ENABLE
#define TRACY_ENABLE
#endif
#ifndef TRACY_ON_DEMAND
#define TRACY_ON_DEMAND
#endif
#ifndef TRACY_ONLY_LOCALHOST
#define TRACY_ONLY_LOCALHOST
#endif

#pragma warning(push)
#pragma warning(disable : 4018 4100 4127 4189 4244 4245 4267 4324 4456 4457 4458 4459 4505 4702 4706 4996)
#include "Externals/tracy/public/tracy/Tracy.hpp"
#pragma warning(pop)

#define CORE_PROFILE_CONCAT_INNER(a, b) a##b
#define CORE_PROFILE_CONCAT(a, b) CORE_PROFILE_CONCAT_INNER(a, b)

/// @brief この行からスコープの終わりまでを 1 区間として記録する（名前は文字列リテラル）
#define CORE_PROFILE_ZONE(name) ZoneScopedN(name)

/// @brief CORE_PROFILE_ZONE の、名前が実行時に決まる版（const char*。区間の間だけ生きていればよい）
#define CORE_PROFILE_ZONE_TRANSIENT(name) \
    ZoneTransientN(CORE_PROFILE_CONCAT(coreProfileZone_, __LINE__), (name), true)

/// @brief 1 フレームの区切り（Present の後で 1 回）
#define CORE_PROFILE_FRAME() FrameMark

/// @brief Tracy の時間軸へ文字列を残す（text は size バイト）
#define CORE_PROFILE_MESSAGE(text, size) TracyMessage((text), (size))

/// @brief Tracy の画面がつないでいるか
#define CORE_PROFILE_IS_CONNECTED() (TracyIsConnected)

#else

#define CORE_PROFILE_ZONE(name) ((void)0)
#define CORE_PROFILE_ZONE_TRANSIENT(name) ((void)0)
#define CORE_PROFILE_FRAME() ((void)0)
#define CORE_PROFILE_MESSAGE(text, size) ((void)0)
#define CORE_PROFILE_IS_CONNECTED() (false)

#endif
