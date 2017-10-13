#pragma once

#include <vector>

#define GLEW_STATIC
#include <glew.h>
#include <glfw3.h>

#include "..\Scene\Scene.h"
#include "Material\Material.h"
#include "FBO\FBO.h"
#include "Camera\OrthographicCamera.h"
#include "../Shape/Mesh.h"
#include "Texture3D.h"
#include "TextureBuffer.h"
#include "IndexBuffer.h"

#define MAX_NODE_POOL_LEVELS 12
class MeshRenderer;
class Shape;

/// <summary> A graphical context used for rendering. </summary>
class Graphics {
	using RenderingQueue = const std::vector<MeshRenderer*> &;
public:
	enum RenderingMode {
		VOXELIZATION_VISUALIZATION = 0, // Voxelization visualization.
		VOXEL_CONE_TRACING = 1			// Global illumination using voxel cone tracing.
	};

	/// <summary> Initializes rendering. </summary>
	virtual void init(unsigned int viewportWidth, unsigned int viewportHeight); // Called pre-render once per run.

	/// <sumamry> Renders a scene using a given rendering mode. </summary>
	virtual void render(
		Scene & renderingScene, unsigned int viewportWidth,
		unsigned int viewportHeight, RenderingMode renderingMode = RenderingMode::VOXEL_CONE_TRACING
	);

	// ----------------
	// Rendering.
	// ----------------
	bool shadows = true;
	bool indirectDiffuseLight = true;
	bool indirectSpecularLight = true;
	bool directLight = true;

	// ----------------
	// Voxelization.
	// ----------------
	bool automaticallyRegenerateMipmap = true;
	bool regenerateMipmapQueued = true;
	bool automaticallyVoxelize = true;
	bool voxelizationQueued = true;
	int voxelizationSparsity = 1; // Number of ticks between mipmap generation. 
	// (voxelization sparsity gives unstable framerates, so not sure if it's worth it in interactive applications.)

	~Graphics();
private:
	// ----------------
	// GLSL uniform names.
	// ----------------
	const char * PROJECTION_MATRIX_NAME = "P";
	const char * VIEW_MATRIX_NAME = "V";
	const char * CAMERA_POSITION_NAME = "cameraPosition";
	const char * NUMBER_OF_LIGHTS_NAME = "numberOfLights";
	const char * SCREEN_SIZE_NAME = "screenSize";
	const char * APP_STATE_NAME = "state";

	// ----------------
	// Rendering.
	// ----------------
	void renderScene(Scene & renderingScene, unsigned int viewportWidth, unsigned int viewportHeight);
	void renderQueue(RenderingQueue renderingQueue, const GLuint program, bool uploadMaterialSettings = false) const;
	void uploadGlobalConstants(const GLuint program, unsigned int viewportWidth, unsigned int viewportHeight) const;
	void uploadCamera(Camera & camera, const GLuint program);
	void uploadLighting(Scene & renderingScene, const GLuint glProgram) const;
	void uploadRenderingSettings(const GLuint glProgram) const;
	glm::mat4 getVoxelTransformInverse(Scene & renderingScene);
	glm::mat4 getVoxelTransform(Scene & renderingScene);
	// ----------------
	// Voxel cone tracing.
	// ----------------
	Material * voxelConeTracingMaterial;

  // ----------------
  // Sparse Voxel Tree
  // ----------------
  void initSparseVoxelization();
  void sparseVoxelize(Scene & renderingScene, bool clearVoxelizationFirst = true);
  // sparse voxelize functions
  void clearNodePool(Scene& renderingScene);
  void clearBrickPool(Scene& renderingScene);
  void clearFragmentTex(Scene& renderingScene);
  void voxelizeScene(Scene& renderingScene);
  void modifyIndirectBuffer(Scene& renderingScene);
  void visualizeVoxel(Scene& renderingScene, unsigned int viewportWidth, unsigned int viewportHeight);
  void flagNode(Scene& renderingScene);
  void allocateNode(Scene& renderingScene, int level);
  void findNeighbours(Scene& renderingScene, int level);

  struct IndirectDrawCommand {
    uint32_t numVertices;
    uint32_t numPrimitives;
    uint32_t firstVertexIdx;
    uint32_t baseInstanceIdx;
  };

