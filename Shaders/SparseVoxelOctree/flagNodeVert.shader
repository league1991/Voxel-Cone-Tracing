
#version 420 core

layout(r32ui) uniform volatile uimageBuffer voxelFragmentListPosition;
layout(r32ui) uniform volatile uimageBuffer nodePool_next;
uniform uint voxelGridResolution;
uniform uint numLevels;

const uint NODE_MASK_VALUE = 0x3FFFFFFF;
const uint NODE_MASK_TAG = (0x00000001 << 31);
const uint NODE_MASK_TAG_STATIC = (0x00000003 << 30);
const uint NODE_NOT_FOUND = 0xFFFFFFFF;

const uvec3 childOffsets[8] = {
	uvec3(0, 0, 0),
	uvec3(1, 0, 0),
	uvec3(0, 1, 0),
	uvec3(1, 1, 0),
	uvec3(0, 0, 1),
	uvec3(1, 0, 1),
	uvec3(0, 1, 1),
	uvec3(1, 1, 1) };

const uint pow2[] = { 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024 };

uvec3 uintXYZ10ToVec3(uint v)
{
	uint z = (v >> 20U) & 0x000003FF;
	uint y = (v >> 10U) & 0x000003FF;
	uint x = (v >> 0U) & 0x000003FF;
	return uvec3(x, y, z);
}

int traverseOctree_simple(in vec3 posTex, out uint foundOnLevel) {
	vec3 nodePosTex = vec3(0.0);
	vec3 nodePosMaxTex = vec3(1.0);
	int nodeAddress = 0;
	foundOnLevel = 0;
	float sideLength = 1.0;

	for (uint iLevel = 0; iLevel < numLevels; ++iLevel) {
		uint nodeNext = imageLoad(nodePool_next, nodeAddress).x;

		uint childStartAddress = nodeNext & NODE_MASK_VALUE;
		if (childStartAddress == 0U) {
			foundOnLevel = iLevel;
			break;
		}

		uvec3 offVec = uvec3(2.0 * posTex);
		uint off = offVec.x + 2U * offVec.y + 4U * offVec.z;

		// Restart while-loop with the child node (aka recursion)
		nodeAddress = int(childStartAddress + off);
		nodePosTex += vec3(childOffsets[off]) * vec3(sideLength);
		nodePosMaxTex = nodePosTex + vec3(sideLength);

		sideLength = sideLength / 2.0;
		posTex = 2.0 * posTex - vec3(offVec);
	} // level-for

	return nodeAddress;
}

void flagNode(in uint nodeNext, in int address) {
	// nodeNext is original node data
	nodeNext = NODE_MASK_TAG | nodeNext;
	imageStore(nodePool_next, address, uvec4(nodeNext));
	memoryBarrier();
}

void main() {
	uint voxelPosU = imageLoad(voxelFragmentListPosition, gl_VertexID).x;
	uvec3 voxelPos = uintXYZ10ToVec3(voxelPosU);
	vec3 posTex = vec3(voxelPos) / vec3(voxelGridResolution);

	uint onLevel = 0;
	// find node without child and return its address
	int nodeAddress = traverseOctree_simple(posTex, onLevel);

	uint nodeNext = imageLoad(nodePool_next, nodeAddress).x;
	flagNode(nodeNext, nodeAddress);
	//if (onLevel < numLevels - 1) {
	//	uint nodeNext = imageLoad(nodePool_next, nodeAddress).x;
	//	flagNode(nodeNext, nodeAddress);
	//}
}
