#include "Scene.h"
#include "../Shape/Shape.h"

void Scene::getBoundingBox(glm::vec3 & boxMin, glm::vec3 & boxMax)
{
  boxMin = glm::vec3(FLT_MAX);
  boxMax = glm::vec3(-FLT_MAX);
  for (auto shapePtr : shapes)
  {
    glm::vec3 minPnt, maxPnt;
    shapePtr->getBoundingBox(minPnt, maxPnt);

    for (int i = 0; i < 3; i++)
    {
      boxMin[i] = std::min(boxMin[i], minPnt[i]);
      boxMax[i] = std::max(boxMax[i], maxPnt[i]);
    }
  }
}
