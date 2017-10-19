#version 420 core

layout(r32ui) uniform uimageBuffer nodePool_next;
layout(r32ui) uniform uimageBuffer nodePool_color;
layout(binding = 0) uniform atomic_uint nextFreeBrick;

uniform uint brickPoolResolution;

#include "SparseVoxelOctree/_utilityFunctions.shader"


// Allocate brick-texture, store pointer in color
void alloc3x3x3TextureBrick(in int nodeAddress) {
  uint nextFreeTexBrick = atomicCounterIncrement(nextFreeBrick);
  memoryBarrier();
  uvec3 texAddress = uvec3(0);
  uint brickPoolResBricks = brickPoolResolution / 3;
  texAddress.x = nextFreeTexBrick % brickPoolResBricks;
  texAddress.y = (nextFreeTexBrick / brickPoolResBricks) % brickPoolResBricks;
  texAddress.z = nextFreeTexBrick / (brickPoolResBricks * brickPoolResBricks);
  texAddress *= 3;

  // Store brick-pointer
  imageStore(nodePool_color, nodeAddress,
      uvec4(vec3ToUintXYZ10(texAddress), 0, 0, 0));
}

void main() {
  uint tileAddress = (8U * gl_VertexID)+1;

  for (uint i = 0; i < 8; ++i) {
    int address = int(tileAddress + i);
	// allocate new brick
    alloc3x3x3TextureBrick(address);

    //set Brick flag
    uint nodeNextU = imageLoad(nodePool_next, address).x;
    imageStore(nodePool_next, address,
               uvec4(NODE_MASK_BRICK | nodeNextU, 0, 0, 0));
  }
}
