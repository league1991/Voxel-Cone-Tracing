#pragma once

#include <vector>

class Material;

/// <summary> Manages all loaded materials and shader programs. </summary>
class MaterialStore {
public:
  struct ShaderInfo {
    std::string m_path;
    std::string m_preprocessorDef;
  };
  static MaterialStore &getInstance();
  std::vector<Material *> materials;
  Material *MaterialStore::findMaterialWithName(std::string name);
  Material *MaterialStore::findMaterialWithProgramID(unsigned int programID);

  void AddNewMaterial(std::string name, const char *vertexPath = nullptr,
                      const char *fragmentPath = nullptr,
                      const char *geometryPath = nullptr,
                      const char *tessEvalPath = nullptr,
                      const char *tessCtrlPath = nullptr);

  void AddNewMaterial(std::string name, const ShaderInfo *vertexInfo = nullptr,
                      const ShaderInfo *fragmentInfo = nullptr,
                      const ShaderInfo *geometryInfo = nullptr,
                      const ShaderInfo *tessEvalInfo = nullptr,
                      const ShaderInfo *tessCtrlInfo = nullptr);

  ~MaterialStore();

private:
  MaterialStore();
  MaterialStore(MaterialStore const &) = delete;
  void operator=(MaterialStore const &) = delete;
};