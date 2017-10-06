#pragma once

#include <vector>

#define GLEW_STATIC
#include <glew.h>
#include <glfw3.h>
#include <SOIL.h>

class TextureBuffer
{
public:
  TextureBuffer(int sizeOfBytes, char* data = nullptr)
  {
    glGenTextures(1, &m_textureID);
    glBindTexture(GL_TEXTURE_BUFFER, m_textureID);
    
    glGenBuffers(1, &m_bufferID);
    glBindBuffer(GL_TEXTURE_BUFFER, m_bufferID);
    glBufferData(GL_TEXTURE_BUFFER, sizeOfBytes, data, GL_STATIC_DRAW);
    
    glTextureBuffer(m_textureID, GL_R32UI, m_bufferID);
  }

  ~TextureBuffer() {
    glDeleteTextures(1, &m_textureID);
    glDeleteBuffers(1, &m_bufferID);
  }

  void Activate(const int shaderProgram, const std::string glSamplerName, const int textureUnit)
  {
    glActiveTexture(GL_TEXTURE0 + textureUnit);
    glBindTexture(GL_TEXTURE_BUFFER, m_textureID);
    glUniform1i(glGetUniformLocation(shaderProgram, glSamplerName.c_str()), textureUnit);
  }
  GLuint m_textureID;
  GLuint m_bufferID;
};

