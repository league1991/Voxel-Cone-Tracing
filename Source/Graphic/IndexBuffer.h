#pragma once

#include <vector>

#define GLEW_STATIC
#include <glew.h>
#include <glfw3.h>
#include <SOIL.h>

class IndexBuffer
{
public:
  IndexBuffer(GLuint bufferTarget,
    GLsizeiptr sizeInBytes,
    GLuint usageHint,
    GLvoid* data)
  {
    glGenBuffers(1, &m_bufferID);
    glBindBuffer(bufferTarget, m_bufferID);
    glBufferData(bufferTarget, sizeInBytes, data, usageHint);
  }

  ~IndexBuffer() {
    glDeleteBuffers(1, &m_bufferID);
  }

  GLuint m_bufferID;
};

