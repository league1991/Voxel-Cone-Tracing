#include "Graphics.h"

// Stdlib.
#include <queue>
#include <algorithm>
#include <vector>

// External.
#include <glm.hpp>
#include <gtc/type_ptr.hpp>

// Internal.
#include "Texture2D.h"
#include "Material\Material.h"
#include "Camera\OrthographicCamera.h"
#include "Material\MaterialStore.h"
#include "../Time/Time.h"
#include "../Shape/Mesh.h"
#include "../Shape/StandardShapes.h"
#include "Renderer\MeshRenderer.h"
#include "../Utility/ObjLoader.h"
#include "../Shape/Shape.h"
#include "../Application.h"

// ----------------------
// Rendering pipeline.
// ----------------------
void Graphics::init(unsigned int viewportWidth, unsigned int viewportHeight)
{
	glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
	glEnable(GL_MULTISAMPLE); // MSAA. Set MSAA level using GLFW (see Application.cpp).
	voxelConeTracingMaterial = MaterialStore::getInstance().findMaterialWithName("voxel_cone_tracing");
	voxelCamera = OrthographicCamera(viewportWidth / float(viewportHeight));
	initVoxelization();
	initSparseVoxelization();
	initVoxelVisualization(viewportWidth, viewportHeight);
}

void Graphics::render(Scene & renderingScene, unsigned int viewportWidth, unsigned int viewportHeight, RenderingMode renderingMode)
{
	// Voxelize.
	bool voxelizeNow = voxelizationQueued || (automaticallyVoxelize && voxelizationSparsity > 0 && ++ticksSinceLastVoxelization >= voxelizationSparsity);
	if (voxelizeNow) {
		voxelize(renderingScene, true);
		ticksSinceLastVoxelization = 0;
		voxelizationQueued = false;
	}
	shadowMap(renderingScene);
	sparseVoxelize(renderingScene, true);
	lightUpdate(renderingScene, true);

	// Render.
	switch (renderingMode) {
	case RenderingMode::VOXELIZATION_VISUALIZATION:
		//renderVoxelVisualization(renderingScene, viewportWidth, viewportHeight);
		visualizeVoxel(renderingScene, viewportWidth, viewportHeight, m_ithVisualizeLevel);
		break;
	case RenderingMode::VOXEL_CONE_TRACING:
		renderScene(renderingScene, viewportWidth, viewportHeight);
		break;
	}
}

// ----------------------
// Scene rendering.
// ----------------------
void Graphics::renderScene(Scene & renderingScene, unsigned int viewportWidth, unsigned int viewportHeight)
{
	// Fetch references.
	auto & camera = *renderingScene.renderingCamera;
	const Material * material = voxelConeTracingMaterial;
	const GLuint program = material->program;

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glUseProgram(program);

	// GL Settings.
	glViewport(0, 0, viewportWidth, viewportHeight);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	// Texture.
	voxelTexture->Activate(material->program, "texture3D", 0);
	glBindImageTexture(0, voxelTexture->textureID, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA8);

	// Upload uniforms.
	uploadCamera(camera, program);
	uploadGlobalConstants(program, viewportWidth, viewportHeight);
	uploadLighting(renderingScene, program);
	uploadRenderingSettings(program);

	// Render.
	renderQueue(renderingScene.renderers, material->program, true);
}

void Graphics::uploadLighting(Scene & renderingScene, const GLuint program) const
{
	// Point lights.
	for (unsigned int i = 0; i < renderingScene.pointLights.size(); ++i) renderingScene.pointLights[i].Upload(program, i);

	// Number of point lights.
	glUniform1i(glGetUniformLocation(program, NUMBER_OF_LIGHTS_NAME), renderingScene.pointLights.size());
}

void Graphics::uploadRenderingSettings(const GLuint glProgram) const
{
	glUniform1i(glGetUniformLocation(glProgram, "settings.shadows"), shadows);
	glUniform1i(glGetUniformLocation(glProgram, "settings.indirectDiffuseLight"), indirectDiffuseLight);
	glUniform1i(glGetUniformLocation(glProgram, "settings.indirectSpecularLight"), indirectSpecularLight);
	glUniform1i(glGetUniformLocation(glProgram, "settings.directLight"), directLight);
}

void Graphics::uploadGlobalConstants(const GLuint program, unsigned int viewportWidth, unsigned int viewportHeight) const
{
	glUniform1i(glGetUniformLocation(program, APP_STATE_NAME), Application::getInstance().state);
	glm::vec2 screenSize(viewportWidth, viewportHeight);
}

void Graphics::uploadCamera(Camera & camera, const GLuint program)
{
	glUniformMatrix4fv(glGetUniformLocation(program, VIEW_MATRIX_NAME), 1, GL_FALSE, glm::value_ptr(camera.viewMatrix));
	glUniformMatrix4fv(glGetUniformLocation(program, PROJECTION_MATRIX_NAME), 1, GL_FALSE, glm::value_ptr(camera.getProjectionMatrix()));
	glUniform3fv(glGetUniformLocation(program, CAMERA_POSITION_NAME), 1, glm::value_ptr(camera.position));
}

void Graphics::renderQueue(RenderingQueue renderingQueue, const GLuint program, bool uploadMaterialSettings) const
{
	for (unsigned int i = 0; i < renderingQueue.size(); ++i) if (renderingQueue[i]->enabled)
		renderingQueue[i]->transform.updateTransformMatrix();

	for (unsigned int i = 0; i < renderingQueue.size(); ++i) if (renderingQueue[i]->enabled) {
		if (uploadMaterialSettings && renderingQueue[i]->materialSetting != nullptr) {
			renderingQueue[i]->materialSetting->Upload(program, false);
		}
		renderingQueue[i]->render(program);
	}
}

// ----------------------
// Voxelization.
// ----------------------
void Graphics::initVoxelization()
{
	voxelizationMaterial = MaterialStore::getInstance().findMaterialWithName("voxelization");

	assert(voxelizationMaterial != nullptr);

	const std::vector<GLfloat> texture3D(4 * voxelTextureSize * voxelTextureSize * voxelTextureSize, 0.0f);
	voxelTexture = new Texture3D(texture3D, voxelTextureSize, voxelTextureSize, voxelTextureSize, true);

}

