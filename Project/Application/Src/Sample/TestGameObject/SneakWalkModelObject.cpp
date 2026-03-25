#include "SneakWalkModelObject.h"
void SneakWalkModelObject::OnInitialize() {
    transform_.translate = { 0.0f, 0.0f, 0.0f };
    transform_.scale = { 1.0f, 1.0f, 1.0f };
    transform_.rotate = { 0.0f, 0.0f, 0.0f };
}

