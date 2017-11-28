
#version 430
#define MAX_NUM_AVG_ITERATIONS 100

layout(r32ui) uniform coherent uimageBuffer voxelFragList_position;
layout(r32ui) uniform volatile uimage3D voxelFragTex_color;
layout(r32ui) uniform volatile uimage3D voxelFragTex_normal;

layout(binding = 0) uniform atomic_uint voxel_index;

uniform sampler2D diffuseTex;
uniform uint voxelTexSize;

struct Material {
	vec3 diffuseColor;
	vec3 specularColor;
	float diffuseReflectivity;
	float specularReflectivity;
	float emissivity;
	float transparency;
};
uniform Material material;

in VoxelData{
	vec3 posTexSpace;
	vec3 normal;
	vec2 uv;
} In;


out vec4 color;

vec4 convRGBA8ToVec4(uint val) {
	return vec4(float((val & 0x000000FF)),
		float((val & 0x0000FF00) >> 8U),
		float((val & 0x00FF0000) >> 16U),
		float((val & 0xFF000000) >> 24U));
}

uint convVec4ToRGBA8(vec4 val) {
	return (uint(val.w) & 0x000000FF) << 24U
		| (uint(val.z) & 0x000000FF) << 16U
		| (uint(val.y) & 0x000000FF) << 8U
		| (uint(val.x) & 0x000000FF);
}

uint vec3ToUintXYZ10(uvec3 val) {
	return (uint(val.z) & 0x000003FF) << 20U
		| (uint(val.y) & 0x000003FF) << 10U
		| (uint(val.x) & 0x000003FF);
}


uint imageAtomicRGBA8Avg(layout(r32ui) volatile uimage3D img,
	ivec3 coords,
	vec4 newVal) {
	newVal.xyz *= 255.0; // Optimise following calculations
	uint newValU = convVec4ToRGBA8(newVal);
	uint lastValU = 0;
	uint currValU;
	vec4 currVal;
	uint numIterations = 0;
	// Loop as long as destination value gets changed by other threads
	while ((currValU = imageAtomicCompSwap(img, coords, lastValU, newValU)) != lastValU
		&& numIterations < MAX_NUM_AVG_ITERATIONS) {
		lastValU = currValU;

		// Compute average value newValU
		currVal = convRGBA8ToVec4(currValU);
		currVal.xyz *= currVal.a; // Denormalize
		currVal += newVal; // Add new value
		currVal.xyz /= currVal.a; // Renormalize
		newValU = convVec4ToRGBA8(currVal);

		++numIterations;
	}

	// currVal now contains the calculated color: now convert it to a proper alpha-premultiplied version
	//newVal = convRGBA8ToVec4(newValU);
	//newVal.a = 255.0;
	//newValU = convVec4ToRGBA8(newVal);
	//imageStore(img, coords, uvec4(newValU));

	return newValU;
}

void main() {
	//uvec3 baseVoxel = uvec3(floor(In.posTexSpace * (voxelTexSize)));
	uvec3 baseVoxel = uvec3(floor(In.posTexSpace * (voxelTexSize)));

	vec4 diffColor = texture(diffuseTex, vec2(In.uv.x, 1.0 - In.uv.y));
	// Pre-multiply alpha:
	diffColor = vec4(material.diffuseColor,1);

	vec4 normal = vec4(normalize(In.normal) * 0.5 + 0.5, 1.0);
	normal.xyz *= diffColor.a;
	normal.a = diffColor.a;


	uint voxelIndex = atomicCounterIncrement(voxel_index);
	memoryBarrier();

	//Store voxel position in FragmentList
	uint diffColorU = convVec4ToRGBA8(diffColor * 255.0);
	uint normalU = convVec4ToRGBA8(normal * 255.0);
	imageStore(voxelFragList_position, int(voxelIndex), uvec4(vec3ToUintXYZ10(baseVoxel)));
	//imageStore(voxelFragTex_color, ivec3(baseVoxel), uvec4(diffColorU));
	//imageStore(voxelFragTex_normal, ivec3(baseVoxel), uvec4(normalU));

	//Avg voxel attributes and store in FragmentTexXXX
	imageAtomicRGBA8Avg(voxelFragTex_color, ivec3(baseVoxel), diffColor);
	imageAtomicRGBA8Avg(voxelFragTex_normal, ivec3(baseVoxel), normal);
}