void Graphics::initSparseVoxelization() {

  // Initialize node pool
	m_nodePoolDim = 64;
	m_numLevels = (int)log2f(m_nodePoolDim);
	m_ithVisualizeLevel = m_numLevels - 1;
  int levelVoxels = m_nodePoolDim * m_nodePoolDim * m_nodePoolDim;
  int nLevels = 0;
  int totalVoxels = 0;
  while (levelVoxels)
  {
    totalVoxels += levelVoxels;
    levelVoxels /= 8;
    nLevels++;
  }
  m_maxNodes = totalVoxels;
  for (int i = 0; i < NODE_POOL_NUM_TEXTURES; i++)
  {
    m_nodePoolTextures[i] = std::shared_ptr<TextureBuffer>(new TextureBuffer(totalVoxels * sizeof(int)));
  }
  std::vector<int> initialValues;
  initialValues.resize(MAX_NODE_POOL_LEVELS, 0x3FFFFFFF);
  initialValues[0] = 0;
  initialValues[1] = 1;
  m_levelAddressBuffer = std::shared_ptr<TextureBuffer>(new TextureBuffer(MAX_NODE_POOL_LEVELS * sizeof(int), (char*)&initialValues[0]));

  // Initialize brick pool
  m_brickPoolDim = 70 * 3;
  m_brickPoolTextures[BRICK_POOL_COLOR] = std::shared_ptr<Texture3D>(new Texture3D( m_brickPoolDim, m_brickPoolDim, m_brickPoolDim, false, GL_RGBA8, GL_RGBA));
  m_brickPoolTextures[BRICK_POOL_NORMAL] = std::shared_ptr<Texture3D>(new Texture3D( m_brickPoolDim, m_brickPoolDim, m_brickPoolDim, false, GL_RGBA8, GL_RGBA));
  m_brickPoolTextures[BRICK_POOL_IRRADIANCE] = std::shared_ptr<Texture3D>(new Texture3D( m_brickPoolDim, m_brickPoolDim, m_brickPoolDim, false, GL_RGBA8, GL_RGBA));
  int brickPoolHalfDim = m_brickPoolDim / 2;
  m_brickPoolTextures[BRICK_POOL_COLOR_X] = std::shared_ptr<Texture3D>(new Texture3D( brickPoolHalfDim, brickPoolHalfDim, brickPoolHalfDim, false, GL_RGBA8, GL_RGBA));
  m_brickPoolTextures[BRICK_POOL_COLOR_Y] = std::shared_ptr<Texture3D>(new Texture3D( brickPoolHalfDim, brickPoolHalfDim, brickPoolHalfDim, false, GL_RGBA8, GL_RGBA));
  m_brickPoolTextures[BRICK_POOL_COLOR_Z] = std::shared_ptr<Texture3D>(new Texture3D( brickPoolHalfDim, brickPoolHalfDim, brickPoolHalfDim, false, GL_RGBA8, GL_RGBA));
  m_brickPoolTextures[BRICK_POOL_COLOR_X_NEG] = std::shared_ptr<Texture3D>(new Texture3D( brickPoolHalfDim, brickPoolHalfDim, brickPoolHalfDim, false, GL_RGBA8, GL_RGBA));
  m_brickPoolTextures[BRICK_POOL_COLOR_Y_NEG] = std::shared_ptr<Texture3D>(new Texture3D( brickPoolHalfDim, brickPoolHalfDim, brickPoolHalfDim, false, GL_RGBA8, GL_RGBA));
  m_brickPoolTextures[BRICK_POOL_COLOR_Z_NEG] = std::shared_ptr<Texture3D>(new Texture3D( brickPoolHalfDim, brickPoolHalfDim, brickPoolHalfDim, false, GL_RGBA8, GL_RGBA));

  // Initialize fragment texture
  m_fragmentTextures[FRAG_TEX_COLOR] = std::shared_ptr<Texture3D>(new  Texture3D(m_nodePoolDim, m_nodePoolDim, m_nodePoolDim, false, GL_R32UI, GL_RED_INTEGER));
  m_fragmentTextures[FRAG_TEX_NORMAL] = std::shared_ptr<Texture3D>(new  Texture3D(m_nodePoolDim, m_nodePoolDim, m_nodePoolDim, false, GL_R32UI, GL_RED_INTEGER));

  // Initialize fragment list
  int fragmentListSize = m_nodePoolDim * m_nodePoolDim * m_nodePoolDim * 2 * sizeof(int);
  GLint maxTexBufferSize = 0;
  glGetIntegerv(GL_MAX_TEXTURE_BUFFER_SIZE, &maxTexBufferSize);
  if (maxTexBufferSize < fragmentListSize)
  {
	  fragmentListSize = maxTexBufferSize;
  }
  m_fragmentList = std::shared_ptr<TextureBuffer>(new TextureBuffer(fragmentListSize));

  // Initialize atomic counter
  int counterVal = 0;
  m_nextFreeNode = std::shared_ptr<IndexBuffer>(new IndexBuffer(GL_ATOMIC_COUNTER_BUFFER, sizeof(counterVal), GL_STATIC_DRAW, &counterVal));
  m_nextFreeBrick = std::shared_ptr<IndexBuffer>(new IndexBuffer(GL_ATOMIC_COUNTER_BUFFER, sizeof(counterVal), GL_STATIC_DRAW, &counterVal));
  m_fragmentListCounter = std::shared_ptr<IndexBuffer>(new IndexBuffer(GL_ATOMIC_COUNTER_BUFFER, sizeof(counterVal), GL_STATIC_DRAW, &counterVal));

  // Init light node map
  m_shadowMapRes = 512;
  m_shadowMapRes = std::max(m_nodePoolDim, m_shadowMapRes);
  m_nNodeMapLevels = (int)log2f(m_shadowMapRes) + 1;
  m_nodeMapSizes.resize(m_nNodeMapLevels);
  m_nodeMapOffsets.resize(m_nNodeMapLevels);
  m_lightNodeMap = std::shared_ptr<Texture2D>(new Texture2D(m_shadowMapRes + m_shadowMapRes / 2, m_shadowMapRes, false, GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT));
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, m_lightNodeMap->textureID);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
  glBindTexture(GL_TEXTURE_2D, 0);
  for (int i = m_numLevels-1, res = m_shadowMapRes; i >= 0; i--, res /= 2)
  {
	  m_nodeMapSizes[i] = glm::ivec2(res, res);
  }
  m_nodeMapOffsets[m_numLevels - 1] = glm::ivec2(m_shadowMapRes/2, 0);
  for (int i = m_numLevels - 2, lastPos = m_shadowMapRes; i >= 0; i--)
  {
	  int yPos = lastPos - m_nodeMapSizes[i].x;
	  m_nodeMapOffsets[i] = glm::ivec2(0, yPos);
	  lastPos = yPos;
  }

  // Init shadow map
  m_shadowMapBuffer = std::shared_ptr<FBO>(new FBO(m_shadowMapRes, m_shadowMapRes, GL_NEAREST, GL_NEAREST, GL_RGB32F, GL_FLOAT, GL_CLAMP));

  // Init indirect draw command buffer
  IndirectDrawCommand indirectCommand;
  indirectCommand.baseInstanceIdx = 0;
  indirectCommand.firstVertexIdx = 0;
  indirectCommand.numPrimitives = 1;
  indirectCommand.numVertices = totalVoxels;
  m_nodePoolCmdBuf = std::shared_ptr<IndexBuffer>(new IndexBuffer(GL_DRAW_INDIRECT_BUFFER, sizeof(indirectCommand), GL_STATIC_DRAW, &indirectCommand));
  indirectCommand.numVertices = m_brickPoolDim * m_brickPoolDim * m_brickPoolDim;
  m_brickPoolCmdBuf = std::shared_ptr<IndexBuffer>(new IndexBuffer(GL_DRAW_INDIRECT_BUFFER, sizeof(indirectCommand), GL_STATIC_DRAW, &indirectCommand));
  indirectCommand.numVertices = m_nodePoolDim * m_nodePoolDim * m_nodePoolDim;
  m_fragmentTexCmdBuf = std::shared_ptr<IndexBuffer>(new IndexBuffer(GL_DRAW_INDIRECT_BUFFER, sizeof(indirectCommand), GL_STATIC_DRAW, &indirectCommand));
  indirectCommand.numVertices = 1;
  m_modifyIndirectBufferCmdBuf = std::shared_ptr<IndexBuffer>(new IndexBuffer(GL_DRAW_INDIRECT_BUFFER, sizeof(indirectCommand), GL_STATIC_DRAW, &indirectCommand));
  m_fragmentListCmdBuf = std::shared_ptr<TextureBuffer>(new TextureBuffer(sizeof(indirectCommand), (char*)&indirectCommand));
  m_nodePoolNodesCmdBuf = std::shared_ptr<TextureBuffer>(new TextureBuffer(sizeof(indirectCommand), (char*)&indirectCommand));
  int numVoxelsUpToLevel = 0;
  for (int iLevel = 0; iLevel < MAX_NODE_POOL_LEVELS; ++iLevel)
  {
	  int numVoxelsOnLevel = pow(8U, iLevel);
	  numVoxelsUpToLevel += numVoxelsOnLevel;
	  indirectCommand.numVertices = numVoxelsUpToLevel;
	  m_nodePoolUpToLevelCmdBuf[iLevel] = std::shared_ptr<IndexBuffer>(new IndexBuffer(GL_DRAW_INDIRECT_BUFFER, sizeof(indirectCommand), GL_STATIC_DRAW, &indirectCommand));
	  indirectCommand.numVertices = numVoxelsOnLevel;
	  m_nodePoolOnLevelCmdBuf[iLevel] = std::shared_ptr<IndexBuffer>(new IndexBuffer(GL_DRAW_INDIRECT_BUFFER, sizeof(indirectCommand), GL_STATIC_DRAW, &indirectCommand));
  }
  for (int iLevel = 0; iLevel < m_nNodeMapLevels; ++iLevel)
  {
	  int res = m_nodeMapSizes[iLevel].x;
	  indirectCommand.numVertices = res * res;
	  m_nodeMapOnLevelCmdBuf[iLevel] = std::shared_ptr<IndexBuffer>(new IndexBuffer(GL_DRAW_INDIRECT_BUFFER, sizeof(indirectCommand), GL_STATIC_DRAW, &indirectCommand));
  }
  indirectCommand.numVertices = (m_shadowMapRes + m_shadowMapRes / 2)* m_shadowMapRes;
  m_lightNodeMapCmdBuf = std::shared_ptr<IndexBuffer>(new IndexBuffer(GL_DRAW_INDIRECT_BUFFER, sizeof(indirectCommand), GL_STATIC_DRAW, &indirectCommand));

  // Add shaders
  MaterialStore::ShaderInfo vertInfo, geomInfo, fragInfo;
  MaterialStore::getInstance().AddNewMaterial("clearNodePool", "SparseVoxelOctree\\clearNodePoolVert.shader");
  MaterialStore::getInstance().AddNewMaterial("clearNodePoolNeigh", "SparseVoxelOctree\\clearNodePoolNeighVert.shader");
  MaterialStore::getInstance().AddNewMaterial("clearBrickPool", "SparseVoxelOctree\\clearBrickPoolVert.shader");
  MaterialStore::getInstance().AddNewMaterial("clearFragmentTex", "SparseVoxelOctree\\clearFragmentTexVert.shader");
  MaterialStore::getInstance().AddNewMaterial("voxelize", "SparseVoxelOctree\\VoxelizeVert.shader", "SparseVoxelOctree\\VoxelizeFrag.shader", "SparseVoxelOctree\\VoxelizeGeom.shader");
  MaterialStore::getInstance().AddNewMaterial("modifyIndirectBuffer", "SparseVoxelOctree\\modifyIndirectBufferVert.shader");
  MaterialStore::getInstance().AddNewMaterial("voxelVisualization", "SparseVoxelOctree\\voxelVisualizationVert.shader", "SparseVoxelOctree\\voxelVisualizationFrag.shader","SparseVoxelOctree\\voxelVisualizationGeom.shader");
  MaterialStore::getInstance().AddNewMaterial("flagNode", "SparseVoxelOctree\\flagNodeVert.shader");
  MaterialStore::getInstance().AddNewMaterial("allocateNode", "SparseVoxelOctree\\allocateNodeVert.shader");
  MaterialStore::getInstance().AddNewMaterial("findNeighbours", "SparseVoxelOctree\\findNeighbours.shader");
  MaterialStore::getInstance().AddNewMaterial("allocateBrick", "SparseVoxelOctree\\allocBricks.shader");
  MaterialStore::getInstance().AddNewMaterial("writeLeafs", "SparseVoxelOctree\\WriteLeafs.shader");

  vertInfo = MaterialStore::ShaderInfo("SparseVoxelOctree\\SpreadLeafBricks.shader", "#version 420 core\n#define THREAD_MODE 0\n");
  MaterialStore::getInstance().AddNewMaterial("spreadLeaf", &vertInfo);
  vertInfo = MaterialStore::ShaderInfo("SparseVoxelOctree\\BorderTransfer.shader", "#version 430 core\n#define THREAD_MODE 0\n");
  MaterialStore::getInstance().AddNewMaterial("borderTransfer", &vertInfo);
  vertInfo = MaterialStore::ShaderInfo("SparseVoxelOctree\\MipmapCenter.shader", "#version 430 core\n#define THREAD_MODE 0\n");
  MaterialStore::getInstance().AddNewMaterial("mipmapCenter", &vertInfo);
  vertInfo = MaterialStore::ShaderInfo("SparseVoxelOctree\\MipmapFaces.shader", "#version 430 core\n#define THREAD_MODE 0\n");
  MaterialStore::getInstance().AddNewMaterial("mipmapFaces", &vertInfo);
  vertInfo = MaterialStore::ShaderInfo("SparseVoxelOctree\\MipmapCorners.shader", "#version 430 core\n#define THREAD_MODE 0\n");
  MaterialStore::getInstance().AddNewMaterial("mipmapCorners", &vertInfo);
  vertInfo = MaterialStore::ShaderInfo("SparseVoxelOctree\\MipmapEdges.shader", "#version 430 core\n#define THREAD_MODE 0\n");
  MaterialStore::getInstance().AddNewMaterial("mipmapEdges", &vertInfo);

  // light shaders
  MaterialStore::getInstance().AddNewMaterial("clearNodeMap", "SparseVoxelOctree\\ClearNodeMap.shader");
  MaterialStore::getInstance().AddNewMaterial("lightInjection", "SparseVoxelOctree\\LightInjection.shader");
  MaterialStore::getInstance().AddNewMaterial("shadowMap", "SparseVoxelOctree\\ShadowMapVert.shader", "SparseVoxelOctree\\ShadowMapFrag.shader");

  vertInfo = MaterialStore::ShaderInfo("SparseVoxelOctree\\SpreadLeafBricks.shader", "#version 420 core\n#define THREAD_MODE 1\n");
  MaterialStore::getInstance().AddNewMaterial("spreadLeafLight", &vertInfo);
  vertInfo = MaterialStore::ShaderInfo("SparseVoxelOctree\\BorderTransfer.shader", "#version 430 core\n#define THREAD_MODE 1\n");
  MaterialStore::getInstance().AddNewMaterial("borderTransferLight", &vertInfo);
  vertInfo = MaterialStore::ShaderInfo("SparseVoxelOctree\\MipmapCenter.shader", "#version 430 core\n#define THREAD_MODE 1\n");
  MaterialStore::getInstance().AddNewMaterial("mipmapCenterLight", &vertInfo);
  vertInfo = MaterialStore::ShaderInfo("SparseVoxelOctree\\MipmapFaces.shader", "#version 430 core\n#define THREAD_MODE 1\n");
  MaterialStore::getInstance().AddNewMaterial("mipmapFacesLight", &vertInfo);
  vertInfo = MaterialStore::ShaderInfo("SparseVoxelOctree\\MipmapCorners.shader", "#version 430 core\n#define THREAD_MODE 1\n");
  MaterialStore::getInstance().AddNewMaterial("mipmapCornersLight", &vertInfo);
  vertInfo = MaterialStore::ShaderInfo("SparseVoxelOctree\\MipmapEdges.shader", "#version 430 core\n#define THREAD_MODE 1\n");
  MaterialStore::getInstance().AddNewMaterial("mipmapEdgesLight", &vertInfo);
}

