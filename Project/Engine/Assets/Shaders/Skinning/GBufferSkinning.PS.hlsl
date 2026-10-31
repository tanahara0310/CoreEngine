// スキンメッシュの GBuffer 書き込み。GBuffer.PS.hlsl と同じで、モーションベクターの a に動く物の印を立てる

#define GBUFFER_MOVING_OBJECT 1
#include "../Object/GBuffer.PS.hlsl"
