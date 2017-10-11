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

	// Render.
	switch (renderingMode) {
	case RenderingMode::VOXELIZATION_VISUALIZATION:
		//renderVoxelVisualization(renderingScene, viewportWidth, viewportHeight);
		visualizeVoxel(renderingScene, viewportWidth, viewportHeight);
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

  initSparseVoxelization();
}

void Graphics::initSparseVoxelization() {

  // Initialize node pool
	m_nodePoolDim = 64;
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

  // Initialize brick pool
  m_brickPoolDim = 70;
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
  m_nextFreeCounter = std::shared_ptr<IndexBuffer>(new IndexBuffer(GL_ATOMIC_COUNTER_BUFFER, sizeof(counterVal), GL_STATIC_DRAW, &counterVal));
  m_fragmentListCounter = std::shared_ptr<IndexBuffer>(new IndexBuffer(GL_ATOMIC_COUNTER_BUFFER, sizeof(counterVal), GL_STATIC_DRAW, &counterVal));

  // Init indirect draw command buffer
  IndirectDrawCommand indirectCommand;
  indirectCommand.baseInstanceIdx = 0;
  indirectCommand.firstVertexIdx = 0;
  indirectCommand.numPrimitives = 1;
  indirectCommand.numVertices = totalVoxels;
  m_nodePoolDrawCommandBuffer = std::shared_ptr<IndexBuffer>(new IndexBuffer(GL_DRAW_INDIRECT_BUFFER, sizeof(indirectCommand), GL_STATIC_DRAW, &indirectCommand));
  indirectCommand.numVertices = m_brickPoolDim * m_brickPoolDim * m_brickPoolDim;
  m_brickPoolDrawCommandBuffer = std::shared_ptr<IndexBuffer>(new IndexBuffer(GL_DRAW_INDIRECT_BUFFER, sizeof(indirectCommand), GL_STATIC_DRAW, &indirectCommand));
  indirectCommand.numVertices = m_nodePoolDim * m_nodePoolDim * m_nodePoolDim;
  m_fragmentTexDrawCommandBuffer = std::shared_ptr<IndexBuffer>(new IndexBuffer(GL_DRAW_INDIRECT_BUFFER, sizeof(indirectCommand), GL_STATIC_DRAW, &indirectCommand));
  indirectCommand.numVertices = 1;
  m_modifyIndirectBufferCommandBuffer = std::shared_ptr<IndexBuffer>(new IndexBuffer(GL_DRAW_INDIRECT_BUFFER, sizeof(indirectCommand), GL_STATIC_DRAW, &indirectCommand));
  m_fragmentListDrawCommandBuffer = std::shared_ptr<TextureBuffer>(new TextureBuffer(sizeof(indirectCommand), (char*)&indirectCommand));

  MaterialStore::getInstance().AddNewMaterial("clearNodePool", "SparseVoxelOctree\\clearNodePoolVert.shader");
  MaterialStore::getInstance().AddNewMaterial("clearNodePoolNeigh", "SparseVoxelOctree\\clearNodePoolNeighVert.shader");
  MaterialStore::getInstance().AddNewMaterial("clearBrickPool", "SparseVoxelOctree\\clearBrickPoolVert.shader");
  MaterialStore::getInstance().AddNewMaterial("clearFragmentTex", "SparseVoxelOctree\\clearFragmentTexVert.shader");
  MaterialStore::getInstance().AddNewMaterial("voxelize", "SparseVoxelOctree\\VoxelizeVert.shader", "SparseVoxelOctree\\VoxelizeFrag.shader", "SparseVoxelOctree\\VoxelizeGeom.shader");
  MaterialStore::getInstance().AddNewMaterial("modifyIndirectBuffer", "SparseVoxelOctree\\modifyIndirectBufferVert.shader");
  MaterialStore::getInstance().AddNewMaterial("voxelVisualization", "SparseVoxelOctree\\voxelVisualizationVert.shader", "SparseVoxelOctree\\voxelVisualizationFrag.shader","SparseVoxelOctree\\voxelVisualizationGeom.shader");
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
  glColorMask(false, false, false, false);
  MaterialStore& matStore = MaterialStore::getInstance();

  clearNodePool(renderingScene);

  clearBrickPool(renderingScene);

  clearFragmentTex(renderingScene);

  voxelizeScene(renderingScene);

  modifyIndirectBuffer(renderingScene);
}