glm::mat4 Graphics::getVoxelTransformInverse(Scene & renderingScene)
{
	renderingScene.getBoundingBox(sceneBoxMin, sceneBoxMax);
	glm::vec3 deltaBox = sceneBoxMax - sceneBoxMin;
	glm::vec3 sumBox = sceneBoxMin + sceneBoxMax;
	glm::vec3 scaleVal(1 / deltaBox.x, 1 / deltaBox.y, 1 / deltaBox.z);
	glm::mat4 voxelTransform(1);
	voxelTransform[0][0] = scaleVal[0];
	voxelTransform[1][1] = scaleVal[1];
	voxelTransform[2][2] = scaleVal[2];
	voxelTransform[3] = glm::vec4(-scaleVal.x*sceneBoxMin.x, -scaleVal.y*sceneBoxMin.y, -scaleVal.z*sceneBoxMin.z, 1.f);
	return voxelTransform;
}

glm::mat4 Graphics::getVoxelTransform(Scene & renderingScene)
{
	renderingScene.getBoundingBox(sceneBoxMin, sceneBoxMax);
	glm::vec3 deltaBox = sceneBoxMax - sceneBoxMin;
	glm::mat4 voxelTransform(1);
	voxelTransform[0][0] = deltaBox[0];
	voxelTransform[1][1] = deltaBox[1];
	voxelTransform[2][2] = deltaBox[2];
	voxelTransform[3] = glm::vec4(sceneBoxMin.x, sceneBoxMin.y, sceneBoxMin.z, 1.f);
	return voxelTransform;
}

void Graphics::sparseVoxelize(Scene & renderingScene, bool clearVoxelization)
{
	// Clear everything
  clearNodePool(renderingScene);
  clearBrickPool(renderingScene, true);
  clearFragmentTex(renderingScene);

  voxelizeScene(renderingScene);

  // write fragment list length to draw buffer
  modifyIndirectBuffer(m_fragmentListCounter, m_fragmentListCmdBuf);

  for (int level = 0; level < m_numLevels-1; level++)
  {
	  if (level != 0)
	  {
		  findNeighbours(renderingScene, level);
	  }
	  // allocate nodes in level+1
	  flagNode(renderingScene);
	  allocateNode(renderingScene, level);
  }

  // write node count to draw buffer
  modifyIndirectBuffer(m_nextFreeNode, m_nodePoolNodesCmdBuf);

  allocateBrick();

  writeLeafNode();

  spreadLeafBrick(m_brickPoolTextures[BRICK_POOL_COLOR]);
  spreadLeafBrick(m_brickPoolTextures[BRICK_POOL_NORMAL]);

  borderTransfer(m_numLevels - 1, m_brickPoolTextures[BRICK_POOL_COLOR]);
  borderTransfer(m_numLevels - 1, m_brickPoolTextures[BRICK_POOL_NORMAL]);

  int ithLevel = m_numLevels - 2;
  for (int ithLevel = m_numLevels - 2; ithLevel >= 0; --ithLevel) {
	  mipmapCenter(ithLevel, m_brickPoolTextures[BRICK_POOL_COLOR]);
	  mipmapFaces(ithLevel, m_brickPoolTextures[BRICK_POOL_COLOR]);
	  mipmapCorners(ithLevel, m_brickPoolTextures[BRICK_POOL_COLOR]);
	  mipmapEdges(ithLevel, m_brickPoolTextures[BRICK_POOL_COLOR]);
	  if (ithLevel > 0)
	  {
		  borderTransfer(ithLevel, m_brickPoolTextures[BRICK_POOL_COLOR]);
	  }
  }
}

void Graphics::lightUpdate(Scene & renderingScene, bool clearVoxelizationFirst)
{
	clearBrickPool(renderingScene, false);
	clearNodeMap();

	lightInjection(renderingScene);

	spreadLeafBrickLight(m_brickPoolTextures[BRICK_POOL_IRRADIANCE]);
	borderTransferLight(m_numLevels-1, m_brickPoolTextures[BRICK_POOL_IRRADIANCE]);

	int ithLevel;
	for (int ithLevel = m_numLevels - 2; ithLevel >= 0; --ithLevel) {
		mipmapCenterLight(ithLevel, m_brickPoolTextures[BRICK_POOL_IRRADIANCE]);
		mipmapFacesLight(ithLevel, m_brickPoolTextures[BRICK_POOL_IRRADIANCE]);
		mipmapCornersLight(ithLevel, m_brickPoolTextures[BRICK_POOL_IRRADIANCE]);
		mipmapEdges(ithLevel, m_brickPoolTextures[BRICK_POOL_IRRADIANCE]);
		if (ithLevel > 0)
		{
			borderTransferLight(ithLevel, m_brickPoolTextures[BRICK_POOL_IRRADIANCE]);
		}
	}
	//spreadLeafBrick(m_brickPoolTextures[BRICK_POOL_IRRADIANCE]);
	//borderTransfer(m_numLevels - 1, m_brickPoolTextures[BRICK_POOL_IRRADIANCE]);

	//int ithLevel = m_numLevels - 2;
	//for (int ithLevel = m_numLevels - 2; ithLevel >= 0; --ithLevel) {
	//	mipmapCenter(ithLevel, m_brickPoolTextures[BRICK_POOL_IRRADIANCE]);
	//	mipmapFaces(ithLevel, m_brickPoolTextures[BRICK_POOL_IRRADIANCE]);
	//	mipmapCorners(ithLevel, m_brickPoolTextures[BRICK_POOL_IRRADIANCE]);
	//	mipmapEdges(ithLevel, m_brickPoolTextures[BRICK_POOL_IRRADIANCE]);
	//	if (ithLevel > 0)
	//	{
	//		borderTransfer(ithLevel, m_brickPoolTextures[BRICK_POOL_IRRADIANCE]);
	//	}
	//}
}

void Graphics::clearNodePool(Scene & renderingScene) {
	glColorMask(false, false, false, false);
	MaterialStore& matStore = MaterialStore::getInstance();
	// Clear node pool
	// Because opengl only supports 8 texture units per draw, at least 2 draws are needed for 9 textures
	std::string nodePoolNames[] = {
		"nodePool_next",
		"nodePool_color",
		"nodePool_normal",
		"nodePool_X",
		"nodePool_X_neg",
		"nodePool_Y",
		"nodePool_Y_neg",
		"nodePool_Z",
		"nodePool_Z_neg",
	};
	Material* clearShader = matStore.findMaterialWithName("clearNodePool");
	glUseProgram(clearShader->program);
	for (int i = 0; i < NODE_POOL_NUM_TEXTURES; i++)
	{
		m_nodePoolTextures[i]->Activate(clearShader->program, nodePoolNames[i], i);
		glBindImageTexture(i, m_nodePoolTextures[i]->m_textureID, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R32UI);
	}
	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_nodePoolCmdBuf->m_bufferID);
	glDrawArraysIndirect(GL_POINTS, 0);
	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

	// Clear node pool neighbour
	clearShader = matStore.findMaterialWithName("clearNodePoolNeigh");
	glUseProgram(clearShader->program);
	for (int i = 0; i < NODE_POOL_NUM_TEXTURES; i++)
	{
		m_nodePoolTextures[i]->Activate(clearShader->program, nodePoolNames[i], i);
		glBindImageTexture(i, m_nodePoolTextures[i]->m_textureID, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R32UI);
	}
	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_nodePoolCmdBuf->m_bufferID);
	glDrawArraysIndirect(GL_POINTS, 0);
	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}

void Graphics::clearBrickPool(Scene & renderingScene, bool isClearAll) {
	// Clear brick pool
	MaterialStore& matStore = MaterialStore::getInstance();
	auto clearShader = matStore.findMaterialWithName("clearBrickPool");
	glUseProgram(clearShader->program);
	std::string brickPoolNames[3] = { "brickPool_color", "brickPool_irradiance","brickPool_normal" };
	int brickPoolIndices[3] = { BRICK_POOL_COLOR, BRICK_POOL_IRRADIANCE, BRICK_POOL_NORMAL };
	for (int i = 0; i < 3; i++)
	{
		int bIdx = brickPoolIndices[i];
		m_brickPoolTextures[bIdx]->Activate(clearShader->program, brickPoolNames[i], i);
		glBindImageTexture(i, m_brickPoolTextures[bIdx]->textureID, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA8);
	}
	glUniform1ui(glGetUniformLocation(clearShader->program, "clearMode"), isClearAll ? 0 : 1);
	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_brickPoolCmdBuf->m_bufferID);
	glDrawArraysIndirect(GL_POINTS, 0);
	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}

