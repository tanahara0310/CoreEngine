#pragma once

#include "Matrix/Matrix4x4.h"


namespace CoreEngine
{
struct TransformationMatrix {
	Matrix4x4 WVP;
	Matrix4x4 world;
	Matrix4x4 worldInverseTranspose;
};
}
