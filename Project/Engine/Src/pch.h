#pragma once

// ─── Windows / システム ───────────────────────────────────────
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <timeapi.h>

// ─── DirectX 12 / DXGI ───────────────────────────────────────
#include <d3d12.h>
#include <dxgi1_6.h>
#include <dxcapi.h>

// ─── WRL ─────────────────────────────────────────────────────
#include <wrl.h>

// ─── DirectInput ─────────────────────────────────────────────
#ifndef DIRECTINPUT_VERSION
#define DIRECTINPUT_VERSION 0x0800
#endif
#include <dinput.h>

// ─── C++ 標準ライブラリ（コンテナ） ──────────────────────────
#include <array>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

// ─── C++ 標準ライブラリ（ユーティリティ） ────────────────────
#include <cassert>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <numbers>
#include <typeindex>
