#include "pch.h"
#include "RayTracingDispatchInfo.h"

namespace CoreEngine
{
    const char* ToString(RayTracingDispatchStatus status)
    {
        switch (status) {
        case RayTracingDispatchStatus::None:                    return "None";
        case RayTracingDispatchStatus::NotInitialized:          return "NotInitialized";
        case RayTracingDispatchStatus::RayTracingUnsupported:   return "RayTracingUnsupported";
        case RayTracingDispatchStatus::NoBLAS:                  return "NoBLAS";
        case RayTracingDispatchStatus::InvalidCommandList:      return "InvalidCommandList";
        case RayTracingDispatchStatus::CommandList4Unavailable: return "CommandList4Unavailable";
        case RayTracingDispatchStatus::OutputAllocationFailed:  return "OutputAllocationFailed";
        case RayTracingDispatchStatus::Skipped:                 return "Skipped";
        case RayTracingDispatchStatus::Dispatched:              return "Dispatched";
        default:                                                return "Unknown";
        }
    }
}
