
#version 420 core

layout(r32ui) uniform volatile uimageBuffer nodePool_next;
layout(r32ui) uniform volatile uimageBuffer levelAddressBuffer;
layout(binding = 0) uniform atomic_uint nextFreeNode;

uniform uint level; // current working level

#include "SparseVoxelOctree/_utilityFunctions.shader"

bool isMarked(in uint nodeNext) {
	return (nodeNext & NODE_MASK_BRICK) != 0U;
}

uint allocChildTile(in int nodeAddress) {
	uint nextFreeTile = atomicCounterIncrement(nextFreeNode);
	// root node is not in a 2x2x2 tile
	uint nextFreeAddress = (1U + 8U * nextFreeTile);

	// Create levelAddress indirection buffer by storing the start-addresses on each level
	// The beginning address of next layer
	imageAtomicMax(levelAddressBuffer, int(level + 2), nextFreeAddress+8);
	return nextFreeAddress;
}

void main() {
	int levelBeginAddress = int(imageLoad(levelAddressBuffer, int(level)).x);
	int curNodeAddress = levelBeginAddress + gl_VertexID;

	uint nodeNextU = imageLoad(nodePool_next, curNodeAddress).x;

	if (isMarked(nodeNextU)) {
		//alloc child and unflag
		nodeNextU = NODE_MASK_VALUE & allocChildTile(curNodeAddress);

		// Store the unflagged nodeNextU
		imageStore(nodePool_next, curNodeAddress, uvec4(nodeNextU | NODE_MASK_BRICK, 0, 0, 0));
	}
}