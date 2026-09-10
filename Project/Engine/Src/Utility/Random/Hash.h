#pragma once

#include <cstddef>
#include <cstdint>

/// @file Hash.h
/// @brief 決定的なハッシュ（ビット撹拌）とその実数化。
/// @details RandomGenerator との使い分け:
///          - RandomGenerator … 共有ストリームから「次の値」を1つ取る。呼ぶたびに違う値。
///          - こちら … 同じ入力なら常に同じ値を返す純粋関数。呼ぶ順・呼ぶ回数に依存しない。
///          値ノイズ（座標や添字から毎フレーム同じ揺らぎを作る）と、unordered_map の
///          キー撹拌は後者でしか書けない。ストリームで書くと、描画範囲がずれた・
///          別システムが先に乱数を消費した、というだけで値が変わってしまう。
namespace CoreEngine::Hash
{
    /// @brief 32bit のビット撹拌（lowbias32 の finalizer）
    /// @details 乗算と右シフトだけで入力の全ビットを出力全体へ伝える。
    ///          連番や格子座標のような近い入力でも、出力は散らばる。
    constexpr std::uint32_t Mix32(std::uint32_t value) noexcept
    {
        value ^= value >> 16;
        value *= 0x7FEB352Du;
        value ^= value >> 15;
        value *= 0x846CA68Bu;
        value ^= value >> 16;
        return value;
    }

    /// @brief 64bit のビット撹拌（splitmix64 の finalizer）
    constexpr std::uint64_t Mix64(std::uint64_t value) noexcept
    {
        value += 0x9E3779B97F4A7C15ull;
        value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ull;
        value = (value ^ (value >> 27)) * 0x94D049BB133111EBull;
        return value ^ (value >> 31);
    }

    /// @brief 複数のハッシュ値を1つへ畳み込む（boost::hash_combine と同じ形）
    /// @note 順序に意味がある。同じ値の集合でも順番が違えば別のハッシュになる。
    constexpr std::size_t Combine(std::size_t seed, std::size_t value) noexcept
    {
        return seed ^ (value + 0x9E3779B97F4A7C15ull + (seed << 6) + (seed >> 2));
    }

    /// @brief 撹拌済みハッシュを 0..1 へ写す
    /// @note 上位 24bit だけを使う。float の仮数が 24bit なので、それ以上は入れても落ちる。
    constexpr float ToUnitFloat(std::uint32_t hash) noexcept
    {
        return static_cast<float>(hash >> 8) * (1.0f / 16777216.0f);
    }

    /// @brief 撹拌済みハッシュを -1..1 へ写す
    constexpr float ToSignedFloat(std::uint32_t hash) noexcept
    {
        return static_cast<float>(hash >> 8) * (1.0f / 8388608.0f) - 1.0f;
    }

    /// @brief 2次元の格子座標から 0..1 の決定的な値を作る（値ノイズ用）
    /// @details 同じマスは何フレーム後でも同じ値を返す。マップのマスごとの色ムラなど、
    ///          「毎フレーム引き直しても絵が変わらない乱数」が要る場所で使う。
    constexpr float Cell01(std::int32_t x, std::int32_t y) noexcept
    {
        // 空間ハッシュで広く使われる素数。軸ごとに別の素数を掛けてから XOR することで、
        // (x, y) と (y, x) が同じ値になるのを避ける。
        return ToUnitFloat(Mix32(
            static_cast<std::uint32_t>(x) * 73856093u ^
            static_cast<std::uint32_t>(y) * 19349663u));
    }
}
