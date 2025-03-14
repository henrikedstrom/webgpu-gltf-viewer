#pragma once

// Project Headers
#include "model.h"

namespace mesh_utils
{

void GenerateTangents(const Model::SubMesh &subMesh, std::vector<Model::Vertex> &vertices,
                      std::vector<uint32_t> &indices);

} // namespace mesh_utils