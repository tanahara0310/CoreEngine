#include "WalkModelObject.h"

void WalkModelObject::OnInitialize() {
    transform_.translate = { 3.0f, 0.0f, 0.0f };  // 右側に配置
    transform_.scale = { 1.0f, 1.0f, 1.0f };
    transform_.rotate = { 0.0f, 0.0f, 0.0f };
}