void Graphics::clearFragmentTex(Scene & renderingScene) {
	// Clear fragment texture
	MaterialStore& matStore = MaterialStore::getInstance();
	auto clearShader = matStore.findMaterialWithName("clearFragmentTex");
	glUseProgram(clearShader->program);
	std::string fragmentTexNames[2] = { "voxelFragTex_color", "voxelFragTex_normal" };
	int fragmentTexIndices[2] = { FRAG_TEX_COLOR, FRAG_TEX_NORMAL };
	for (int i = 0; i < 2; i++)
	{
		int fIdx = fragmentTexIndices[i];
		m_fragmentTextures[fIdx]->Activate(clearShader->program, fragmentTexNames[i], i);
		glBindImageTexture(i, m_fragmentTextures[fIdx]->textureID, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R32UI);
	}
	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_fragmentTexCmdBuf->m_bufferID);
	glDrawArraysIndirect(GL_POINTS, 0);
	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}

void Graphics::voxelizeScene(Scene & renderingScene) {
	// Voxelize
	MaterialStore& matStore = MaterialStore::getInstance();
	auto voxelizeShader = matStore.findMaterialWithName("voxelize");
	glUseProgram(voxelizeShader->program);

	int textureUnitIdx = 0;
	std::string fragmentTexNames[2] = { "voxelFragTex_color", "voxelFragTex_normal" };
	int fragmentTexIndices[2] = { FRAG_TEX_COLOR, FRAG_TEX_NORMAL };
	for (textureUnitIdx = 0; textureUnitIdx < 2; textureUnitIdx++)
	{
		int fIdx = fragmentTexIndices[textureUnitIdx];
		m_fragmentTextures[fIdx]->Activate(voxelizeShader->program, fragmentTexNames[textureUnitIdx], textureUnitIdx);
		glBindImageTexture(textureUnitIdx, m_fragmentTextures[fIdx]->textureID, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);
	}
	m_fragmentList->Activate(voxelizeShader->program, "voxelFragList_position", textureUnitIdx);
	glBindImageTexture(textureUnitIdx, m_fragmentList->m_textureID, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R32UI);

	glm::mat4 viewMatrix = glm::mat4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1);
	glUniformMatrix4fv(glGetUniformLocation(voxelizeShader->program, "V"), 1, GL_FALSE, glm::value_ptr(viewMatrix));
	glm::mat4 viewMats[3];
	{
		// View Matrix for right camera
		viewMats[0][0] = glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);
		viewMats[0][1] = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);
		viewMats[0][2] = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
		viewMats[0][3] = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
		// View Matrix for top camera
		viewMats[1][0] = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
		viewMats[1][1] = glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);
		viewMats[1][2] = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);
		viewMats[1][3] = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
		// View Matrix for far camera
		viewMats[2][0] = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
		viewMats[2][1] = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);
		viewMats[2][2] = glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);
		viewMats[2][3] = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
	}
	glUniformMatrix4fv(glGetUniformLocation(voxelizeShader->program, "viewProjs[0]"), 3, GL_FALSE, glm::value_ptr(viewMats[0]));

	glm::mat4 voxelGridTransformI = getVoxelTransformInverse(renderingScene);
	glUniformMatrix4fv(glGetUniformLocation(voxelizeShader->program, "voxelGridTransformI"), 1, GL_FALSE, glm::value_ptr(voxelGridTransformI));

	glUniform1ui(glGetUniformLocation(voxelizeShader->program, "voxelTexSize"), m_nodePoolDim);

	// Bind atomic variable and set its value
	int bindingPoint = 0;
	glGetActiveAtomicCounterBufferiv(voxelizeShader->program, 0, GL_ATOMIC_COUNTER_BUFFER_BINDING, &bindingPoint);
	glBindBufferBase(GL_ATOMIC_COUNTER_BUFFER, bindingPoint, m_fragmentListCounter->m_bufferID);
	GLuint *ptr = (GLuint *)glMapBufferRange(GL_ATOMIC_COUNTER_BUFFER, 0, sizeof(GLuint), GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
	ptr[0] = 0;
	glUnmapBuffer(GL_ATOMIC_COUNTER_BUFFER);

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(0, 0, m_nodePoolDim, m_nodePoolDim);
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
	glDisable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	//uploadLighting(renderingScene, voxelizeShader->program);
	renderQueue(renderingScene.renderers, voxelizeShader->program, true);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_ATOMIC_COUNTER_BARRIER_BIT);
}

void Graphics::modifyIndirectBuffer(std::shared_ptr<IndexBuffer> valueBuffer, std::shared_ptr<TextureBuffer> commandBuffer) {
	MaterialStore& matStore = MaterialStore::getInstance();
	auto modifyIndirectDrawShader = matStore.findMaterialWithName("modifyIndirectBuffer");
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
	glUseProgram(modifyIndirectDrawShader->program);

	// bind texture buffer which will be modified by shader
	commandBuffer->Activate(modifyIndirectDrawShader->program, "indirectCommandBuf", 0);
	glBindImageTexture(0, commandBuffer->m_textureID, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R32UI);

	// bind atomic variable, which will be used to update the command buffer
	int bindingPoint = 0;
	glGetActiveAtomicCounterBufferiv(modifyIndirectDrawShader->program, 0, GL_ATOMIC_COUNTER_BUFFER_BINDING, &bindingPoint);
	glBindBufferBase(GL_ATOMIC_COUNTER_BUFFER, bindingPoint, valueBuffer->m_bufferID);

	// bind indirect draw buffer and draw
	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_modifyIndirectBufferCmdBuf->m_bufferID);
	glDrawArraysIndirect(GL_POINTS, 0);
}

void Graphics::visualizeVoxel(Scene& renderingScene, unsigned int viewportWidth, unsigned int viewportHeight, int level)
{
	MaterialStore& matStore = MaterialStore::getInstance();
	auto & camera = *renderingScene.renderingCamera;
	const Material * material = matStore.findMaterialWithName("voxelVisualization");;
	const GLuint program = material->program;

	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glMemoryBarrier(GL_ALL_BARRIER_BITS);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glUseProgram(program);

	// GL Settings.
	{
		glViewport(0, 0, viewportWidth, viewportHeight);
		glClearColor(0.0f, 0.0f, 0.0f, 1.0);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glDisable(GL_DEPTH_TEST);
		glDisable(GL_CULL_FACE);
		glCullFace(GL_BACK);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE);
		glBlendColor(1.0, 1.0, 1.0, 2.0);
	}

	// Upload uniforms.
	uploadCamera(camera, program);
	uploadGlobalConstants(program, viewportWidth, viewportHeight);
	uploadLighting(renderingScene, program);
	uploadRenderingSettings(program);

	glUniform1ui(glGetUniformLocation(material->program, "numLevels"), m_numLevels);
	glUniform1ui(glGetUniformLocation(material->program, "level"), level);
	glUniform1ui(glGetUniformLocation(material->program, "levelG"), level);
	glUniform1ui(glGetUniformLocation(material->program, "voxelTexSize"), m_nodePoolDim);
	glm::mat4 voxelGridTransform = getVoxelTransform(renderingScene);
	glUniformMatrix4fv(glGetUniformLocation(material->program, "voxelGridTransform"), 1, GL_FALSE, glm::value_ptr(voxelGridTransform));
	glUniformMatrix4fv(glGetUniformLocation(material->program, "voxelGridTransformG"), 1, GL_FALSE, glm::value_ptr(voxelGridTransform));

	int textureUnitIdx = 0;
	m_levelAddressBuffer->Activate(material->program, "levelAddressBuffer", textureUnitIdx);
	glBindImageTexture(textureUnitIdx, m_levelAddressBuffer->m_textureID, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R32UI);
	textureUnitIdx++;
	m_fragmentList->Activate(material->program, "voxelFragList_position", textureUnitIdx);
	glBindImageTexture(textureUnitIdx, m_fragmentList->m_textureID, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R32UI);
	textureUnitIdx++;
	m_fragmentTextures[FRAG_TEX_COLOR]->Activate(material->program, "voxelFragTex_color", textureUnitIdx);
	glBindImageTexture(textureUnitIdx, m_fragmentTextures[FRAG_TEX_COLOR]->textureID, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R32UI);
	textureUnitIdx++;
	m_nodePoolTextures[NODE_POOL_NEXT]->Activate(material->program, "nodePool_next", textureUnitIdx);
	glBindImageTexture(textureUnitIdx, m_nodePoolTextures[NODE_POOL_NEXT]->m_textureID, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R32UI);
	textureUnitIdx++;
	m_nodePoolTextures[NODE_POOL_COLOR]->Activate(material->program, "nodePool_color", textureUnitIdx);
	glBindImageTexture(textureUnitIdx, m_nodePoolTextures[NODE_POOL_COLOR]->m_textureID, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R32UI);
	textureUnitIdx++;
	//m_brickPoolTextures[BRICK_POOL_COLOR]->Activate(material->program, "brickPool_color", textureUnitIdx);
	//glBindImageTexture(textureUnitIdx, m_brickPoolTextures[BRICK_POOL_COLOR]->textureID, 0, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA8);
	m_brickPoolTextures[BRICK_POOL_IRRADIANCE]->Activate(material->program, "brickPool_color", textureUnitIdx);
	glBindImageTexture(textureUnitIdx, m_brickPoolTextures[BRICK_POOL_IRRADIANCE]->textureID, 0, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA8);

	// Render.
	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_fragmentListCmdBuf->m_bufferID);
	glDrawArraysIndirect(GL_POINTS, 0);
	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}

void Graphics::flagNode(Scene & renderingScene) {
	MaterialStore& matStore = MaterialStore::getInstance();
	const Material * material = matStore.findMaterialWithName("flagNode");

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glUseProgram(material->program);
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);

	int textureUnitIdx = 0;
	m_fragmentList->Activate(material->program, "voxelFragmentListPosition", textureUnitIdx);
	glBindImageTexture(textureUnitIdx, m_fragmentList->m_textureID, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R32UI);

	textureUnitIdx++;
	m_nodePoolTextures[NODE_POOL_NEXT]->Activate(material->program, "nodePool_next", textureUnitIdx);
	glBindImageTexture(textureUnitIdx, m_nodePoolTextures[NODE_POOL_NEXT]->m_textureID, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R32UI);

	glUniform1ui(glGetUniformLocation(material->program, "voxelGridResolution"), m_nodePoolDim);

	glUniform1ui(glGetUniformLocation(material->program, "numLevels"), m_numLevels);

	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_fragmentListCmdBuf->m_bufferID);
	glDrawArraysIndirect(GL_POINTS, 0);
	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}

