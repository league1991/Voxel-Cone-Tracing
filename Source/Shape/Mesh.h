#pragma once

#include <vector>
#include <algorithm>

#include "VertexData.h"

/// <summary> Represents a basic mesh with OpenGL related attributes (vertex data, indices), 
/// and variables (VAO, VAO, and EBO identifiers). </summary>
class Mesh {
public:
	/// <summary> If the mesh is static, i.e. does not change over time, set this to true to improve performance. </summary>
	bool staticMesh = true;

	Mesh();
	~Mesh();

  void getBoundingBox(glm::vec3& minBox, glm::vec3& maxBox)
  {
    minBox = glm::vec3(FLT_MAX);
    maxBox = glm::vec3(-FLT_MAX);
    for (size_t i = 0; i < vertexData.size(); i++)
    {
      auto& vtx = vertexData[i].position;
      for (int j = 0; j < 3; j++)
      {
        minBox[j] = std::min(minBox[j], vtx[j]);
        maxBox[j] = std::max(maxBox[j], vtx[j]);
      }
    }
  }

	std::vector<VertexData> vertexData;
	std::vector<unsigned int> indices;

	// Used for (shared) rendering.
	int program;
	unsigned int vbo, vao, ebo; // Vertex Buffer Object, Vertex Array Object, Element Buffer Object.
	bool meshUploaded = false;
private:
	static unsigned int idCounter;
};