void Graphics::clearNodePool(Scene & renderingScene) {
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
	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_nodePoolDrawCommandBuffer->m_bufferID);
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
	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_nodePoolDrawCommandBuffer->m_bufferID);
	glDrawArraysIndirect(GL_POINTS, 0);
	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}

void Graphics::clearBrickPool(Scene & renderingScene) {
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
	glUniform1i(glGetUniformLocation(clearShader->program, "clearMode"), 0);
	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_brickPoolDrawCommandBuffer->m_bufferID);
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
	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_fragmentTexDrawCommandBuffer->m_bufferID);
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
		glBindImageTexture(textureUnitIdx, m_fragmentTextures[fIdx]->textureID, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R32UI);
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

void Graphics::modifyIndirectBuffer(Scene& renderingScene) {
	MaterialStore& matStore = MaterialStore::getInstance();
	auto modifyIndirectDrawShader = matStore.findMaterialWithName("modifyIndirectBuffer");
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
	glUseProgram(modifyIndirectDrawShader->program);

	// bind texture buffer which will be modified by shader
	m_fragmentListDrawCommandBuffer->Activate(modifyIndirectDrawShader->program, "indirectCommandBuf", 0);
	glBindImageTexture(0, m_fragmentListDrawCommandBuffer->m_textureID, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R32UI);

	// bind atomic variable
	int bindingPoint = 0;
	glGetActiveAtomicCounterBufferiv(modifyIndirectDrawShader->program, 0, GL_ATOMIC_COUNTER_BUFFER_BINDING, &bindingPoint);
	glBindBufferBase(GL_ATOMIC_COUNTER_BUFFER, bindingPoint, m_fragmentListCounter->m_bufferID);

	// bind indirect draw buffer and draw
	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_modifyIndirectBufferCommandBuffer->m_bufferID);
	glDrawArraysIndirect(GL_POINTS, 0);
}

void Graphics::visualizeVoxel(Scene& renderingScene, unsigned int viewportWidth, unsigned int viewportHeight)
{
	MaterialStore& matStore = MaterialStore::getInstance();
	auto & camera = *renderingScene.renderingCamera;
	const Material * material = matStore.findMaterialWithName("voxelVisualization");;
	const GLuint program = material->program;

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glUseProgram(program);

	// GL Settings.
	{
		glViewport(0, 0, viewportWidth, viewportHeight);
		glClearColor(0.0f, 0.0f, 0.0f, 1.0);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glEnable(GL_DEPTH_TEST);
		glEnable(GL_CULL_FACE);
		glCullFace(GL_BACK);
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}

	// Upload uniforms.
	uploadCamera(camera, program);
	uploadGlobalConstants(program, viewportWidth, viewportHeight);
	uploadLighting(renderingScene, program);
	uploadRenderingSettings(program);

	int textureUnitIdx = 0;
	m_fragmentList->Activate(material->program, "voxelFragList_position", textureUnitIdx);
	glBindImageTexture(textureUnitIdx, m_fragmentList->m_textureID, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R32UI);
	textureUnitIdx++;
	m_fragmentTextures[FRAG_TEX_COLOR]->Activate(material->program, "voxelFragTex_color", textureUnitIdx);
	glBindImageTexture(textureUnitIdx, m_fragmentTextures[FRAG_TEX_COLOR]->textureID, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R32UI);

	glUniform1ui(glGetUniformLocation(material->program, "voxelTexSize"), m_nodePoolDim);
	glm::mat4 voxelGridTransform = getVoxelTransform(renderingScene);
	glUniformMatrix4fv(glGetUniformLocation(material->program, "voxelGridTransform"), 1, GL_FALSE, glm::value_ptr(voxelGridTransform));

	// Render.
	//renderQueue(renderingScene.renderers, material->program, true);
	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_fragmentListDrawCommandBuffer->m_bufferID);
	glDrawArraysIndirect(GL_POINTS, 0);
	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}

void Graphics::voxelize(Scene & renderingScene, bool clearVoxelization)
{
  sparseVoxelize(renderingScene, clearVoxelization);

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