void Graphics::allocateNode(Scene & renderingScene, int level) {
	MaterialStore& matStore = MaterialStore::getInstance();
	const Material * material = matStore.findMaterialWithName("allocateNode");

	glUseProgram(material->program);
	glMemoryBarrier(GL_ALL_BARRIER_BITS);
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);

	glUniform1ui(glGetUniformLocation(material->program, "level"), level);

	// Clear level address buffer first
	if (level == 0)
	{
		glBindBuffer(GL_TEXTURE_BUFFER, m_levelAddressBuffer->m_bufferID);
		GLuint *ptr = (GLuint *)glMapBufferRange(GL_TEXTURE_BUFFER, 0, sizeof(GLuint) * MAX_NODE_POOL_LEVELS, GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
		for (int i = 0; i < MAX_NODE_POOL_LEVELS; i++)
		{
			ptr[i] = 0x3FFFFFFF;
		}
		ptr[0] = 0;
		ptr[1] = 1;
		glUnmapBuffer(GL_TEXTURE_BUFFER);
	}

	int textureUnitIdx = 0;
	m_levelAddressBuffer->Activate(material->program, "levelAddressBuffer", textureUnitIdx);
	glBindImageTexture(textureUnitIdx, m_levelAddressBuffer->m_textureID, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);
	textureUnitIdx++;
	m_nodePoolTextures[NODE_POOL_NEXT]->Activate(material->program, "nodePool_next", textureUnitIdx);
	glBindImageTexture(textureUnitIdx, m_nodePoolTextures[NODE_POOL_NEXT]->m_textureID, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R32UI);

	// Bind atomic variable and set its value
	int bindingPoint = 0;
	glGetActiveAtomicCounterBufferiv(material->program, 0, GL_ATOMIC_COUNTER_BUFFER_BINDING, &bindingPoint);
	glBindBufferBase(GL_ATOMIC_COUNTER_BUFFER, bindingPoint, m_nextFreeNode->m_bufferID);
	if (level == 0)
	{
		GLuint *ptr = (GLuint *)glMapBufferRange(GL_ATOMIC_COUNTER_BUFFER, 0, sizeof(GLuint), GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
		ptr[0] = 0;
		glUnmapBuffer(GL_ATOMIC_COUNTER_BUFFER);
	}

	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_nodePoolUpToLevelCmdBuf[level]->m_bufferID);
	glDrawArraysIndirect(GL_POINTS, 0);
	glMemoryBarrier(GL_ALL_BARRIER_BITS);
}

void Graphics::findNeighbours(Scene & renderingScene, int level) {
	const Material * material = MaterialStore::getInstance().findMaterialWithName("findNeighbours");

	glUseProgram(material->program);
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
	glUniform1ui(glGetUniformLocation(material->program, "level"), level);

	int textureUnitIdx = 0;
	m_fragmentList->Activate(material->program, "voxelFragmentListPosition", textureUnitIdx);
	glBindImageTexture(textureUnitIdx, m_fragmentList->m_textureID, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R32UI);
	textureUnitIdx++;
	m_nodePoolTextures[NODE_POOL_NEXT]->Activate(material->program, "nodePool_next", textureUnitIdx);
	glBindImageTexture(textureUnitIdx, m_nodePoolTextures[NODE_POOL_NEXT]->m_textureID, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R32UI);

	int nodePoolIndices[] = {
		NODE_POOL_NEIGH_X,		NODE_POOL_NEIGH_X_NEG,
		NODE_POOL_NEIGH_Y,		NODE_POOL_NEIGH_Y_NEG,
		NODE_POOL_NEIGH_Z,		NODE_POOL_NEIGH_Z_NEG,
	};
	std::string shaderVars[] = {
		"nodePool_X",		"nodePool_X_neg",
		"nodePool_Y",		"nodePool_Y_neg",
		"nodePool_Z",		"nodePool_Z_neg",
	};
	for (int i = 0; i < 6; i++)
	{
		textureUnitIdx++;
		int nodePoolTexID = nodePoolIndices[i];
		std::string& shaderVarName = shaderVars[i];
		m_nodePoolTextures[nodePoolTexID]->Activate(material->program, shaderVarName, textureUnitIdx);
		glBindImageTexture(textureUnitIdx, m_nodePoolTextures[nodePoolTexID]->m_textureID, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);
	}
	glUniform1ui(glGetUniformLocation(material->program, "numLevels"), m_numLevels);
	glUniform1ui(glGetUniformLocation(material->program, "voxelGridResolution"), m_nodePoolDim);

	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_fragmentListCmdBuf->m_bufferID);
	glDrawArraysIndirect(GL_POINTS, 0);
	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}

void Graphics::allocateBrick() {
	MaterialStore& matStore = MaterialStore::getInstance();
	const Material * material = matStore.findMaterialWithName("allocateBrick");

	glUseProgram(material->program); 
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
	glUniform1ui(glGetUniformLocation(material->program, "brickPoolResolution"), m_brickPoolDim);

	int textureUnitIdx = 0;
	m_nodePoolTextures[NODE_POOL_NEXT]->Activate(material->program, "nodePool_next", textureUnitIdx);
	glBindImageTexture(textureUnitIdx, m_nodePoolTextures[NODE_POOL_NEXT]->m_textureID, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R32UI);
	textureUnitIdx++;
	m_nodePoolTextures[NODE_POOL_COLOR]->Activate(material->program, "nodePool_color", textureUnitIdx);
	glBindImageTexture(textureUnitIdx, m_nodePoolTextures[NODE_POOL_COLOR]->m_textureID, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R32UI);

	// bind atomic counter
	int bindingPoint = 0;
	glGetActiveAtomicCounterBufferiv(material->program, 0, GL_ATOMIC_COUNTER_BUFFER_BINDING, &bindingPoint);
	glBindBufferBase(GL_ATOMIC_COUNTER_BUFFER, bindingPoint, m_nextFreeBrick->m_bufferID);
	GLuint *ptr = (GLuint *)glMapBufferRange(GL_ATOMIC_COUNTER_BUFFER, 0, sizeof(GLuint), GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
	ptr[0] = 1;
	glUnmapBuffer(GL_ATOMIC_COUNTER_BUFFER);

	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_nodePoolNodesCmdBuf->m_bufferID);
	glDrawArraysIndirect(GL_POINTS, 0);
	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}

void Graphics::writeLeafNode() {
	// Write original values to brick's cornal voxels 
	MaterialStore& matStore = MaterialStore::getInstance();
	const Material * material = matStore.findMaterialWithName("writeLeafs");

	glUseProgram(material->program);
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);

	glUniform1ui(glGetUniformLocation(material->program, "numLevels"), m_numLevels);

	int textureUnitIdx = 0;
	std::string fragmentTexNames[2] = { "voxelFragTex_color", "voxelFragTex_normal" };
	int fragmentTexIndices[2] = { FRAG_TEX_COLOR, FRAG_TEX_NORMAL };
	for (textureUnitIdx = 0; textureUnitIdx < 2; textureUnitIdx++)
	{
		int fIdx = fragmentTexIndices[textureUnitIdx];
		m_fragmentTextures[fIdx]->Activate(material->program, fragmentTexNames[textureUnitIdx], textureUnitIdx);
		glBindImageTexture(textureUnitIdx, m_fragmentTextures[fIdx]->textureID, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R32UI);
	}
	m_fragmentList->Activate(material->program, "voxelFragList_position", textureUnitIdx);
	glBindImageTexture(textureUnitIdx, m_fragmentList->m_textureID, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R32UI);
	textureUnitIdx++;
	m_nodePoolTextures[NODE_POOL_NEXT]->Activate(material->program, "nodePool_next", textureUnitIdx);
	glBindImageTexture(textureUnitIdx, m_nodePoolTextures[NODE_POOL_NEXT]->m_textureID, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R32UI);
	textureUnitIdx++;
	m_nodePoolTextures[NODE_POOL_COLOR]->Activate(material->program, "nodePool_color", textureUnitIdx);
	glBindImageTexture(textureUnitIdx, m_nodePoolTextures[NODE_POOL_COLOR]->m_textureID, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R32UI);
	textureUnitIdx++;
	std::string brickPoolNames[3] = { "brickPool_color", "brickPool_irradiance","brickPool_normal" };
	int brickPoolIndices[3] = { BRICK_POOL_COLOR, BRICK_POOL_IRRADIANCE, BRICK_POOL_NORMAL };
	for (int i = 0; i < 3; i++, textureUnitIdx++)
	{
		int bIdx = brickPoolIndices[i];
		m_brickPoolTextures[bIdx]->Activate(material->program, brickPoolNames[i], textureUnitIdx);
		glBindImageTexture(textureUnitIdx, m_brickPoolTextures[bIdx]->textureID, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA8);
	}
	glUniform1ui(glGetUniformLocation(material->program, "voxelGridResolution"), m_nodePoolDim);

	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_fragmentListCmdBuf->m_bufferID);
	glDrawArraysIndirect(GL_POINTS, 0);
	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}

void Graphics::spreadLeafBrick(std::shared_ptr<Texture3D> brickPoolTexture) {
	// Interpolate values in corner voxels and store the results into remaining voxels
	MaterialStore& matStore = MaterialStore::getInstance();
	const Material * material = matStore.findMaterialWithName("spreadLeaf");

	glUseProgram(material->program);
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_nodePoolOnLevelCmdBuf[m_numLevels]->m_bufferID);

	glUniform1ui(glGetUniformLocation(material->program, "numLevels"), m_numLevels);
	glUniform1ui(glGetUniformLocation(material->program, "level"), m_numLevels-1);

	int textureUnitIdx = 0;
	m_levelAddressBuffer->Activate(material->program, "levelAddressBuffer", textureUnitIdx);
	glBindImageTexture(textureUnitIdx, m_levelAddressBuffer->m_textureID, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R32UI);
	textureUnitIdx++;
	m_nodePoolTextures[NODE_POOL_COLOR]->Activate(material->program, "nodePool_color", textureUnitIdx);
	glBindImageTexture(textureUnitIdx, m_nodePoolTextures[NODE_POOL_COLOR]->m_textureID, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R32UI);
	textureUnitIdx++;
	brickPoolTexture->Activate(material->program, "brickPool_value", textureUnitIdx);
	glBindImageTexture(textureUnitIdx, brickPoolTexture->textureID, 0, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA8);
	
	glDrawArraysIndirect(GL_POINTS, 0);
	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}

