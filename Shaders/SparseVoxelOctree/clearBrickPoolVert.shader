#version 430 core

#define CLEAR_ALL 0U
#define CLEAR_DYNAMIC 1U

layout(rgba8) uniform image3D brickPool_color;
layout(rgba8) uniform image3D brickPool_irradiance;
layout(rgba8) uniform image3D brickPool_normal;

uniform uint clearMode;

void main() {
  int size = imageSize(brickPool_color).x;
  ivec3 texCoord = ivec3(0);
  texCoord.x = gl_VertexID % size;
  texCoord.y = (gl_VertexID / size) % size;
  texCoord.z = gl_VertexID / (size * size);

  vec4 clearColor      = vec4(0.0, 0.0, 0.0, 0.0);
  vec4 clearIrradiance = vec4(0.0, 0.0, 0.0, 0.0);
  vec4 clearNormal     = vec4(0.0, 0.0, 0.0, 0.0);
  if (clearMode == CLEAR_ALL) {
    imageStore(brickPool_color, texCoord, clearColor);
    imageStore(brickPool_irradiance, texCoord, clearIrradiance);
    imageStore(brickPool_normal, texCoord, clearNormal);
  }

  else if (clearMode == CLEAR_DYNAMIC) {
    imageStore(brickPool_irradiance, texCoord, clearIrradiance);
  }
}
