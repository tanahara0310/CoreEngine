#include "TerrainObject.h"
#include <numbers>

using namespace CoreEngine;

void TerrainObject::OnInitialize() {
    // テレインの初期回転
    transform_.rotate = { 0.0f, std::numbers::pi_v<float> * 0.5f, 0.0f };
}