  // Node pool
  enum NodePoolData
  {
    NODE_POOL_NEXT,
    NODE_POOL_COLOR,
    NODE_POOL_NORMAL,
    NODE_POOL_NEIGH_X,
    NODE_POOL_NEIGH_X_NEG,
    NODE_POOL_NEIGH_Y,
    NODE_POOL_NEIGH_Y_NEG,
    NODE_POOL_NEIGH_Z,
    NODE_POOL_NEIGH_Z_NEG,
    NODE_POOL_NUM_TEXTURES
  };
  std::shared_ptr<TextureBuffer> m_nodePoolTextures[NODE_POOL_NUM_TEXTURES];
  std::shared_ptr<TextureBuffer> m_levelAddressBuffer;
  int m_nodePoolDim;
  int m_maxNodes; // max nodes = 1 + 8 + 8^2 + ... + nodePoolDim ^ 3
  std::shared_ptr<IndexBuffer> m_nextFreeNode;

  // Brick pool
  enum BrickPoolData {
	  BRICK_POOL_COLOR,
	  BRICK_POOL_IRRADIANCE,
	  BRICK_POOL_NORMAL,
	  BRICK_POOL_COLOR_X,
	  BRICK_POOL_COLOR_X_NEG,
	  BRICK_POOL_COLOR_Y,
	  BRICK_POOL_COLOR_Y_NEG,
	  BRICK_POOL_COLOR_Z,
	  BRICK_POOL_COLOR_Z_NEG,
	  BRICK_POOL_NUM_TEXTURES
  };
  int m_brickPoolDim; // brick pool voxels = dim * dim * dim
  std::shared_ptr<Texture3D> m_brickPoolTextures[BRICK_POOL_NUM_TEXTURES];

  // Fragment Texure
  enum FragmentTexData {
	  FRAG_TEX_COLOR,
	  FRAG_TEX_NORMAL,
	  FRAG_TEX_NUM_TEXTURES,
  };
  std::shared_ptr<Texture3D> m_fragmentTextures[FRAG_TEX_NUM_TEXTURES];

  // Fragment List
  std::shared_ptr<TextureBuffer> m_fragmentList;
  std::shared_ptr<IndexBuffer> m_fragmentListCounter;

  // Draw command
  std::shared_ptr<IndexBuffer> m_nodePoolDrawCommandBuffer;
  std::shared_ptr<IndexBuffer> m_brickPoolDrawCommandBuffer;
  std::shared_ptr<IndexBuffer> m_fragmentTexDrawCommandBuffer;
  std::shared_ptr<IndexBuffer> m_modifyIndirectBufferCommandBuffer;
  std::shared_ptr<TextureBuffer> m_fragmentListDrawCommandBuffer;
  std::shared_ptr<IndexBuffer> m_nodePoolUpToLevelDrawCommandBuffer[MAX_NODE_POOL_LEVELS];
  std::shared_ptr<IndexBuffer> m_nodePoolOnLevelDrawCommandBuffer[MAX_NODE_POOL_LEVELS];

  glm::vec3 sceneBoxMin;
  glm::vec3 sceneBoxMax;

	// ----------------
	// Voxelization.
	// ----------------
	int ticksSinceLastVoxelization = voxelizationSparsity;
	GLuint voxelTextureSize = 64; // Must be set to a power of 2.
	OrthographicCamera voxelCamera;
	Material * voxelizationMaterial;
	Texture3D * voxelTexture = nullptr;

	void initVoxelization();
	void voxelize(Scene & renderingScene, bool clearVoxelizationFirst = true);

	// ----------------
	// Voxelization visualization.
	// ----------------
	void initVoxelVisualization(unsigned int viewportWidth, unsigned int viewportHeight);
	void renderVoxelVisualization(Scene & renderingScene, unsigned int viewportWidth, unsigned int viewportHeight);
	FBO *vvfbo1, *vvfbo2;
	Material * worldPositionMaterial, *voxelVisualizationMaterial;
	// --- Screen quad. ---
	MeshRenderer * quadMeshRenderer;
	Mesh quad;
	// --- Screen cube. ---
	MeshRenderer * cubeMeshRenderer;
	Shape * cubeShape;
};