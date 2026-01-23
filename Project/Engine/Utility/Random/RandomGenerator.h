#pragma once

#include <random>
#include <array>
#include <memory>

/// @brief エンジン内で統一された乱数生成クラス

namespace CoreEngine
{
class RandomGenerator {
public:
	/// @brief インスタンスを取得（シングルトンパターン）
	/// @return RandomGeneratorのインスタンス
	static RandomGenerator& GetInstance();

	/// @brief 乱数エンジンを初期化
	/// @param seed シード値（省略時は現在時刻を使用）
	void Initialize(uint32_t seed = 0);

	/// @brief 指定範囲の整数乱数を生成
	/// @param min 最小値
	/// @param max 最大値
	/// @return min以上max以下の整数
	int GetInt(int min, int max);

	/// @brief 指定範囲の浮動小数点乱数を生成
	/// @param min 最小値
	/// @param max 最大値
	/// @return min以上max以下の浮動小数点数
	float GetFloat(float min, float max);

	/// @brief 0.0f以上1.0f以下の浮動小数点乱数を生成
	/// @return 0.0f以上1.0f以下の浮動小数点数
	float GetFloat();

	/// @brief -1.0f以上1.0f以下の浮動小数点乱数を生成
	/// @return -1.0f以上1.0f以下の浮動小数点数
	float GetFloatSigned();

	/// @brief 指定確率でtrueを返す
	/// @param probability 確率（0.0f〜1.0f）
	/// @return 指定確率でtrue
	bool GetBool(float probability = 0.5f);

	/// @brief 乱数エンジンへの直接アクセス（既存システム互換用）
	/// @return 乱数エンジンへのポインタ
	std::mt19937* GetEngine() { return &engine_; }

private:
	RandomGenerator() = default;
	~RandomGenerator() = default;
	RandomGenerator(const RandomGenerator&) = delete;
	RandomGenerator& operator=(const RandomGenerator&) = delete;

	std::mt19937 engine_;                           // メルセンヌ・ツイスター乱数エンジン
	bool initialized_ = false;                      // 初期化フラグ
};
}
