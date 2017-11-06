/*
 Copyright (c) 2012 The VCT Project

  This file is part of VoxelConeTracing and is an implementation of
  "Interactive Indirect Illumination Using Voxel Cone Tracing" by Crassin et al

  VoxelConeTracing is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  VoxelConeTracing is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with VoxelConeTracing.  If not, see <http://www.gnu.org/licenses/>.
*/

/*!
* \author Dominik Lazarek (dominik.lazarek@gmail.com)
* \author Andreas Weinmann (andy.weinmann@gmail.com)
*/

/** 
This shader writes the voxel-colors from the voxel fragment list into the
leaf nodes of the octree. The shader is lauched with one thread per entry of
the voxel fragment list.
Every voxel is read from the voxel fragment list and its position is used
to traverse the octree and find the leaf-node.
*/
//#version 420 core
//#version 420 core
//#define THREAD_MODE 0

layout(r32ui) uniform readonly uimageBuffer nodePool_color;
layout(rgba8) uniform volatile image3D brickPool_value;

uniform usampler2D nodeMap;
uniform usamplerBuffer levelAddressBuffer;
uniform ivec2 nodeMapOffset[8];
uniform ivec2 nodeMapSize[8];

uniform uint numLevels;  // Number of levels in the octree

// "global variable" can be used by all functions
// every thread has a unique instance of such variable
vec4 voxelValues[8] = {
  vec4(0),
  vec4(0),
  vec4(0),
  vec4(0),
  vec4(0),
  vec4(0),
  vec4(0),
  vec4(0)
};

const uvec3 childOffsets[8] = {
  uvec3(0, 0, 0),
  uvec3(1, 0, 0),
  uvec3(0, 1, 0),
  uvec3(1, 1, 0),
  uvec3(0, 0, 1),
  uvec3(1, 0, 1),
  uvec3(0, 1, 1), 
  uvec3(1, 1, 1)};

const uint level = numLevels - 1;

#include "SparseVoxelOctree/_utilityFunctions.shader"
#include "SparseVoxelOctree/_threadNodeUtil.shader"

void loadVoxelValues(in ivec3 brickAddress){
  // Collect the original voxel colors (from voxelfragmentlist-voxels)
  // which were stored at the corners of the brick texture.
  for(int i = 0; i < 8; ++i) {
    voxelValues[i] = imageLoad(brickPool_value, 
                               brickAddress + 2 * ivec3(childOffsets[i]));
  }
}


void main() {
  uint nodeAddress = getThreadNode();
  if(nodeAddress == NODE_NOT_FOUND) {
    return;  // The requested threadID-node does not belong to the current level
  }

  ivec3 brickAddress = ivec3(uintXYZ10ToVec3(
                       imageLoad(nodePool_color, int(nodeAddress)).x));

  loadVoxelValues(brickAddress);

  
  vec4 col = vec4(0);
  
  // Center
  for (int i = 0; i < 8; ++i) {
    col += 0.125 * voxelValues[i];
  }

  imageStore(brickPool_value, brickAddress + ivec3(1,1,1), col);


  // Face X
  col = vec4(0);
  col += 0.25 * voxelValues[1];
  col += 0.25 * voxelValues[3];
  col += 0.25 * voxelValues[5];
  col += 0.25 * voxelValues[7];
  imageStore(brickPool_value, brickAddress + ivec3(2,1,1), col);

  // Face X Neg
  col = vec4(0);
  col += 0.25 * voxelValues[0];
  col += 0.25 * voxelValues[2];
  col += 0.25 * voxelValues[4];
  col += 0.25 * voxelValues[6];
  imageStore(brickPool_value, brickAddress + ivec3(0,1,1), col);


  // Face Y
  col = vec4(0);
  col += 0.25 * voxelValues[2];
  col += 0.25 * voxelValues[3];
  col += 0.25 * voxelValues[6];
  col += 0.25 * voxelValues[7];
  imageStore(brickPool_value, brickAddress + ivec3(1,2,1), col);

  // Face Y Neg
  col = vec4(0);
  col += 0.25 * voxelValues[0];
  col += 0.25 * voxelValues[1];
  col += 0.25 * voxelValues[4];
  col += 0.25 * voxelValues[5];
  imageStore(brickPool_value, brickAddress + ivec3(1,0,1), col);

  
  // Face Z
  col = vec4(0);
  col += 0.25 * voxelValues[4];
  col += 0.25 * voxelValues[5];
  col += 0.25 * voxelValues[6];
  col += 0.25 * voxelValues[7];
  imageStore(brickPool_value, brickAddress + ivec3(1,1,2), col);

  // Face Z Neg
  col = vec4(0);
  col += 0.25 * voxelValues[0];
  col += 0.25 * voxelValues[1];
  col += 0.25 * voxelValues[2];
  col += 0.25 * voxelValues[3];
  imageStore(brickPool_value, brickAddress + ivec3(1,1,0), col);


  // Edges
  col = vec4(0);
  col += 0.5 * voxelValues[0];
  col += 0.5 * voxelValues[1];
  imageStore(brickPool_value, brickAddress + ivec3(1,0,0), col);

  col = vec4(0);
  col += 0.5 * voxelValues[0];
  col += 0.5 * voxelValues[2];
  imageStore(brickPool_value, brickAddress + ivec3(0,1,0), col);

  col = vec4(0);
  col += 0.5 * voxelValues[2];
  col += 0.5 * voxelValues[3];
  imageStore(brickPool_value, brickAddress + ivec3(1,2,0), col);

  col = vec4(0);
  col += 0.5 * voxelValues[3];
  col += 0.5 * voxelValues[1];
  imageStore(brickPool_value, brickAddress + ivec3(2,1,0), col);

  col = vec4(0);
  col += 0.5 * voxelValues[0];
  col += 0.5 * voxelValues[4];
  imageStore(brickPool_value, brickAddress + ivec3(0,0,1), col);

  col = vec4(0);
  col += 0.5 * voxelValues[2];
  col += 0.5 * voxelValues[6];
  imageStore(brickPool_value, brickAddress + ivec3(0,2,1), col);

  col = vec4(0);
  col += 0.5 * voxelValues[3];
  col += 0.5 * voxelValues[7];
  imageStore(brickPool_value, brickAddress + ivec3(2,2,1), col);

  col = vec4(0);
  col += 0.5 * voxelValues[1];
  col += 0.5 * voxelValues[5];
  imageStore(brickPool_value, brickAddress + ivec3(2,0,1), col);

  col = vec4(0);
  col += 0.5 * voxelValues[4];
  col += 0.5 * voxelValues[6];
  imageStore(brickPool_value, brickAddress + ivec3(0,1,2), col);

  col = vec4(0);
  col += 0.5 * voxelValues[6];
  col += 0.5 * voxelValues[7];
  imageStore(brickPool_value, brickAddress + ivec3(1,2,2), col);

  col = vec4(0);
  col += 0.5 * voxelValues[5];
  col += 0.5 * voxelValues[7];
  imageStore(brickPool_value, brickAddress + ivec3(2,1,2), col);

  col = vec4(0);
  col += 0.5 * voxelValues[4];
  col += 0.5 * voxelValues[5];
  imageStore(brickPool_value, brickAddress + ivec3(1,0,2), col);
}