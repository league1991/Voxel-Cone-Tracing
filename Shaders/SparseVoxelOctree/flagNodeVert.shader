
#version 420 core

layout(r32ui) uniform volatile uimageBuffer voxelFragmentListPosition;
layout(r32ui) uniform volatile uimageBuffer nodePool_next;
//layout(r32ui) uniform volatile uimageBuffer nodePool_color;
uniform uint voxelGridResolution;
uniform uint numLevels;
uniform uint level;

#include "SparseVoxelOctree/_utilityFunctions.shader"
//const uint NODE_MASK_VALUE = 0x3FFFFFFF;
//const uint NODE_MASK_TAG = (0x00000001 << 31);
//const uint NODE_MASK_TAG_STATIC = (0x00000003 << 30);
//const uint NODE_NOT_FOUND = 0xFFFFFFFF;

const uvec3 childOffsets[8] = {
	uvec3(0, 0, 0),
	uvec3(1, 0, 0),
	uvec3(0, 1, 0),
	uvec3(1, 1, 0),
	uvec3(0, 0, 1),
	uvec3(1, 0, 1),
	uvec3(0, 1, 1),
	uvec3(1, 1, 1) };

const vec3 neighOffset[6] = {
	vec3(1, 0, 0),
	vec3(0, 1, 0),
	vec3(0, 0, 1),
	vec3(-1, 0, 0),
	vec3(0, -1, 0),
	vec3(0, 0, -1),
};

int traverseOctree_simple(in vec3 posTex, out uint foundOnLevel, out vec3 nodeCenterPos) {
	vec3 nodePosTex = vec3(0.0);
	vec3 nodePosMaxTex = vec3(1.0);
	int nodeAddress = 0;
	foundOnLevel = 0;
	float sideLength = 0.5;

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

	nodeCenterPos = 0.5 * (nodePosTex + nodePosMaxTex);
	return nodeAddress;
}

void flagNode(in int address, in vec3 nodeCenterPos) {
	uvec3 nodeCenterPosI = uvec3(nodeCenterPos * voxelGridResolution);
	// uint nodeNext = imageLoad(nodePool_next, address).x;
	// nodeNext = NODE_MASK_BRICK | nodeNext;
	uint nodeNext = NODE_MASK_BRICK | vec3ToUintXYZ10(nodeCenterPosI);
	imageStore(nodePool_next, address, uvec4(nodeNext));
	//imageStore(nodePool_color, address, uvec4(vec3ToUintXYZ10(nodeCenterPosI)));
	memoryBarrier();
}

void main() {
	uint voxelPosU = imageLoad(voxelFragmentListPosition, gl_VertexID).x;
	uvec3 voxelPos = uintXYZ10ToVec3(voxelPosU);
	vec3 posTex = vec3(voxelPos) / vec3(voxelGridResolution);

	float nodeOffset = nodeSizes[level];
	uint onLevel = 0;
	vec3 nodeCenterPos;
	// find node without child and return its address
	int nodeAddress = traverseOctree_simple(posTex, onLevel, nodeCenterPos);
	flagNode(nodeAddress, nodeCenterPos);

	if (level < numLevels-2)
	{
		for (int i = 0; i < 6; i++)
		{
			nodeAddress = traverseOctree_simple(
				posTex + neighOffset[i] * nodeOffset,
				onLevel, nodeCenterPos);

			flagNode(nodeAddress, nodeCenterPos);
		}
	}

	//if (onLevel < numLevels - 1) {
	//	uint nodeNext = imageLoad(nodePool_next, nodeAddress).x;
	//	flagNode(nodeNext, nodeAddress);
	//}
}
