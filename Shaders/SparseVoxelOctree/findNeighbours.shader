
#version 430 core

layout(r32ui) uniform uimageBuffer nodePool_next;
//uniform usamplerBuffer levelAddressBuffer;
layout(r32ui) uniform uimageBuffer levelAddressBuffer;
//layout(r32ui) uniform readonly uimageBuffer voxelFragmentListPosition;

layout(r32ui) uniform uimageBuffer nodePool_X;
layout(r32ui) uniform uimageBuffer nodePool_Y;
layout(r32ui) uniform uimageBuffer nodePool_Z;
layout(r32ui) uniform uimageBuffer nodePool_X_neg;
layout(r32ui) uniform uimageBuffer nodePool_Y_neg;
layout(r32ui) uniform uimageBuffer nodePool_Z_neg;

uniform uint level;
uniform uint numLevels;
uniform uint voxelGridResolution;

#define THREAD_MODE 0

#include "SparseVoxelOctree/_utilityFunctions.shader"
//#include "SparseVoxelOctree/_threadNodeUtil.shader"
#include "SparseVoxelOctree/_traverseUtil.shader"
#include "SparseVoxelOctree/_octreeTraverse.shader"


bool isEmpty(int nodeAddress)
{
	uint nodeNext = imageLoad(nodePool_next, nodeAddress).x;
	return (nodeNext & NODE_MASK_BRICK) == 0;
}

