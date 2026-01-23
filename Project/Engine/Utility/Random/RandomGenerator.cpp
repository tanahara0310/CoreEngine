#include "RandomGenerator.h"
#include <chrono>


namespace CoreEngine
{
RandomGenerator& RandomGenerator::GetInstance()
{
	static RandomGenerator instance;
	return instance;
}

void RandomGenerator::Initialize(uint32_t seed)
{
	if (seed == 0) {
		// 現在時刻をシードとして使用
		auto now = std::chrono::high_resolution_clock::now();
		seed = static_cast<uint32_t>(now.time_since_epoch().count());
	}
	
	engine_.seed(seed);
	initialized_ = true;
}

int RandomGenerator::GetInt(int min, int max)
{
	if (!initialized_) {
		Initialize();
	}
	
	std::uniform_int_distribution<int> dist(min, max);
	return dist(engine_);
}

float RandomGenerator::GetFloat(float min, float max)
{
	if (!initialized_) {
		Initialize();
	}
	
	std::uniform_real_distribution<float> dist(min, max);
	return dist(engine_);
}

float RandomGenerator::GetFloat()
{
	return GetFloat(0.0f, 1.0f);
}

float RandomGenerator::GetFloatSigned()
{
	return GetFloat(-1.0f, 1.0f);
}

bool RandomGenerator::GetBool(float probability)
{
	if (!initialized_) {
		Initialize();
	}
	
	std::bernoulli_distribution dist(probability);
	return dist(engine_);
}
}
