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

#version 420 core

//layout (location = 0) in vec3 v_position;
//out vec4 posWS;
// 
//uniform mat4 viewProjMat;
//uniform mat4 modelMat;
// 
//void main(){
//  posWS = modelMat* vec4(v_position,1);
//  gl_Position = viewProjMat * posWS;
//}
///////////////
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;

uniform mat4 M;
uniform mat4 V;
uniform mat4 P;

out vec4 posWS;
out vec3 normalGeom;

void main() {
	posWS = M * vec4(position, 1);
	normalGeom = normalize(mat3(transpose(inverse(M))) * normal);
	gl_Position = P * V * vec4(posWS.xyz, 1);
}