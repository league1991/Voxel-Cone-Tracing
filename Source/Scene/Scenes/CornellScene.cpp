#include "CornellScene.h"

#define GLEW_STATIC
#include <glew.h>
#include <glfw3.h>
#include <gtx/rotate_vector.hpp>

#include "../../Graphic/Lighting/PointLight.h"
#include "../../Time/Time.h"
#include "../../Utility/ObjLoader.h"
#include "../../Graphic/Renderer/MeshRenderer.h"
#include "../../Graphic/Material/MaterialSetting.h"

// Settings.
namespace { unsigned int lightSphereIndex = 0; }

void CornellScene::init(unsigned int viewportWidth, unsigned int viewportHeight) {
	FirstPersonScene::init(viewportWidth, viewportHeight);

	// Cornell box.
	Shape * cornell = ObjLoader::loadObjFile(
		"Assets\\Models\\cornell_teapot.obj",
		"Assets\\Models\\");
	shapes.push_back(cornell);
	for (unsigned int i = 0; i < cornell->meshes.size(); ++i) {
		auto& mesh = cornell->meshes[i];
		MaterialSetting* setting = nullptr;
		if (mesh.materialID < cornell->materialSettings.size())
		{
			setting = &cornell->materialSettings[mesh.materialID];
		}
		auto renderer = new MeshRenderer(&mesh, setting);
		renderers.push_back(renderer);
	}
	for (auto & r : renderers) {
		r->tweakable = true;
		r->transform.position -= glm::vec3(0.00f, 0.0f, 0);
		r->transform.scale = glm::vec3(0.995f);
		r->transform.updateTransformMatrix();
	}

	// Light sphere.
	Shape * lightSphere = ObjLoader::loadObjFile("Assets\\Models\\sphere.obj");
	shapes.push_back(lightSphere);
	for (unsigned int i = 0; i < lightSphere->meshes.size(); ++i) {
		auto renderer = new MeshRenderer(&(lightSphere->meshes[i]));
		renderer->tweakable = true;
		renderers.push_back(renderer);
	}
	lightSphereIndex = renderers.size() - 1;

	// Cornell box.
	//renderers[0]->materialSetting = MaterialSetting::Green(); // Green wall.
	//renderers[1]->materialSetting = MaterialSetting::White(); // Floor.
	//renderers[2]->materialSetting = MaterialSetting::White(); // Roof.
	//renderers[3]->materialSetting = MaterialSetting::Red(); // Red wall.
	//renderers[4]->materialSetting = MaterialSetting::White(); // White wall.
	//renderers[5]->materialSetting = MaterialSetting::White(); // Left box.
	//renderers[5]->tweakable = true;
	//renderers[6]->materialSetting = MaterialSetting::White(); // Right box.
	//renderers[6]->tweakable = true;

	// Light Sphere.
	renderers[lightSphereIndex]->materialSetting = MaterialSetting::Emissive();
	renderers[lightSphereIndex]->materialSetting->diffuseColor.r = 1.0f;
	renderers[lightSphereIndex]->materialSetting->diffuseColor.g = 1.0f;
	renderers[lightSphereIndex]->materialSetting->diffuseColor.b = 1.0f;
	renderers[lightSphereIndex]->materialSetting->emissivity = 8.0f;
	renderers[lightSphereIndex]->materialSetting->specularReflectivity = 0.0f;
	renderers[lightSphereIndex]->materialSetting->diffuseReflectivity = 0.0f;

	// ----------
	// Lighting.
	// ----------
	//PointLight p;
	//pointLights.push_back(p);
	//pointLights[0].color = glm::vec3(1.4f, 0.9f, 0.35f);
	//pointLights[0].color = normalize(pointLights[0].color);
	DirectionalLight d(glm::vec3(0,0.5,0), glm::vec3(0,-1,0), glm::vec3(1,0,0),1.5,1.5, glm::vec3(1,1,1));
	directionalLights.push_back(d);
}


void CornellScene::update() {
	FirstPersonScene::update();

	glm::vec3 r = glm::vec3(sinf(float(Time::time * 0.97)), sinf(float(Time::time * 0.45)), sinf(float(Time::time * 0.32)));

	// Lighting.
	float ballRadius = 0.1;
	renderers[lightSphereIndex]->transform.position = glm::vec3(0, 0, sinf(float(Time::time * 0.3)));
	//renderers[lightSphereIndex]->transform.rotation = r;
	renderers[lightSphereIndex]->transform.scale = glm::vec3(ballRadius, ballRadius,0.1);
	renderers[lightSphereIndex]->transform.updateTransformMatrix();

	//pointLights[0].position = glm::vec3(0, 0.5, 0.1) + r * 0.1f;// renderers[lightSphereIndex]->transform.position;
	//pointLights[0].position.x *= 4.5f;
	//pointLights[0].position.z = pointLights[0].position.z * 4.5f;
	directionalLights[0].m_direction = glm::vec3(0.5*sinf(float(Time::time * 0.3)), -1, 0.5*cosf(float(Time::time * 0.3)));
	glm::normalize(directionalLights[0].m_direction);
	renderers[lightSphereIndex]->materialSetting->diffuseColor = directionalLights[0].m_color;
}

CornellScene::~CornellScene() {
	for (auto * r : renderers) delete r;
	for (auto * s : shapes) delete s;
}
