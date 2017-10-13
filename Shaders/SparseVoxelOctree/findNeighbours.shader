
#version 430 core

layout(r32ui) uniform readonly uimageBuffer nodePool_next;
layout(r32ui) uniform readonly uimageBuffer voxelFragmentListPosition;

layout(r32ui) uniform uimageBuffer nodePool_X;
layout(r32ui) uniform uimageBuffer nodePool_Y;
layout(r32ui) uniform uimageBuffer nodePool_Z;
layout(r32ui) uniform uimageBuffer nodePool_X_neg;
layout(r32ui) uniform uimageBuffer nodePool_Y_neg;
layout(r32ui) uniform uimageBuffer nodePool_Z_neg;

uniform uint level;
uniform uint numLevels;
uniform uint voxelGridResolution;

#include "SparseVoxelOctree/_utilityFunctions.shader"
#include "SparseVoxelOctree/_traverseUtil.shader"
#include "SparseVoxelOctree/_octreeTraverse.shader"

void main() {
	// Find the node for this position
	uint voxelPosU = imageLoad(voxelFragmentListPosition, gl_VertexID).x;
	uvec3 voxelPos = uintXYZ10ToVec3(voxelPosU);
	vec3 posTex = vec3(voxelPos) / vec3(voxelGridResolution);
	float stepTex = 1.0 / float(pow2[level]);
	//stepTex *= 0.99;

	uint nodeLevel = 0;
	int nodeAddress = traverseOctree_simple(posTex, nodeLevel);

	int nX = 0;
	int nY = 0;
	int nZ = 0;
	int nX_neg = 0;
	int nY_neg = 0;
	int nZ_neg = 0;

	uint neighbourLevel = 0;

	if (posTex.x + stepTex < 1) {
		nX = traverseOctree_simple(posTex + vec3(stepTex, 0, 0), neighbourLevel);
		if (nodeLevel != neighbourLevel) {
			nX = 0; // invalidate neighbour-pointer if they are not on the same level
		}
	}

	if (posTex.y + stepTex < 1) {
		nY = traverseOctree_simple(posTex + vec3(0, stepTex, 0), neighbourLevel);
		if (nodeLevel != neighbourLevel) {
			nY = 0; // invalidate neighbour-pointer if they are not on the same level
		}
	}

	if (posTex.z + stepTex < 1) {
		nZ = traverseOctree_simple(posTex + vec3(0, 0, stepTex), neighbourLevel);
		if (nodeLevel != neighbourLevel) {
			nZ = 0; // invalidate neighbour-pointer if they are not on the same level
		}
	}

	if (posTex.x - stepTex > 0) {
		nX_neg = traverseOctree_simple(posTex - vec3(stepTex, 0, 0), neighbourLevel);
		if (nodeLevel != neighbourLevel) {
			nX_neg = 0; // invalidate neighbour-pointer if they are not on the same level
		}
	}

	if (posTex.y - stepTex > 0) {
		nY_neg = traverseOctree_simple(posTex - vec3(0, stepTex, 0), neighbourLevel);
		if (nodeLevel != neighbourLevel) {
			nY_neg = 0; // invalidate neighbour-pointer if they are not on the same level
		}
	}

	if (posTex.z - stepTex > 0) {
		nZ_neg = traverseOctree_simple(posTex - vec3(0, 0, stepTex), neighbourLevel);
		if (nodeLevel != neighbourLevel) {
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