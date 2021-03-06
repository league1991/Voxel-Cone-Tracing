//----------------------------------------------------------------------------------------------//
// A voxel cone tracing implementation for real-time global illumination,                       //
// refraction, specular, glossy and diffuse reflections, and soft shadows.                      //
// The implementation traces cones through a 3D texture which contains a                        //
// direct lit voxelized scene.                                                                  //
//                                                                                              //
// Inspired by "Interactive Indirect Illumination Using Voxel Cone Tracing" by Crassin et al.   //  
// (Cyril Crassin, Fabrice Neyret, Miguel Saintz, Simon Green and Elmar Eisemann)               //
// https://research.nvidia.com/sites/default/files/publications/GIVoxels-pg2011-authors.pdf     //
//                                                                                              //  
// Author:  Fredrik Präntare <prantare@gmail.com>                                               //
// Date:    11/26/2016                                                                          //
//                                                                                              // 
// Course project in TSBK03 (Techniques for Advanced Game Programming) at Linköping University. //
// ---------------------------------------------------------------------------------------------//
#version 450 core
#define TSQRT2 2.828427
#define SQRT2 1.414213
#define ISQRT2 0.707106
// --------------------------------------
// Light (voxel) cone tracing settings.
// --------------------------------------
#define MIPMAP_HARDCAP 5.4f /* Too high mipmap levels => glitchiness, too low mipmap levels => sharpness. */
#define VOXEL_SIZE (1/128.0) /* Size of a voxel. 128x128x128 => 1/128 = 0.0078125. */
#define SHADOWS 1 /* Shadow cone tracing. */
//#define DIFFUSE_INDIRECT_FACTOR 0.52f /* Just changes intensity of diffuse indirect lighting. */
// --------------------------------------
// Other lighting settings.
// --------------------------------------
#define SPECULAR_MODE 1 /* 0 == Blinn-Phong (halfway vector), 1 == reflection model. */
#define SPECULAR_FACTOR 4.0f /* Specular intensity tweaking factor. */
#define SPECULAR_POWER 65.0f /* Specular power in Blinn-Phong. */
//#define DIRECT_LIGHT_INTENSITY 0.96f /* (direct) point light intensity factor. */
#define MAX_LIGHTS 1 /* Maximum number of lights supported. */

// Lighting attenuation factors. See the function "attenuate" (below) for more information.
#define DIST_FACTOR 1.1f /* Distance is multiplied by this when calculating attenuation. */
#define CONSTANT 1
#define LINEAR 0 /* Looks meh when using gamma correction. */
#define QUADRATIC 1

// Other settings.
#define GAMMA_CORRECTION 1 /* Whether to use gamma correction or not. */

// Here are my uniform variables
layout(r32ui) uniform readonly uimageBuffer nodePool_next;
layout(r32ui) uniform readonly uimageBuffer nodePool_color;

//layout(rgba8) uniform image3D brickPool_color;
//layout(rgba8) uniform image3D brickPool_irradiance;
//layout(rgba8) uniform image3D brickPool_normal;
uniform sampler3D brickPool_color;
uniform sampler3D brickPool_irradiance;
uniform sampler3D brickPool_normal;

uniform sampler2D smPosition;

uniform mat4 voxelGridTransformI;
uniform vec3 voxelSize;
uniform uint numLevels;

#include "SparseVoxelOctree/_utilityFunctions.shader"
#include "SparseVoxelOctree/_traverseUtil.shader"
#include "SparseVoxelOctree/_octreeTraverse.shader"


// Basic point light.
struct PointLight {
	vec3 position;
	vec3 color;
};
struct DirectionalLight {
	vec3 position;
	vec3 direction;
	vec3 up;
	vec2 size;
	vec3 color;
};

// Basic material.
struct Material {
	vec3 diffuseColor;
	float diffuseReflectivity;
	vec3 specularColor;
	float specularDiffusion; // "Reflective and refractive" specular diffusion. 
	float specularReflectivity;
	float emissivity; // Emissive materials uses diffuse color as emissive color.
	float refractiveIndex;
	float transparency;
};

