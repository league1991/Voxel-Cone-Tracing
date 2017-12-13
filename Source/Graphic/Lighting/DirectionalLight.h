#pragma once

#define GLEW_STATIC
#include <glew.h>
#include <glfw3.h>
#include <gtc/type_ptr.hpp>
#include "gtc/matrix_transform.hpp"
#include <glm.hpp>

#include <iostream>
#include <string>

/// <summary> A simple point light. </summary>
class DirectionalLight {
public:
	bool tweakable = true;
	glm::vec3 m_position;
	glm::vec3 m_direction;
	glm::vec3 m_up;
	float m_width, m_height;
	glm::vec3 m_color;
	float m_intensity;

	DirectionalLight(
		glm::vec3 position,
		glm::vec3 direction,
		glm::vec3 up,
		float width, float height,
		glm::vec3 color):
		m_position(position),
		m_direction(direction),
		m_up(up),
		m_width(width), m_height(height),
		m_color(color), m_intensity(1.0f) {}

	void Upload(GLuint program, GLuint index) const {
		glm::vec3 color = m_color * m_intensity;
		glUniform3fv(glGetUniformLocation(program, ("directionalLights[" + std::to_string(index) + "].position").c_str()), 1, glm::value_ptr(m_position));
		glUniform3fv(glGetUniformLocation(program, ("directionalLights[" + std::to_string(index) + "].direction").c_str()), 1, glm::value_ptr(m_direction));
		glUniform3fv(glGetUniformLocation(program, ("directionalLights[" + std::to_string(index) + "].up").c_str()), 1, glm::value_ptr(m_up));
		glUniform2f (glGetUniformLocation(program, ("directionalLights[" + std::to_string(index) + "].size").c_str()), m_width, m_height);
		glUniform3fv(glGetUniformLocation(program, ("directionalLights[" + std::to_string(index) + "].color").c_str()), 1, glm::value_ptr(color));
	}

	glm::mat4 getLightViewMatrix() const {
		return glm::lookAt(m_position, m_position + m_direction, m_up);
	}

	glm::mat4 getLightProjectionMatrix() const {
		return glm::ortho(-m_width / 2.0, m_width / 2.0,-m_height / 2.0, m_height / 2.0, 0.0, 10.0);
	}
};