void Graphics::borderTransfer(int level, std::shared_ptr<Texture3D> brickPoolTexture) {
	MaterialStore& matStore = MaterialStore::getInstance();
	const Material * material = matStore.findMaterialWithName("borderTransfer");

	glUseProgram(material->program);
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_nodePoolOnLevelCmdBuf[level]->m_bufferID);
	glUniform1ui(glGetUniformLocation(material->program, "numLevels"), m_numLevels);
	glUniform1ui(glGetUniformLocation(material->program, "level"), level);

	int textureUnitIdx = 0;
	m_levelAddressBuffer->Activate(material->program, "levelAddressBuffer", textureUnitIdx);
	glBindImageTexture(textureUnitIdx, m_levelAddressBuffer->m_textureID, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);
	textureUnitIdx++;
	m_nodePoolTextures[NODE_POOL_COLOR]->Activate(material->program, "nodePool_color", textureUnitIdx);
	glBindImageTexture(textureUnitIdx, m_nodePoolTextures[NODE_POOL_COLOR]->m_textureID, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);
	textureUnitIdx++;
	brickPoolTexture->Activate(material->program, "brickPool_value", textureUnitIdx);
	glBindImageTexture(textureUnitIdx, brickPoolTexture->textureID, 0, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA8);


	textureUnitIdx++;
	glUniform1ui(glGetUniformLocation(material->program, "axis"),0);
	m_nodePoolTextures[NODE_POOL_NEIGH_X]->Activate(material->program, "nodePool_Neighbour", textureUnitIdx);
	glBindImageTexture(textureUnitIdx, m_nodePoolTextures[NODE_POOL_NEIGH_X]->m_textureID, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);
	glDrawArraysIndirect(GL_POINTS, 0);
	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

	glUniform1ui(glGetUniformLocation(material->program, "axis"), 1);
	m_nodePoolTextures[NODE_POOL_NEIGH_Y]->Activate(material->program, "nodePool_Neighbour", textureUnitIdx);
	glBindImageTexture(textureUnitIdx, m_nodePoolTextures[NODE_POOL_NEIGH_Y]->m_textureID, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);
	glDrawArraysIndirect(GL_POINTS, 0);
	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

	glUniform1ui(glGetUniformLocation(material->program, "axis"), 2);
	m_nodePoolTextures[NODE_POOL_NEIGH_Z]->Activate(material->program, "nodePool_Neighbour", textureUnitIdx);
	glBindImageTexture(textureUnitIdx, m_nodePoolTextures[NODE_POOL_NEIGH_Z]->m_textureID, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);
	glDrawArraysIndirect(GL_POINTS, 0);
	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}

void Graphics::mipmapCenter(int level, std::shared_ptr<Texture3D> brickPoolTexture) {
	const Material * material = MaterialStore::getInstance().findMaterialWithName("mipmapCenter");

	glUseProgram(material->program);
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);

	glUniform1ui(glGetUniformLocation(material->program, "numLevels"), m_numLevels);
	glUniform1ui(glGetUniformLocation(material->program, "level"), level);

	int textureUnitIdx = 0;
	m_levelAddressBuffer->Activate(material->program, "levelAddressBuffer", textureUnitIdx);
	glBindImageTexture(textureUnitIdx, m_levelAddressBuffer->m_textureID, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);
	textureUnitIdx++;
	brickPoolTexture->Activate(material->program, "brickPool_value", textureUnitIdx);
	glBindImageTexture(textureUnitIdx, brickPoolTexture->textureID, 0, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA8);
	textureUnitIdx++;
	m_nodePoolTextures[NODE_POOL_NEXT]->Activate(material->program, "nodePool_next", textureUnitIdx);
	glBindImageTexture(textureUnitIdx, m_nodePoolTextures[NODE_POOL_NEXT]->m_textureID, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);
	textureUnitIdx++;
	m_nodePoolTextures[NODE_POOL_COLOR]->Activate(material->program, "nodePool_color", textureUnitIdx);
	glBindImageTexture(textureUnitIdx, m_nodePoolTextures[NODE_POOL_COLOR]->m_textureID, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);

	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_nodePoolOnLevelCmdBuf[level]->m_bufferID);
	glDrawArraysIndirect(GL_POINTS, 0);
	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}

void Graphics::mipmapFaces(int level, std::shared_ptr<Texture3D> brickPoolTexture) {
	const Material * material = MaterialStore::getInstance().findMaterialWithName("mipmapFaces");
	glUseProgram(material->program);
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);

	glUniform1ui(glGetUniformLocation(material->program, "numLevels"), m_numLevels);
	glUniform1ui(glGetUniformLocation(material->program, "level"), level);

	int textureUnitIdx = 0;
	m_levelAddressBuffer->Activate(material->program, "levelAddressBuffer", textureUnitIdx);
	glBindImageTexture(textureUnitIdx, m_levelAddressBuffer->m_textureID, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);
	textureUnitIdx++;
	brickPoolTexture->Activate(material->program, "brickPool_value", textureUnitIdx);
	glBindImageTexture(textureUnitIdx, brickPoolTexture->textureID, 0, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA8);
	textureUnitIdx++;
	m_nodePoolTextures[NODE_POOL_NEXT]->Activate(material->program, "nodePool_next", textureUnitIdx);
	glBindImageTexture(textureUnitIdx, m_nodePoolTextures[NODE_POOL_NEXT]->m_textureID, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);
	textureUnitIdx++;
	m_nodePoolTextures[NODE_POOL_COLOR]->Activate(material->program, "nodePool_color", textureUnitIdx);
	glBindImageTexture(textureUnitIdx, m_nodePoolTextures[NODE_POOL_COLOR]->m_textureID, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);

	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_nodePoolOnLevelCmdBuf[level]->m_bufferID);
	glDrawArraysIndirect(GL_POINTS, 0);
	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}

void Graphics::mipmapCorners(int level, std::shared_ptr<Texture3D> brickPoolTexture) {
	const Material * material = MaterialStore::getInstance().findMaterialWithName("mipmapCorners");
	glUseProgram(material->program);
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);

	glUniform1ui(glGetUniformLocation(material->program, "numLevels"), m_numLevels);
	glUniform1ui(glGetUniformLocation(material->program, "level"), level);

	int textureUnitIdx = 0;
	m_levelAddressBuffer->Activate(material->program, "levelAddressBuffer", textureUnitIdx);
	glBindImageTexture(textureUnitIdx, m_levelAddressBuffer->m_textureID, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);
	textureUnitIdx++;
	brickPoolTexture->Activate(material->program, "brickPool_value", textureUnitIdx);
	glBindImageTexture(textureUnitIdx, brickPoolTexture->textureID, 0, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA8);
	textureUnitIdx++;
	m_nodePoolTextures[NODE_POOL_NEXT]->Activate(material->program, "nodePool_next", textureUnitIdx);
	glBindImageTexture(textureUnitIdx, m_nodePoolTextures[NODE_POOL_NEXT]->m_textureID, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);
	textureUnitIdx++;
	m_nodePoolTextures[NODE_POOL_COLOR]->Activate(material->program, "nodePool_color", textureUnitIdx);
	glBindImageTexture(textureUnitIdx, m_nodePoolTextures[NODE_POOL_COLOR]->m_textureID, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);

	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_nodePoolOnLevelCmdBuf[level]->m_bufferID);
	glDrawArraysIndirect(GL_POINTS, 0);
	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}

void Graphics::mipmapEdges(int level, std::shared_ptr<Texture3D> brickPoolTexture) {
	const Material * material = MaterialStore::getInstance().findMaterialWithName("mipmapEdges");
	glUseProgram(material->program);
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);

	glUniform1ui(glGetUniformLocation(material->program, "numLevels"), m_numLevels);
	glUniform1ui(glGetUniformLocation(material->program, "level"), level);

	int textureUnitIdx = 0;
	m_levelAddressBuffer->Activate(material->program, "levelAddressBuffer", textureUnitIdx);
	glBindImageTexture(textureUnitIdx, m_levelAddressBuffer->m_textureID, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);
	textureUnitIdx++;
	brickPoolTexture->Activate(material->program, "brickPool_value", textureUnitIdx);
	glBindImageTexture(textureUnitIdx, brickPoolTexture->textureID, 0, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA8);
	textureUnitIdx++;
	m_nodePoolTextures[NODE_POOL_NEXT]->Activate(material->program, "nodePool_next", textureUnitIdx);
	glBindImageTexture(textureUnitIdx, m_nodePoolTextures[NODE_POOL_NEXT]->m_textureID, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);
	textureUnitIdx++;
	m_nodePoolTextures[NODE_POOL_COLOR]->Activate(material->program, "nodePool_color", textureUnitIdx);
	glBindImageTexture(textureUnitIdx, m_nodePoolTextures[NODE_POOL_COLOR]->m_textureID, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);

	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_nodePoolOnLevelCmdBuf[level]->m_bufferID);
	glDrawArraysIndirect(GL_POINTS, 0);
	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}

void Graphics::clearNodeMap()
{
	const Material * material = MaterialStore::getInstance().findMaterialWithName("clearNodeMap");
	glUseProgram(material->program);
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);

	int textureUnitIdx = 0;
	m_lightNodeMap->Activate(material->program, textureUnitIdx, "nodeMap");
	glBindImageTexture(textureUnitIdx, m_lightNodeMap->textureID, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);

	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_lightNodeMapCmdBuf->m_bufferID);
	glDrawArraysIndirect(GL_POINTS, 0);
}

