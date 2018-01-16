#pragma once

#include <vector>

#include "Transform.h"
#include "Mesh.h"

#include "../Graphic/Material/MaterialSetting.h"
/// <summary> A 'concatenation' of several meshes. </summary>
class Shape {
public:
	std::vector<Mesh> meshes;
	std::vector<MaterialSetting> materialSettings;
	Shape() {}
	~Shape() {}

  void getBoundingBox(glm::vec3& boxMin, glm::vec3& boxMax)
  {
    boxMin = glm::vec3(FLT_MAX);
    boxMax = glm::vec3(-FLT_MAX);
    for (auto& mesh: meshes)
    {
      glm::vec3 minPnt, maxPnt;
      mesh.getBoundingBox(minPnt, maxPnt);
      for (int i = 0; i < 3; i++)
      {
        boxMin[i] = std::min(boxMin[i], minPnt[i]);
        boxMax[i] = std::max(boxMax[i], maxPnt[i]);
      }
    }
  }
};