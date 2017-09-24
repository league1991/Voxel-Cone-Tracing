#pragma once

#include <iostream> // TODO: Remove.

#define GLEW_STATIC
#include <glew.h>
#include <glfw3.h>
#include <gtx/rotate_vector.hpp>

#include "../../Camera/Camera.h"
#include "../../../Time/Time.h"
#include "../../Camera\PerspectiveCamera.h"
#include "../../../Application.h"

/// <summary> A first person controller that can be attached to a camera. </summary>
class FirstPersonController {
public:
	const float CAMERA_SPEED = 1.4f;
	const float CAMERA_ROTATION_SPEED = 0.0003f;
	const float CAMERA_POSITION_INTERPOLATION_SPEED = 800.0f;
	const float CAMERA_ROTATION_INTERPOLATION_SPEED = 8.0f;

	Camera * renderingCamera;
	Camera * targetCamera; // Dummy camera used for interpolation.

	FirstPersonController(Camera * camera) {
		targetCamera = new PerspectiveCamera();
		renderingCamera = camera;

	}

	FirstPersonController() { delete targetCamera; }

  void update();
private:
	bool firstUpdate = true;
};