void Graphics::shadowMap(Scene & renderingScene) {
	const Material * material = MaterialStore::getInstance().findMaterialWithName("shadowMap");
	glUseProgram(material->program);
	glBindFramebuffer(GL_FRAMEBUFFER, m_shadowMapBuffer->frameBuffer);

	glEnable(GL_DEPTH_TEST);
	glViewport(0, 0, m_shadowMapRes, m_shadowMapRes);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glClearColor(0, 0, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glDisable(GL_CULL_FACE);

	// Set matrix
	//m_lightPos = renderingScene.renderingCamera->position;// glm::vec3(0, 0.5, 1);
	//m_lightDir = renderingScene.renderingCamera->forward();// glm::vec3(0, -1, -1);
	//m_lightViewMat = renderingScene.renderingCamera->viewMatrix;// glm::lookAt(m_lightPos, m_lightPos + m_lightDir, glm::vec3(0, 1, 0));
	//m_lightProjMat = renderingScene.renderingCamera->getProjectionMatrix(); //glm::ortho(-1, 1, -1, 1, -1, 1);

	m_lightPos = glm::vec3(0, 0.0, 1);
	m_lightDir = glm::vec3(0, -1, -1);
	m_lightViewMat = glm::lookAt(m_lightPos, m_lightPos + m_lightDir, glm::vec3(0, 1, 0));
	m_lightProjMat = glm::ortho(-0.9, 0.9, -0.9, 0.9, 0.0, 2.0); // renderingScene.renderingCamera->getProjectionMatrix(); //glm::ortho(-1, 1, -1, 1, -1, 1);

	glUniformMatrix4fv(glGetUniformLocation(material->program, "V"), 1, GL_FALSE, glm::value_ptr(m_lightViewMat));
	glUniformMatrix4fv(glGetUniformLocation(material->program, "P"), 1, GL_FALSE, glm::value_ptr(m_lightProjMat));
	//uploadCamera(*renderingScene.renderingCamera, material->program);

	renderQueue(renderingScene.renderers, material->program, true);
	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}

void Graphics::lightInjection(Scene& renderingScene) {
	const Material * material = MaterialStore::getInstance().findMaterialWithName("lightInjection");
	glUseProgram(material->program);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

	// TODO: add shadow map
	glm::mat4 voxelGridTransformI = getVoxelTransformInverse(renderingScene);
	glUniformMatrix4fv(glGetUniformLocation(material->program, "voxelGridTransformI"), 1, GL_FALSE, glm::value_ptr(voxelGridTransformI));
	glUniform2iv(glGetUniformLocation(material->program, "nodeMapOffset[0]"), m_nodeMapOffsets.size(), glm::value_ptr(m_nodeMapOffsets[0]));
	glUniform2iv(glGetUniformLocation(material->program, "nodeMapSize[0]"), m_nodeMapSizes.size(), glm::value_ptr(m_nodeMapSizes[0]));

	glm::vec3 lightColor(1,1,1);
	glUniform1ui(glGetUniformLocation(material->program, "numLevels"), m_numLevels);
	glUniform3f(glGetUniformLocation(material->program, "lightColor"), lightColor.r, lightColor.g, lightColor.b);
	glUniform3f(glGetUniformLocation(material->program, "lightDir"), m_lightDir.r, m_lightDir.g, m_lightDir.b);

	int textureUnitIdx = 0;
	m_shadowMapBuffer->ActivateAsTexture(material->program, "smPosition", textureUnitIdx);
	textureUnitIdx++;
	m_nodePoolTextures[NODE_POOL_NEXT]->Activate(material->program, "nodePool_next", textureUnitIdx);
	glBindImageTexture(textureUnitIdx, m_nodePoolTextures[NODE_POOL_NEXT]->m_textureID, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R32UI);
	textureUnitIdx++;
	m_nodePoolTextures[NODE_POOL_COLOR]->Activate(material->program, "nodePool_color", textureUnitIdx);
	glBindImageTexture(textureUnitIdx, m_nodePoolTextures[NODE_POOL_COLOR]->m_textureID, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R32UI);
	textureUnitIdx++;

	std::string brickPoolNames[3] = { "brickPool_color", "brickPool_irradiance","brickPool_normal" };
	int brickPoolIndices[3] = { BRICK_POOL_COLOR, BRICK_POOL_IRRADIANCE, BRICK_POOL_NORMAL };
	for (int i = 0; i < 2; i++, textureUnitIdx++)
	{
		int bIdx = brickPoolIndices[i];
		m_brickPoolTextures[bIdx]->Activate(material->program, brickPoolNames[i], textureUnitIdx);
		glBindImageTexture(textureUnitIdx, m_brickPoolTextures[bIdx]->textureID, 0, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA8);
	}

	m_lightNodeMap->Activate(material->program, textureUnitIdx, "nodeMap");
	glBindImageTexture(textureUnitIdx, m_lightNodeMap->textureID, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);

	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_nodeMapOnLevelCmdBuf[m_numLevels-1]->m_bufferID);
	glDrawArraysIndirect(GL_POINTS, 0);
	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}

void Graphics::spreadLeafBrickLight(std::shared_ptr<Texture3D> brickPoolTexture) {
	// Interpolate values in corner voxels and store the results into remaining voxels
	MaterialStore& matStore = MaterialStore::getInstance();
	const Material * material = matStore.findMaterialWithName("spreadLeafLight");

	glUseProgram(material->program);
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_nodeMapOnLevelCmdBuf[m_numLevels - 1]->m_bufferID);

	glUniform1ui(glGetUniformLocation(material->program, "numLevels"), m_numLevels);
	glUniform1ui(glGetUniformLocation(material->program, "level"), m_numLevels - 1);
	glUniform2iv(glGetUniformLocation(material->program, "nodeMapOffset[0]"), m_nodeMapOffsets.size(), glm::value_ptr(m_nodeMapOffsets[0]));
	glUniform2iv(glGetUniformLocation(material->program, "nodeMapSize[0]"), m_nodeMapSizes.size(), glm::value_ptr(m_nodeMapSizes[0]));

	int textureUnitIdx = 0;
	m_lightNodeMap->Activate(material->program, textureUnitIdx, "nodeMap");
	glBindImageTexture(textureUnitIdx, m_lightNodeMap->textureID, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);
	textureUnitIdx++;
	m_nodePoolTextures[NODE_POOL_COLOR]->Activate(material->program, "nodePool_color", textureUnitIdx);
	glBindImageTexture(textureUnitIdx, m_nodePoolTextures[NODE_POOL_COLOR]->m_textureID, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R32UI);
	textureUnitIdx++;
	brickPoolTexture->Activate(material->program, "brickPool_value", textureUnitIdx);
	glBindImageTexture(textureUnitIdx, brickPoolTexture->textureID, 0, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA8);

	glDrawArraysIndirect(GL_POINTS, 0);
	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}

void Graphics::borderTransferLight(int level, std::shared_ptr<Texture3D> brickPoolTexture) {
	MaterialStore& matStore = MaterialStore::getInstance();
	const Material * material = matStore.findMaterialWithName("borderTransfer");

	glUseProgram(material->program);
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_nodeMapOnLevelCmdBuf[level]->m_bufferID);
	glUniform1ui(glGetUniformLocation(material->program, "level"), level);
	glUniform2iv(glGetUniformLocation(material->program, "nodeMapOffset[0]"), m_nodeMapOffsets.size(), glm::value_ptr(m_nodeMapOffsets[0]));
	glUniform2iv(glGetUniformLocation(material->program, "nodeMapSize[0]"), m_nodeMapSizes.size(), glm::value_ptr(m_nodeMapSizes[0]));

	int textureUnitIdx = 0;
	m_lightNodeMap->Activate(material->program, textureUnitIdx, "nodeMap");
	glBindImageTexture(textureUnitIdx, m_lightNodeMap->textureID, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);
	textureUnitIdx++;
	m_nodePoolTextures[NODE_POOL_COLOR]->Activate(material->program, "nodePool_color", textureUnitIdx);
	glBindImageTexture(textureUnitIdx, m_nodePoolTextures[NODE_POOL_COLOR]->m_textureID, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);
	textureUnitIdx++;
	brickPoolTexture->Activate(material->program, "brickPool_value", textureUnitIdx);
	glBindImageTexture(textureUnitIdx, brickPoolTexture->textureID, 0, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA8);

	{
		textureUnitIdx++;
		glUniform1ui(glGetUniformLocation(material->program, "axis"), 0);
		m_nodePoolTextures[NODE_POOL_NEIGH_X]->Activate(material->program, "nodePool_Neighbour", textureUnitIdx);
		glBindImageTexture(textureUnitIdx, m_nodePoolTextures[NODE_POOL_NEIGH_X]->m_textureID, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);
		glDrawArraysIndirect(GL_POINTS, 0);
		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

		glUniform1ui(glGetUniformLocation(material->program, "axis"), 1);
		m_nodePoolTextures[NODE_POOL_NEIGH_Y]->Activate(material->program, "nodePool_Neighbour", textureUnitIdx);
		glBindImageTexture(textureUnitIdx, m_nodePoolTextures[NODE_POOL_NEIGH_Y]->m_textureID, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);
		glDrawArraysIndirect(GL_POINTS, 0);
		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

		glUniform1ui(glGetUniformLocation(material->program, "axis"), 2);
		m_nodePoolTextures[NODE_POOL_NEIGH_Z]->Activate(material->program, "nodePool_Neighbour", textureUnitIdx);
		glBindImageTexture(textureUnitIdx, m_nodePoolTextures[NODE_POOL_NEIGH_Z]->m_textureID, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);
		glDrawArraysIndirect(GL_POINTS, 0);
		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
	}
}

void Graphics::mipmapCenterLight(int level, std::shared_ptr<Texture3D> brickPoolTexture) {
	const Material * material = MaterialStore::getInstance().findMaterialWithName("mipmapCenterLight");

	glUseProgram(material->program);
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);

	glUniform1ui(glGetUniformLocation(material->program, "level"), level);
	glUniform2iv(glGetUniformLocation(material->program, "nodeMapOffset[0]"), m_nodeMapOffsets.size(), glm::value_ptr(m_nodeMapOffsets[0]));
	glUniform2iv(glGetUniformLocation(material->program, "nodeMapSize[0]"), m_nodeMapSizes.size(), glm::value_ptr(m_nodeMapSizes[0]));

	int textureUnitIdx = 0;
	m_lightNodeMap->Activate(material->program, textureUnitIdx, "nodeMap");
	glBindImageTexture(textureUnitIdx, m_lightNodeMap->textureID, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);
	textureUnitIdx++;
	brickPoolTexture->Activate(material->program, "brickPool_value", textureUnitIdx);
	glBindImageTexture(textureUnitIdx, brickPoolTexture->textureID, 0, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA8);
	textureUnitIdx++;
	m_nodePoolTextures[NODE_POOL_NEXT]->Activate(material->program, "nodePool_next", textureUnitIdx);
	glBindImageTexture(textureUnitIdx, m_nodePoolTextures[NODE_POOL_NEXT]->m_textureID, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);
	textureUnitIdx++;
	m_nodePoolTextures[NODE_POOL_COLOR]->Activate(material->program, "nodePool_color", textureUnitIdx);
	glBindImageTexture(textureUnitIdx, m_nodePoolTextures[NODE_POOL_COLOR]->m_textureID, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);

	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_nodeMapOnLevelCmdBuf[level]->m_bufferID);
	glDrawArraysIndirect(GL_POINTS, 0);
	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}