void main() {
	int levelBeginAddress = int(imageLoad(levelAddressBuffer, int(level)).x);
	int levelEndAddress = int(imageLoad(levelAddressBuffer, int(level+1)).x);
	int nodeAddress = levelBeginAddress + gl_VertexID;
	if (nodeAddress >= levelEndAddress)
	{
		return;
	}

	uint nodeNextU = imageLoad(nodePool_next, nodeAddress).x;

	if ((nodeNextU & NODE_MASK_TAG) == 0U) {
		return;
	}

	//uint nodeAddressU = getThreadNode();
	//if (nodeAddressU == NODE_NOT_FOUND) {
	//	return;  // The requested threadID-node does not belong to the current level
	//}

	// load node center position from node_next
	//int nodeAddress = int(nodeNextU);
	//uint posTexI = (imageLoad(nodePool_next, nodeAddress).x & NODE_MASK_VALUE);
	//if ((posTexI & NODE_MASK_BRICK) == 0) {
	//	return;
	//}
	vec3 posTex = vec3(uintXYZ10ToVec3(nodeNextU & NODE_MASK_VALUE)) / float(voxelGridResolution);
	// then set node_next to 0
	imageStore(nodePool_next, nodeAddress, uvec4(NODE_MASK_BRICK));

	float stepTex = 1.0 / float(pow2[level]);
	//stepTex *= 0.99;

	uint nodeLevel = 0;
	int nodeAddress2 = traverseToLevel(posTex, nodeLevel, level+1);
	//if (nodeAddress2 != nodeAddress)
	//{
	//	return;
	//}

	int nX = 0;
	int nY = 0;
	int nZ = 0;
	int nX_neg = 0;
	int nY_neg = 0;
	int nZ_neg = 0;

	uint neighbourLevel = 0;

	if (posTex.x + stepTex < 1) {
		nX = traverseToLevel(posTex + vec3(stepTex, 0, 0), neighbourLevel, level+1);
		if (nodeLevel != neighbourLevel || isEmpty(nX)) {
			nX = 0; // invalidate neighbour-pointer if they are not on the same level
		}
	}

	if (posTex.y + stepTex < 1) {
		nY = traverseToLevel(posTex + vec3(0, stepTex, 0), neighbourLevel, level + 1);
		if (nodeLevel != neighbourLevel || isEmpty(nY)) {
			nY = 0; // invalidate neighbour-pointer if they are not on the same level
		}
	}

	if (posTex.z + stepTex < 1) {
		nZ = traverseToLevel(posTex + vec3(0, 0, stepTex), neighbourLevel, level + 1);
		if (nodeLevel != neighbourLevel || isEmpty(nZ)) {
			nZ = 0; // invalidate neighbour-pointer if they are not on the same level
		}
	}

	if (posTex.x - stepTex > 0) {
		nX_neg = traverseToLevel(posTex - vec3(stepTex, 0, 0), neighbourLevel, level + 1);
		if (nodeLevel != neighbourLevel || isEmpty(nX_neg)) {
			nX_neg = 0; // invalidate neighbour-pointer if they are not on the same level
		}
	}

	if (posTex.y - stepTex > 0) {
		nY_neg = traverseToLevel(posTex - vec3(0, stepTex, 0), neighbourLevel, level + 1);
		if (nodeLevel != neighbourLevel || isEmpty(nY_neg)) {
			nY_neg = 0; // invalidate neighbour-pointer if they are not on the same level
		}
	}

	if (posTex.z - stepTex > 0) {
		nZ_neg = traverseToLevel(posTex - vec3(0, 0, stepTex), neighbourLevel, level + 1);
		if (nodeLevel != neighbourLevel || isEmpty(nZ_neg)) {
			nZ_neg = 0; // invalidate neighbour-pointer if they are not on the same level
		}
	}

	imageStore(nodePool_X, nodeAddress, uvec4(nX));
	imageStore(nodePool_Y, nodeAddress, uvec4(nY));
	imageStore(nodePool_Z, nodeAddress, uvec4(nZ));
	imageStore(nodePool_X_neg, nodeAddress, uvec4(nX_neg));
	imageStore(nodePool_Y_neg, nodeAddress, uvec4(nY_neg));
	imageStore(nodePool_Z_neg, nodeAddress, uvec4(nZ_neg));
/*
	if (nY != 0)
	{
		imageStore(nodePool_Y_neg, nY, uvec4(nodeAddress));
	}
	if (nY_neg != 0)
	{
		imageStore(nodePool_Y, nY_neg, uvec4(nodeAddress));
	}
	if (nX != 0)
	{
		imageStore(nodePool_X_neg, nX, uvec4(nodeAddress));
	}
	if (nX_neg != 0)
	{
		imageStore(nodePool_X, nX_neg, uvec4(nodeAddress));
	}
	if (nZ != 0)
	{
		imageStore(nodePool_Z_neg, nZ, uvec4(nodeAddress));
	}
	if (nZ_neg != 0)
	{
		imageStore(nodePool_Z, nZ_neg, uvec4(nodeAddress));
	}*/

	/*
	// First: Assign the neighbour-pointers between the children
	imageStore(nodePool_X, int(childStartAddress + 0), uvec4(childStartAddress + 1));
	imageStore(nodePool_X, int(childStartAddress + 2), uvec4(childStartAddress + 3));
	imageStore(nodePool_X, int(childStartAddress + 4), uvec4(childStartAddress + 5));
	imageStore(nodePool_X, int(childStartAddress + 6), uvec4(childStartAddress + 7));

	imageStore(nodePool_X_neg, int(childStartAddress + 1), uvec4(childStartAddress + 0));
	imageStore(nodePool_X_neg, int(childStartAddress + 3), uvec4(childStartAddress + 2));
	imageStore(nodePool_X_neg, int(childStartAddress + 5), uvec4(childStartAddress + 4));
	imageStore(nodePool_X_neg, int(childStartAddress + 7), uvec4(childStartAddress + 6));

	imageStore(nodePool_Y, int(childStartAddress + 0), uvec4(childStartAddress + 2));
	imageStore(nodePool_Y, int(childStartAddress + 1), uvec4(childStartAddress + 3));
	imageStore(nodePool_Y, int(childStartAddress + 4), uvec4(childStartAddress + 6));
	imageStore(nodePool_Y, int(childStartAddress + 5), uvec4(childStartAddress + 7));

	imageStore(nodePool_Y_neg, int(childStartAddress + 2), uvec4(childStartAddress + 0));
	imageStore(nodePool_Y_neg, int(childStartAddress + 3), uvec4(childStartAddress + 1));
	imageStore(nodePool_Y_neg, int(childStartAddress + 6), uvec4(childStartAddress + 4));
	imageStore(nodePool_Y_neg, int(childStartAddress + 7), uvec4(childStartAddress + 5));

	imageStore(nodePool_Z, int(childStartAddress + 0), uvec4(childStartAddress + 4));
	imageStore(nodePool_Z, int(childStartAddress + 1), uvec4(childStartAddress + 5));
	imageStore(nodePool_Z, int(childStartAddress + 2), uvec4(childStartAddress + 6));
	imageStore(nodePool_Z, int(childStartAddress + 3), uvec4(childStartAddress + 7));

	imageStore(nodePool_Z_neg, int(childStartAddress + 4), uvec4(childStartAddress + 0));
	imageStore(nodePool_Z_neg, int(childStartAddress + 5), uvec4(childStartAddress + 1));
	imageStore(nodePool_Z_neg, int(childStartAddress + 6), uvec4(childStartAddress + 2));
	imageStore(nodePool_Z_neg, int(childStartAddress + 7), uvec4(childStartAddress + 3));
	///////////////////////////////////////////////////////////////////////////////// */
}