struct Settings {
	bool indirectSpecularLight; // Whether indirect specular light should be rendered or not.
	bool indirectDiffuseLight; // Whether indirect diffuse light should be rendered or not.
	bool directLight; // Whether direct light should be rendered or not.
	bool shadows; // Whether shadows should be rendered or not.
};

uniform float directLightMultiplier;
uniform float indirectLightMultiplier;
uniform Material material;
uniform Settings settings;
uniform PointLight pointLights[MAX_LIGHTS];
uniform int numberOfLights; // Number of lights currently uploaded.

uniform DirectionalLight directionalLights[1];
uniform int numberOfDirLights;

uniform vec3 cameraPosition; // World campera position.
uniform int state; // Only used for testing / debugging.
//uniform sampler3D texture3D; // Voxelization texture.

in vec3 worldPositionFrag;
in vec3 normalFrag;

out vec4 color;

vec3 normal = normalize(normalFrag); 
float MAX_DISTANCE = distance(vec3(abs(worldPositionFrag)), vec3(-1));

int traverseToLevelAndGetOffset(inout vec3 posTex, out uint foundOnLevel, in uint maxLevel) {
	vec3 nodePosTex = vec3(0.0);
	vec3 nodePosMaxTex = vec3(1.0);
	int nodeAddress = 0;
	foundOnLevel = 0;
	float sideLength = 0.5;

	for (foundOnLevel = 0; foundOnLevel < maxLevel; ++foundOnLevel) {
		uint nodeNext = imageLoad(nodePool_next, nodeAddress).x;
		uint childStartAddress = nodeNext & NODE_MASK_VALUE;
		if (childStartAddress == 0U) {
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

vec4 getSVOValue(vec3 posWorld, sampler3D brickPoolImg, uint maxLevel, vec4 emptyVal = vec4(0)) {
	vec3 posTex = (voxelGridTransformI * vec4(posWorld, 1.0)).xyz;

	vec3 maxDistV = max(max(posTex - vec3(1.0), vec3(0.0) - posTex), vec3(0.0));
	float maxDist = maxDistV.x + maxDistV.y + maxDistV.z;
	float weight = max(0.0, 1.0 - maxDist / 0.5);

	posTex = clamp(posTex, vec3(0.0001), vec3(0.9999));
	uint onLevel = 0;
	int nodeAddress = traverseToLevelAndGetOffset(posTex, onLevel, maxLevel);
	if (onLevel != maxLevel) {
		return emptyVal;
	}
	ivec3 brickAddress = ivec3(uintXYZ10ToVec3(imageLoad(nodePool_color, int(nodeAddress)).x));
	vec3 brickAddressF = (vec3(brickAddress) + vec3(0.5) + posTex * vec3(2.0)) / vec3(textureSize(brickPoolImg, 0));
	vec4 brickVal = textureLod(brickPoolImg, brickAddressF, 0);
	return brickVal;// *weight;
}

// Returns an attenuation factor given a distance.
float attenuate(float dist){ dist *= DIST_FACTOR; return 1.0f / (CONSTANT + LINEAR * dist + QUADRATIC * dist * dist); }

// Returns a vector that is orthogonal to u.
vec3 orthogonal(vec3 u){
	u = normalize(u);
	vec3 v = vec3(0.99146, 0.11664, 0.05832); // Pick any normalized vector.
	return abs(dot(u, v)) > 0.99999f ? cross(u, vec3(0, 1, 0)) : cross(u, v);
}

// Scales and bias a given vector (i.e. from [-1, 1] to [0, 1]).
vec3 scaleAndBias(const vec3 p) { return 0.5f * p + vec3(0.5f); }

// Returns true if the point p is inside the unity cube. 
bool isInsideCube(const vec3 p, float e) { return abs(p.x) < 1 + e && abs(p.y) < 1 + e && abs(p.z) < 1 + e; }

// Returns a soft shadow blend by using shadow cone tracing.
// Uses 2 samples per step, so it's pretty expensive.
//float traceShadowCone(vec3 from, vec3 direction, float targetDistance){
//	from += normal * 0.05f; // Removes artifacts but makes self shadowing for dense meshes meh.
//
//	float acc = 0;
//
//	float dist = 3 * VOXEL_SIZE;
//	// I'm using a pretty big margin here since I use an emissive light ball with a pretty big radius in my demo scenes.
//	const float STOP = targetDistance - 16 * VOXEL_SIZE;
//
//	while(dist < STOP && acc < 1){	
//		vec3 c = from + dist * direction;
//		if(!isInsideCube(c, 0)) break;
//		c = scaleAndBias(c);
//		float l = pow(dist, 2); // Experimenting with inverse square falloff for shadows.
//		float s1 = 0.062 * textureLod(texture3D, c, 1 + 0.75 * l).a;
//		float s2 = 0.135 * textureLod(texture3D, c, 4.5 * l).a;
//		float s = s1 + s2;
//		acc += (1 - acc) * s;
//		dist += 0.9 * VOXEL_SIZE * (1 + 0.05 * l);
//	}
//	return 1 - pow(smoothstep(0, 1, acc * 1.4), 1.0 / 1.4);
//}	

// Traces a diffuse voxel cone.
vec3 traceVoxelCone(const vec3 from, vec3 direction, float coneHalfAngle){
	direction = normalize(direction);
	
	const float coneTangent = tan(coneHalfAngle);
	const float coneSin = sin(coneHalfAngle);

	vec4 acc = vec4(0,0,0,1);

	// Controls bleeding from close surfaces.
	// Low values look rather bad if using shadow cone tracing.
	// Might be a better choice to use shadow maps and lower this value.
	float voxelLength = max(max(voxelSize.x, voxelSize.y), voxelSize.z);
	//float dist = voxelRadius * 8;// 0.02;// 0.1953125;

	// Trace.
	//return getSVOValue(from + direction * dist, brickPool_irradiance, uint(2)).xyz * 0.5;
	float nSubStep = 2.0;
	float radius = voxelLength * 0.5;
	for (uint i = 0; i < numLevels && acc.a > 0.05; i++, radius *= 2)
	{
		float dist = radius / coneSin;
		vec3 c = from + dist * direction;
		vec4 irradianceVoxel = getSVOValue(c, brickPool_irradiance, numLevels - i);
		vec4 normalVoxel = getSVOValue(c, brickPool_normal, numLevels - i, vec4(vec3(0.5), 0));
		float normalWeight = clamp(dot((normalVoxel.xyz - 0.5) * 2.0, direction) * -1.0, 0.0, 1.0);

		float depthVoxels = dist * (1 - coneSin) / voxelLength;
		float trans0 = clamp(1 - irradianceVoxel.a, 0, 1) + 0.01;
		float transN = pow(trans0, depthVoxels);
		float colorFactor = trans0 * (1 - transN) / (1 - trans0);
		acc.xyz += irradianceVoxel.xyz * acc.a * normalWeight * colorFactor;
		acc.a *= transN;
	}
	//while(dist < 5 && acc.a > 0.05)
	//{
	//	vec3 c = from + dist * direction;
	//	float radius = dist * coneTangent;
	//	float levelF = float(numLevels) - 1 -log2(radius / voxelRadius);
	//	levelF = clamp(levelF, 0.0, float(numLevels) - 1);
	//	float lowerLevel = floor(levelF);
	//	float upperLevel = ceil(levelF);
	//	float weight = levelF - lowerLevel;

	//	vec4 lowerVoxel = getSVOValue(c, brickPool_irradiance, uint(lowerLevel));
	//	//vec4 upperVoxel = getSVOValue(c, brickPool_irradiance, uint(upperLevel));
	//	//vec4 avgVoxel = mix(lowerVoxel, upperVoxel, weight);
	//	vec4 avgVoxel = lowerVoxel;

	//	vec4 lowerNormalVoxel = getSVOValue(c, brickPool_normal, uint(lowerLevel),vec4(vec3(0.5),0));
	//	//vec4 upperNormalVoxel = getSVOValue(c, brickPool_normal, uint(upperLevel),vec4(vec3(0.5),0));
	//	//vec4 avgNormalVoxel = mix(lowerNormalVoxel, upperNormalVoxel, weight);
	//	vec4 avgNormalVoxel = lowerNormalVoxel;

	//	float normalWeight = 1.0;// clamp(dot((avgNormalVoxel.xyz - 0.5) * 2.0, direction) * -1.0, 0.0, 1.0);
	//	acc.a *= pow(1.0 - avgVoxel.a, 1.0 / nSubStep);
	//	acc.xyz += avgVoxel.xyz * acc.a / nSubStep;
	//	dist += max(2.0 * coneTangent * dist / (1.0 - coneTangent) / nSubStep, voxelRadius*0.5);
	//}
	//acc = getSVOValue(from, brickPool_irradiance, 4);
	return acc.xyz;
	//return pow(acc.rgb * 2.0, vec3(1.5));
}

// Traces a specular voxel cone.
vec3 traceSpecularVoxelCone(vec3 from, vec3 direction) {
	return vec3(0);
}
//
//vec3 traceSpecularVoxelConeOld(vec3 from, vec3 direction){
//	direction = normalize(direction);
//
//	const float OFFSET = 8 * VOXEL_SIZE;
//	const float STEP = VOXEL_SIZE;
//
//	from += OFFSET * normal;
//	
//	vec4 acc = vec4(0.0f);
//	float dist = OFFSET;
//
//	// Trace.
//	while(dist < MAX_DISTANCE && acc.a < 1){ 
//		vec3 c = from + dist * direction;
//		if(!isInsideCube(c, 0)) break;
//		c = scaleAndBias(c); 
//		
//		float level = 0.1 * material.specularDiffusion * log2(1 + dist / VOXEL_SIZE);
//		vec4 voxel = textureLod(texture3D, c, min(level, MIPMAP_HARDCAP));
//		float f = 1 - acc.a;
//		acc.rgb += 0.25 * (1 + material.specularDiffusion) * voxel.rgb * voxel.a * f;
//		acc.a += 0.25 * voxel.a * f;
//		dist += STEP * (1.0f + 0.125f * level);
//	}
//	return 1.0 * pow(material.specularDiffusion + 1, 0.8) * acc.rgb;
//}

// Calculates indirect diffuse light using voxel cone tracing.
// The current implementation uses 9 cones. I think 5 cones should be enough, but it might generate
// more aliasing and bad blur.
vec3 indirectDiffuseLight() {
	const vec3 directionCoef[6] = {
		normalize(vec3(0, 0, 1)),       normalize(vec3(1,0,0.83)),
		normalize(vec3(0.31,0.95,0.83)),  normalize(vec3(0.31,-0.95,0.83)),
		normalize(vec3(-0.81,0.58,0.83)), normalize(vec3(-0.81,-0.58,0.83)),
	};

	const float coneAngle[6] = {
		60,40,40,40,40,40,
	};

	const vec3 xAxis = normalize(orthogonal(normal));
	const vec3 yAxis = normalize(cross(xAxis, normal));
	const vec3 zAxis = normal;

	const vec3 origin = worldPositionFrag + normal * length(voxelSize) * 1.0;
	vec3 acc = vec3(0);

	for (int i = 0; i < 6; i++)
	{
		vec3 coef = directionCoef[i];
		const float coneHalfAngle = coneAngle[i] / 180 * 3.1415;
		vec3 direction = xAxis * coef.x + yAxis * coef.y + zAxis * coef.z;
		float solidAngle = 2 * 3.14 * (1 - cos(coneHalfAngle));
		acc += traceVoxelCone(origin, direction, coneHalfAngle) * coef.z * solidAngle;
	}
	return material.diffuseReflectivity * acc * material.diffuseColor;
}

vec3 indirectDiffuseLightOld(){
	const float ANGLE_MIX = 0.5; // Angle mix (1.0f => orthogonal direction, 0.0f => direction of normal).

	const float w[3] = {1.0, 1.0, 1.0}; // Cone weights.

	// Find a base for the side cones with the normal as one of its base vectors.
	const vec3 ortho = normalize(orthogonal(normal));
	const vec3 ortho2 = normalize(cross(ortho, normal));

	// Find base vectors for the corner cones too.
	const vec3 corner = 0.5f * (ortho + ortho2);
	const vec3 corner2 = 0.5f * (ortho - ortho2);

	// Find start position of trace (start with a bit of offset).
	const vec3 N_OFFSET = normal * length(voxelSize) * 1.0;// (1 + 4 * ISQRT2) * VOXEL_SIZE;
	const vec3 C_ORIGIN = worldPositionFrag +N_OFFSET;

	// Accumulate indirect diffuse light.
	vec3 acc = vec3(0);

	// We offset forward in normal direction, and backward in cone direction.
	// Backward in cone direction improves GI, and forward direction removes
	// artifacts.
	//const float CONE_OFFSET = -0.01;
	const float CONE_OFFSET = -0.0;

	// Trace 4 side cones.
	const vec3 s1 = mix(normal, ortho, ANGLE_MIX);
	const vec3 s2 = mix(normal, -ortho, ANGLE_MIX);
	const vec3 s3 = mix(normal, ortho2, ANGLE_MIX);
	const vec3 s4 = mix(normal, -ortho2, ANGLE_MIX);

	// Trace front cone
	float coneHalfAngle = 3.1415 / 3.0 * 0.5;
	acc += w[0] * traceVoxelCone(C_ORIGIN + CONE_OFFSET * normal, normal, coneHalfAngle);
	//acc += w[0] * traceVoxelCone(C_ORIGIN + CONE_OFFSET * ortho, s1) * 5.0;
	//return acc;// DIFFUSE_INDIRECT_FACTOR * material.diffuseReflectivity * acc * (material.diffuseColor + vec3(0.001f));


	acc += w[1] * traceVoxelCone(C_ORIGIN + CONE_OFFSET * ortho, s1, coneHalfAngle);
	acc += w[1] * traceVoxelCone(C_ORIGIN - CONE_OFFSET * ortho, s2, coneHalfAngle);
	acc += w[1] * traceVoxelCone(C_ORIGIN + CONE_OFFSET * ortho2, s3, coneHalfAngle);
	acc += w[1] * traceVoxelCone(C_ORIGIN - CONE_OFFSET * ortho2, s4, coneHalfAngle);

	// Trace 4 corner cones.
	const vec3 c1 = mix(normal, corner, ANGLE_MIX);
	const vec3 c2 = mix(normal, -corner, ANGLE_MIX);
	const vec3 c3 = mix(normal, corner2, ANGLE_MIX);
	const vec3 c4 = mix(normal, -corner2, ANGLE_MIX);

	acc += w[2] * traceVoxelCone(C_ORIGIN + CONE_OFFSET * corner, c1, coneHalfAngle);
	acc += w[2] * traceVoxelCone(C_ORIGIN - CONE_OFFSET * corner, c2, coneHalfAngle);
	acc += w[2] * traceVoxelCone(C_ORIGIN + CONE_OFFSET * corner2, c3, coneHalfAngle);
	acc += w[2] * traceVoxelCone(C_ORIGIN - CONE_OFFSET * corner2, c4, coneHalfAngle);

	// Return result.
	// return DIFFUSE_INDIRECT_FACTOR * material.diffuseReflectivity * acc * (material.diffuseColor + vec3(0.001f));
	return material.diffuseReflectivity * acc * (material.diffuseColor + vec3(0.001f));
}

// Calculates indirect specular light using voxel cone tracing.
vec3 indirectSpecularLight(vec3 viewDirection){
	const float cosVal = clamp(dot(-viewDirection, normal), 0.0, 1.0);
	const vec3 reflection = normalize(reflect(viewDirection, normal));
	const float df = 1.0f / (1.0f + 0.25f * material.specularDiffusion); // Diffusion factor.
	const float specularExponent = df * SPECULAR_POWER;
	const float coneHalfAngle = material.specularDiffusion * 0.5;// 0.0043697*specularExponent*specularExponent - 0.136492*specularExponent + 1.50625;

	//float specularAngle = max(0, dot(reflection, lightDirection));
	const vec3 origin = worldPositionFrag + normal * length(voxelSize) * 1;
	vec3 incomeSpecular = traceVoxelCone(origin, reflection, coneHalfAngle);
	//const float specular = pow(specularAngle, specularExponent);
	return material.specularReflectivity * material.specularColor * incomeSpecular * cosVal;
}
// Calculates refractive light using voxel cone tracing.
//vec3 indirectRefractiveLight(vec3 viewDirection){
//	const vec3 refraction = refract(viewDirection, normal, 1.0 / material.refractiveIndex);
//	const vec3 cmix = mix(material.specularColor, 0.5 * (material.specularColor + vec3(1)), material.transparency);
//	return cmix * traceSpecularVoxelCone(worldPositionFrag, refraction);
//}
// Calculates diffuse and specular direct light for a given point light.  
// Uses shadow cone tracing for soft shadows.
vec3 calculateDirectLight(const PointLight light, const vec3 viewDirection){
	vec3 lightDirection = light.position - worldPositionFrag;
	const float distanceToLight = length(lightDirection);
	lightDirection = lightDirection / distanceToLight;
	const float lightAngle = dot(normal, lightDirection);
	
	// --------------------
	// Diffuse lighting.
	// --------------------
	float diffuseAngle = max(lightAngle, 0.0f); // Lambertian.	
	
	// --------------------
	// Specular lighting.
	// --------------------
#if (SPECULAR_MODE == 0) /* Blinn-Phong. */
	const vec3 halfwayVector = normalize(lightDirection + viewDirection);
	float specularAngle = max(dot(normal, halfwayVector), 0.0f);
#endif
	
#if (SPECULAR_MODE == 1) /* Perfect reflection. */
	const vec3 reflection = normalize(reflect(viewDirection, normal));
	float specularAngle = max(0, dot(reflection, lightDirection));
#endif

	float refractiveAngle = 0;
	if(material.transparency > 0.01){
		vec3 refraction = refract(viewDirection, normal, 1.0 / material.refractiveIndex);
		refractiveAngle = max(0, material.transparency * dot(refraction, lightDirection));
	}

	// --------------------
	// Shadows.
	// --------------------
	float shadowBlend = 1;
#if (SHADOWS == 1)
	if (diffuseAngle * (1.0f - material.transparency) > 0 && settings.shadows)
		shadowBlend = 1;// traceShadowCone(worldPositionFrag, lightDirection, distanceToLight);
#endif

	// --------------------
	// Add it all together.
	// --------------------
	diffuseAngle = min(shadowBlend, diffuseAngle);
	specularAngle = min(shadowBlend, max(specularAngle, refractiveAngle));
	const float df = 1.0f / (1.0f + 0.25f * material.specularDiffusion); // Diffusion factor.
	const float specular = SPECULAR_FACTOR * pow(specularAngle, df * SPECULAR_POWER);
	const float diffuse = diffuseAngle * (1.0f - material.transparency);

	const vec3 diff = material.diffuseReflectivity * material.diffuseColor * diffuse;
	const vec3 spec = material.specularReflectivity * material.specularColor * specular;
	const vec3 total = light.color * (diff + spec);
	return attenuate(distanceToLight) * total;
};

vec3 calculateDirectDirLight(const DirectionalLight light, const vec3 viewDirection) {
	vec3 pointPosToLight = worldPositionFrag - light.position;
	if (dot(pointPosToLight, light.direction) < 0)
		return vec3(0);
	if (dot(normal, light.direction * -1.0) < 0)
		return vec3(0);

	vec3 xAxis = normalize(cross(light.direction, light.up));
	vec3 yAxis = normalize(cross(xAxis, light.direction));
	float xProj = dot(pointPosToLight, xAxis);
	float yProj = dot(pointPosToLight, yAxis);
	float zProj = dot(pointPosToLight, light.direction);
	float halfWidth = light.size.x*0.5;
	float halfHeight = light.size.y*0.5;
	if (xProj < -halfWidth || xProj > halfWidth)
		return vec3(0);
	if (yProj < -halfHeight || yProj > halfHeight)
		return vec3(0);

	vec2 uv = vec2(xProj / halfWidth, yProj / halfHeight) * 0.5 + 0.5;
	vec3 shadowPosWS =texture(smPosition, uv).xyz;
	float shadowZ = dot(shadowPosWS - light.position, light.direction);
	if (shadowZ > 0.0 && shadowZ < zProj - 0.04)
		return vec3(0);

	vec3 lightDirection = light.direction;
	const float distanceToLight = zProj;
	const float lightAngle = dot(normal, light.direction * -1.0);

	// --------------------
	// Diffuse lighting.
	// --------------------
	float diffuseAngle = max(lightAngle, 0.0f); // Lambertian.	

												// --------------------
												// Specular lighting.
												// --------------------
#if (SPECULAR_MODE == 0) /* Blinn-Phong. */
	const vec3 halfwayVector = normalize(lightDirection + viewDirection);
	float specularAngle = max(dot(normal, halfwayVector), 0.0f);
#endif

#if (SPECULAR_MODE == 1) /* Perfect reflection. */
	const vec3 reflection = normalize(reflect(viewDirection, normal));
	float specularAngle = max(0, dot(reflection, lightDirection));
#endif

	float refractiveAngle = 0;
	if (material.transparency > 0.01) {
		vec3 refraction = refract(viewDirection, normal, 1.0 / material.refractiveIndex);
		refractiveAngle = max(0, material.transparency * dot(refraction, lightDirection));
	}

	// --------------------
	// Shadows.
	// --------------------
	float shadowBlend = 1;

	// --------------------
	// Add it all together.
	// --------------------
	diffuseAngle = min(shadowBlend, diffuseAngle);
	specularAngle = min(shadowBlend, max(specularAngle, refractiveAngle));
	const float df = 1.0f / (1.0f + 0.25f * material.specularDiffusion); // Diffusion factor.
	const float specular = SPECULAR_FACTOR * pow(specularAngle, df * SPECULAR_POWER);
	const float diffuse = diffuseAngle * (1.0f - material.transparency);

	const vec3 diff = material.diffuseReflectivity * material.diffuseColor * diffuse;
	const vec3 spec = material.specularReflectivity * material.specularColor * specular;
	const vec3 total = light.color * (diff + spec);
	return attenuate(distanceToLight) * total;
};
// Sums up all direct light from point lights (both diffuse and specular).
vec3 directLight(vec3 viewDirection){
	vec3 direct = vec3(0.0f);
	const uint maxLights = min(numberOfLights, MAX_LIGHTS);
	for(uint i = 0; i < maxLights; ++i)
		direct += calculateDirectLight(pointLights[i], viewDirection);
	for (uint i = 0; i < numberOfDirLights; ++i)
		direct += calculateDirectDirLight(directionalLights[i], viewDirection);
	//direct *= DIRECT_LIGHT_INTENSITY;
	return direct;
}

void main(){
	color = vec4(0, 0, 0, 1);
	vec3 viewDir = normalize(worldPositionFrag - cameraPosition);

	// Indirect diffuse light.
	if(settings.indirectDiffuseLight && material.diffuseReflectivity * (1.0f - material.transparency) > 0.01f) 
		color.rgb += indirectLightMultiplier * indirectDiffuseLight();

	//// Indirect specular light (glossy reflections).
	if(settings.indirectSpecularLight && material.specularReflectivity * (1.0f - material.transparency) > 0.01f) 
		color.rgb += indirectLightMultiplier * indirectSpecularLight(viewDir);

	//// Emissivity.
	//color.rgb += material.emissivity * material.diffuseColor;

	//// Transparency
	//if(material.transparency > 0.01f)
	//	color.rgb = mix(color.rgb, indirectRefractiveLight(viewDir), material.transparency);

	// Direct light.
	if(settings.directLight)
		color.rgb += directLightMultiplier * directLight(viewDir);

//#if (GAMMA_CORRECTION == 1)
//	color.rgb = pow(color.rgb, vec3(1.0 / 2.2));
//#endif
}