void Graphics::mipmapFacesLight(int level, std::shared_ptr<Texture3D> brickPoolTexture) {
	const Material * material = MaterialStore::getInstance().findMaterialWithName("mipmapFacesLight");
	glUseProgram(material->program);
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
	
	glUniform1ui(glGetUniformLocation(material->program, "level"), level);
	glUniform2iv(glGetUniformLocation(material->program, "nodeMapOffset[0]"), m_nodeMapOffsets.size(), glm::value_ptr(m_nodeMapOffsets[0]));
	glUniform2iv(glGetUniformLocation(material->program, "nodeMapSize[0]"), m_nodeMapSizes.size(), glm::value_ptr(m_nodeMapSizes[0]));

	int textureUnitIdx = 0;
	m_lightNodeMap->Activate(material->program, textureUnitIdx, "nodeMap");
	glBindImageTexture(textureUnitIdx, m_lightNodeMap->textureID, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);
	textureUnitIdx++;
	brickPoolTexture->Activate(material->program, "brickPool_value", textureUnitIdx);
	glBindImageTexture(textureUnitIdx, brickPoolTexture->textureID, 0, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA8);
	textureUnitIdx++;
	m_nodePoolTextures[NODE_POOL_NEXT]->Activate(material->program, "nodePool_next", textureUnitIdx);
	glBindImageTexture(textureUnitIdx, m_nodePoolTextures[NODE_POOL_NEXT]->m_textureID, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);
	textureUnitIdx++;
	m_nodePoolTextures[NODE_POOL_COLOR]->Activate(material->program, "nodePool_color", textureUnitIdx);
	glBindImageTexture(textureUnitIdx, m_nodePoolTextures[NODE_POOL_COLOR]->m_textureID, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);

	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_nodeMapOnLevelCmdBuf[level]->m_bufferID);
	glDrawArraysIndirect(GL_POINTS, 0);
	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}

void Graphics::mipmapCornersLight(int level, std::shared_ptr<Texture3D> brickPoolTexture) {
	const Material * material = MaterialStore::getInstance().findMaterialWithName("mipmapCornersLight");
	glUseProgram(material->program);
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);

	glUniform1ui(glGetUniformLocation(material->program, "level"), level);
	glUniform2iv(glGetUniformLocation(material->program, "nodeMapOffset[0]"), m_nodeMapOffsets.size(), glm::value_ptr(m_nodeMapOffsets[0]));
	glUniform2iv(glGetUniformLocation(material->program, "nodeMapSize[0]"), m_nodeMapSizes.size(), glm::value_ptr(m_nodeMapSizes[0]));

	int textureUnitIdx = 0;
	m_lightNodeMap->Activate(material->program, textureUnitIdx, "nodeMap");
	glBindImageTexture(textureUnitIdx, m_lightNodeMap->textureID, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);
	textureUnitIdx++;
	brickPoolTexture->Activate(material->program, "brickPool_value", textureUnitIdx);
	glBindImageTexture(textureUnitIdx, brickPoolTexture->textureID, 0, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA8);
	textureUnitIdx++;
	m_nodePoolTextures[NODE_POOL_NEXT]->Activate(material->program, "nodePool_next", textureUnitIdx);
	glBindImageTexture(textureUnitIdx, m_nodePoolTextures[NODE_POOL_NEXT]->m_textureID, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);
	textureUnitIdx++;
	m_nodePoolTextures[NODE_POOL_COLOR]->Activate(material->program, "nodePool_color", textureUnitIdx);
	glBindImageTexture(textureUnitIdx, m_nodePoolTextures[NODE_POOL_COLOR]->m_textureID, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);

	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_nodeMapOnLevelCmdBuf[level]->m_bufferID);
	glDrawArraysIndirect(GL_POINTS, 0);
	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}

void Graphics::mipmapEdgesLight(int level, std::shared_ptr<Texture3D> brickPoolTexture) {
	const Material * material = MaterialStore::getInstance().findMaterialWithName("mipmapEdgesLight");
	glUseProgram(material->program);
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);

	glUniform1ui(glGetUniformLocation(material->program, "level"), level);
	glUniform2iv(glGetUniformLocation(material->program, "nodeMapOffset[0]"), m_nodeMapOffsets.size(), glm::value_ptr(m_nodeMapOffsets[0]));
	glUniform2iv(glGetUniformLocation(material->program, "nodeMapSize[0]"), m_nodeMapSizes.size(), glm::value_ptr(m_nodeMapSizes[0]));

	int textureUnitIdx = 0;
	m_lightNodeMap->Activate(material->program, textureUnitIdx, "nodeMap");
	glBindImageTexture(textureUnitIdx, m_lightNodeMap->textureID, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);
	textureUnitIdx++;
	brickPoolTexture->Activate(material->program, "brickPool_value", textureUnitIdx);
	glBindImageTexture(textureUnitIdx, brickPoolTexture->textureID, 0, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA8);
	textureUnitIdx++;
	m_nodePoolTextures[NODE_POOL_NEXT]->Activate(material->program, "nodePool_next", textureUnitIdx);
	glBindImageTexture(textureUnitIdx, m_nodePoolTextures[NODE_POOL_NEXT]->m_textureID, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);
	textureUnitIdx++;
	m_nodePoolTextures[NODE_POOL_COLOR]->Activate(material->program, "nodePool_color", textureUnitIdx);
	glBindImageTexture(textureUnitIdx, m_nodePoolTextures[NODE_POOL_COLOR]->m_textureID, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);

	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_nodeMapOnLevelCmdBuf[level]->m_bufferID);
	glDrawArraysIndirect(GL_POINTS, 0);
	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}

void Graphics::voxelize(Scene & renderingScene, bool clearVoxelization)
{
	if (clearVoxelization) {
		GLfloat clearColor[4] = { 0, 0, 0, 0 };
		voxelTexture->Clear(clearColor);
	}
	Material * material = voxelizationMaterial;

	glUseProgram(material->program);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	// Settings.
	glViewport(0, 0, voxelTextureSize, voxelTextureSize);
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
	glDisable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);

	// Texture.
	voxelTexture->Activate(material->program, "texture3D", 0);
	glBindImageTexture(0, voxelTexture->textureID, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA8);

	// Lighting.
	uploadLighting(renderingScene, material->program);

	// Render.
	renderQueue(renderingScene.renderers, material->program, true);
	if (automaticallyRegenerateMipmap || regenerateMipmapQueued) {
		glGenerateMipmap(GL_TEXTURE_3D);
		regenerateMipmapQueued = false;
	}
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
}

// ----------------------
// Voxelization visualization.
// ----------------------
void Graphics::initVoxelVisualization(unsigned int viewportWidth, unsigned int viewportHeight)
{
	// Materials.
	worldPositionMaterial = MaterialStore::getInstance().findMaterialWithName("world_position");
	voxelVisualizationMaterial = MaterialStore::getInstance().findMaterialWithName("voxel_visualization");

	assert(worldPositionMaterial != nullptr);
	assert(voxelVisualizationMaterial != nullptr);

	// FBOs.
	vvfbo1 = new FBO(viewportHeight, viewportWidth);
	vvfbo2 = new FBO(viewportHeight, viewportWidth);

	// Rendering cube.
	cubeShape = ObjLoader::loadObjFile("Assets\\Models\\cube.obj");
	assert(cubeShape->meshes.size() == 1);
	cubeMeshRenderer = new MeshRenderer(&cubeShape->meshes[0]);

	// Rendering quad.
	quad = StandardShapes::createQuad();
	quadMeshRenderer = new MeshRenderer(&quad);
}

void Graphics::renderVoxelVisualization(Scene & renderingScene, unsigned int viewportWidth, unsigned int viewportHeight)
{
	// -------------------------------------------------------
	// Render cube to FBOs.
	// -------------------------------------------------------
	Camera & camera = *renderingScene.renderingCamera;
	auto program = worldPositionMaterial->program;
	glUseProgram(program);
	uploadCamera(camera, program);

	// Settings.
	glClearColor(0.0, 0.0, 0.0, 1.0);
	glEnable(GL_CULL_FACE);
	glEnable(GL_DEPTH_TEST);

	// Back.
	glCullFace(GL_FRONT);
	glBindFramebuffer(GL_FRAMEBUFFER, vvfbo1->frameBuffer);
	glViewport(0, 0, vvfbo1->width, vvfbo1->height);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	cubeMeshRenderer->render(program);

	// Front.
	glCullFace(GL_BACK);
	glBindFramebuffer(GL_FRAMEBUFFER, vvfbo2->frameBuffer);
	glViewport(0, 0, vvfbo2->width, vvfbo2->height);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	cubeMeshRenderer->render(program);

	// -------------------------------------------------------
	// Render 3D texture to screen.
	// -------------------------------------------------------
	program = voxelVisualizationMaterial->program;
	glUseProgram(program);
	uploadCamera(camera, program);
	glBindRenderbuffer(GL_RENDERBUFFER, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	// Settings.
	uploadGlobalConstants(voxelVisualizationMaterial->program, viewportWidth, viewportHeight);
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);

	// Activate textures.
	vvfbo1->ActivateAsTexture(program, "textureBack", 0);
	vvfbo2->ActivateAsTexture(program, "textureFront", 1);
	voxelTexture->Activate(program, "texture3D", 2);

	// Render.
	glViewport(0, 0, viewportWidth, viewportHeight);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	quadMeshRenderer->render(program);
}

Graphics::~Graphics()
{
	if (vvfbo1) delete vvfbo1;
	if (vvfbo2) delete vvfbo2;
	if (quadMeshRenderer) delete quadMeshRenderer;
	if (cubeMeshRenderer) delete cubeMeshRenderer;
	if (cubeShape) delete cubeShape;
	if (voxelTexture) delete voxelTexture;
}