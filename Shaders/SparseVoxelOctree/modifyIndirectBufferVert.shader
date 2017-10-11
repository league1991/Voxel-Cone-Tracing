#version 420 core

layout(r32ui) uniform uimageBuffer indirectCommandBuf;
layout(binding = 0) uniform atomic_uint numThreads;

void main() {
  uint num = atomicCounter(numThreads);
  
  // Write atomic variable's value to draw command buffer
  imageStore(indirectCommandBuf, 0, uvec4(num));  // Vertex-Count
  imageStore(indirectCommandBuf, 1, uvec4(1));  // Primitive Count
}
