
#version 420 core

layout(r32ui) uniform volatile uimageBuffer nodePool_next;
layout(r32ui) uniform volatile uimageBuffer levelAddressBuffer;
layout(binding = 0) uniform atomic_uint nextFreeNode;

uniform uint level;

#define NODE_MASK_VALUE 0x3FFFFFFF
#define NODE_MASK_TAG (0x00000001 << 31)

bool isFlagged(in uint nodeNext) {
	return (nodeNext & NODE_MASK_TAG) != 0U;
}

uint allocChildTile(in int nodeAddress) {
	uint nextFreeTile = atomicCounterIncrement(nextFreeNode);
	// root node is not in a 2x2x2 tile
	uint nextFreeAddress = (1U + 8U * nextFreeTile);

	// Create levelAddress indirection buffer by storing the start-addresses on each level
	imageAtomicMin(levelAddressBuffer, int(level + 1), int(nextFreeAddress));

	return nextFreeAddress;
}

void main() {
	uint nodeNextU = imageLoad(nodePool_next, gl_VertexID).x;

	if (isFlagged(nodeNextU)) {
		//alloc child and unflag
		nodeNextU = NODE_MASK_VALUE & allocChildTile(gl_VertexID);

		// Store the unflagged nodeNextU
		imageStore(nodePool_next, gl_VertexID, uvec4(nodeNextU, 0, 0, 0));
	}
}