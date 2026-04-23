#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "SpotLight.h"
#include "basic_camera.h"
#include "camera.h"
#include "cube.h"
#include "pointLight.h"
#include "shader.h"
#include "stb_image.h"

#include <cmath>
#include <iostream>
#include <unordered_map>
#include <vector>

#include <cfloat>  // For FLT_MAX
#include <cstdlib> // For rand()
#include <ctime>   // For time()
#include <filesystem>
#include <iostream>

#include <iostream>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
// Example to manage smoothing groups
struct SmoothingGroup {
  std::vector<int> faces; // Indices of faces in this group
};

std::unordered_map<int, SmoothingGroup> smoothingGroups;

void parseOBJ(const std::string &filePath, std::vector<glm::vec3> &vertices,
              std::vector<std::vector<int>> &faces) {
  std::ifstream file(filePath);
  if (!file.is_open()) {
    std::cerr << "Failed to open file: " << filePath << std::endl;
    return;
  }

  int currentSmoothingGroup = 0;
  std::string line;

  while (std::getline(file, line)) {
    std::istringstream ss(line);
    std::string prefix;
    ss >> prefix;

    if (prefix == "v") {
      glm::vec3 vertex;
      ss >> vertex.x >> vertex.y >> vertex.z;
      vertices.push_back(vertex);
    } else if (prefix == "f") {
      std::vector<int> face;
      std::string vertexStr;

      while (ss >> vertexStr) {
        size_t doubleSlashPos = vertexStr.find("//");
        if (doubleSlashPos != std::string::npos) {
          // Handle "f 1//1 2//2 3//3"
          std::string vertexIndex = vertexStr.substr(0, doubleSlashPos);
          if (!vertexIndex.empty()) {
            int index = std::stoi(vertexIndex) - 1; // Convert to 0-based index
            face.push_back(index);
          }
        } else if (vertexStr.find("/") == std::string::npos) {
          // Handle "f 1 2 3"
          int index = std::stoi(vertexStr) - 1; // Convert to 0-based index
          face.push_back(index);
        } else {
          std::cerr << "Unexpected face format: " << vertexStr << std::endl;
        }
      }

      if (face.size() >= 3) {
        faces.push_back(face);

        // Add face to current smoothing group
        if (currentSmoothingGroup > 0) {
          smoothingGroups[currentSmoothingGroup].faces.push_back(faces.size() -
                                                                 1);
        }
      }
    } else if (prefix == "s") {
      std::string group;
      ss >> group;
      if (group == "off" || group == "0") {
        currentSmoothingGroup = 0;
      } else {
        currentSmoothingGroup = std::stoi(group);
      }
    }
  }

  file.close();
}

// Compute normals and create combined array
std::vector<float> computeNormals(const std::vector<glm::vec3> &vertices,
                                  const std::vector<std::vector<int>> &faces) {
  std::vector<glm::vec3> normals(vertices.size(), glm::vec3(0.0f));
  std::vector<float> vertexData;

  // Calculate face normals
  for (const auto &face : faces) {
    glm::vec3 v0 = vertices[face[0]];
    glm::vec3 v1 = vertices[face[1]];
    glm::vec3 v2 = vertices[face[2]];

    glm::vec3 edge1 = v1 - v0;
    glm::vec3 edge2 = v2 - v0;
    glm::vec3 faceNormal = glm::normalize(glm::cross(edge1, edge2));

    // Add face normal to vertex normals (for smooth shading)
    for (int index : face) {
      normals[index] += faceNormal;
    }
  }

  // Normalize vertex normals
  for (auto &normal : normals) {
    normal = glm::normalize(normal);
  }

  // Combine positions and normals
  for (size_t i = 0; i < vertices.size(); ++i) {
    const glm::vec3 &position = vertices[i];
    const glm::vec3 &normal = normals[i];

    vertexData.push_back(position.x);
    vertexData.push_back(position.y);
    vertexData.push_back(position.z);
    vertexData.push_back(normal.x);
    vertexData.push_back(normal.y);
    vertexData.push_back(normal.z);
  }

  return vertexData;
}

glm::vec2 calculateBoxUV(const glm::vec3 &vertex) {
  glm::vec3 absVertex = glm::abs(vertex);
  if (absVertex.x > absVertex.y && absVertex.x > absVertex.z) {
    return glm::vec2(vertex.y, vertex.z); // Project onto YZ plane
  } else if (absVertex.y > absVertex.z) {
    return glm::vec2(vertex.x, vertex.z); // Project onto XZ plane
  } else {
    return glm::vec2(vertex.x, vertex.y); // Project onto XY plane
  }
}

glm::vec2 calculateSphericalUV(const glm::vec3 &vertex) {
  float len = glm::length(vertex);
  if (len < 0.0001f)
    return glm::vec2(0.0f, 0.0f); // Degenerate vertex

  // Normalize the vertex direction to get proper spherical angles
  glm::vec3 dir = vertex / len;

  float theta = atan2(dir.z, dir.x); // Angle around the Y-axis
  // Clamp to prevent NaN from floating point precision issues
  float cosPhiClamped = glm::clamp(dir.y, -1.0f, 1.0f);
  float phi = acos(cosPhiClamped); // Angle from the Y-axis

  float u = (theta + M_PI) / (2.0f * M_PI); // Normalize to [0, 1]
  float v = phi / M_PI;                     // Normalize to [0, 1]

  return glm::vec2(u, v);
}

glm::vec2 calculateCylindricalUV(const glm::vec3 &vertex) {
  // U wraps around the Y-axis, V goes along the height
  float u = (atan2(vertex.z, vertex.x) + M_PI) / (2.0f * M_PI);
  // V is just the normalized height (caller should center the vertex)
  float v = vertex.y; // Will be normalized by the caller
  return glm::vec2(u, v);
}

glm::vec2 calculatePlanarUV(const glm::vec3 &vertex) {
  return glm::vec2(vertex.x, vertex.z); // Map x and z to u and v
}

std::vector<float>
computeNormalsAndGeneratedUV(const std::vector<glm::vec3> &vertices,
                             const std::vector<std::vector<int>> &faces) {

  std::vector<glm::vec3> normals(vertices.size(), glm::vec3(0.0f));
  std::vector<float> vertexData;

  // Calculate face normals
  for (const auto &face : faces) {
    glm::vec3 v0 = vertices[face[0]];
    glm::vec3 v1 = vertices[face[1]];
    glm::vec3 v2 = vertices[face[2]];

    glm::vec3 edge1 = v1 - v0;
    glm::vec3 edge2 = v2 - v0;
    glm::vec3 faceNormal = glm::normalize(glm::cross(edge1, edge2));

    for (int index : face) {
      normals[index] += faceNormal;
    }
  }

  // Normalize vertex normals
  for (auto &normal : normals) {
    normal = glm::normalize(normal);
  }

  // Compute bounding box center so UVs are computed on centered positions
  glm::vec3 minPos(FLT_MAX);
  glm::vec3 maxPos(-FLT_MAX);
  for (const auto &v : vertices) {
    minPos = glm::min(minPos, v);
    maxPos = glm::max(maxPos, v);
  }
  glm::vec3 center = (minPos + maxPos) * 0.5f;

  // Combine positions, normals, and generated UVs
  for (size_t i = 0; i < vertices.size(); ++i) {
    const glm::vec3 &position = vertices[i];
    const glm::vec3 &normal = normals[i];
    // Center the position before computing spherical UV so vertices are
    // distributed around the origin, giving proper UV coverage
    glm::vec3 centered = position - center;
    glm::vec2 uv = calculateSphericalUV(centered);

    vertexData.push_back(position.x);
    vertexData.push_back(position.y);
    vertexData.push_back(position.z);
    vertexData.push_back(normal.x);
    vertexData.push_back(normal.y);
    vertexData.push_back(normal.z);
    vertexData.push_back(uv.x);
    vertexData.push_back(uv.y);
  }

  return vertexData;
}

std::vector<float>
computeNormalsAndCylindricalUV(const std::vector<glm::vec3> &vertices,
                               const std::vector<std::vector<int>> &faces) {

  std::vector<glm::vec3> normals(vertices.size(), glm::vec3(0.0f));
  std::vector<float> vertexData;

  // Calculate face normals
  for (const auto &face : faces) {
    glm::vec3 v0 = vertices[face[0]];
    glm::vec3 v1 = vertices[face[1]];
    glm::vec3 v2 = vertices[face[2]];

    glm::vec3 edge1 = v1 - v0;
    glm::vec3 edge2 = v2 - v0;
    glm::vec3 faceNormal = glm::normalize(glm::cross(edge1, edge2));

    for (int index : face) {
      normals[index] += faceNormal;
    }
  }

  // Normalize vertex normals
  for (auto &normal : normals) {
    normal = glm::normalize(normal);
  }

  // Compute bounding box for centering and height normalization
  glm::vec3 minPos(FLT_MAX);
  glm::vec3 maxPos(-FLT_MAX);
  for (const auto &v : vertices) {
    minPos = glm::min(minPos, v);
    maxPos = glm::max(maxPos, v);
  }
  glm::vec3 center = (minPos + maxPos) * 0.5f;
  float height = maxPos.y - minPos.y;
  if (height < 0.0001f)
    height = 1.0f;

  // Combine positions, normals, and generated cylindrical UVs
  for (size_t i = 0; i < vertices.size(); ++i) {
    const glm::vec3 &position = vertices[i];
    const glm::vec3 &normal = normals[i];
    glm::vec3 centered = position - center;
    glm::vec2 uv = calculateCylindricalUV(centered);
    // Normalize V to [0, 1] based on height
    uv.y = (position.y - minPos.y) / height;

    vertexData.push_back(position.x);
    vertexData.push_back(position.y);
    vertexData.push_back(position.z);
    vertexData.push_back(normal.x);
    vertexData.push_back(normal.y);
    vertexData.push_back(normal.z);
    vertexData.push_back(uv.x);
    vertexData.push_back(uv.y);
  }

  return vertexData;
}

void generateCone(float radius, float height, int sectorCount,
                  std::vector<float> &vertices,
                  std::vector<unsigned int> &indices) {
  float x, y, z;
  float sectorStep = 2 * M_PI / sectorCount;
  float sectorAngle;

  // Top vertex
  vertices.push_back(0.0f);
  vertices.push_back(height);
  vertices.push_back(0.0f);
  vertices.push_back(0.0f);
  vertices.push_back(1.0f);
  vertices.push_back(0.0f);
  vertices.push_back(0.5f);
  vertices.push_back(1.0f);

  // Base vertices
  for (int i = 0; i <= sectorCount; ++i) {
    sectorAngle = i * sectorStep;
    x = radius * cosf(sectorAngle);
    z = radius * sinf(sectorAngle);

    vertices.push_back(x);
    vertices.push_back(0.0f);
    vertices.push_back(z);

    float ny = radius / height;
    glm::vec3 normal = glm::normalize(glm::vec3(x, ny * radius, z));
    vertices.push_back(normal.x);
    vertices.push_back(normal.y);
    vertices.push_back(normal.z);

    vertices.push_back((float)i / sectorCount);
    vertices.push_back(0.0f);
  }

  for (unsigned int i = 1; i <= sectorCount; ++i) {
    indices.push_back(0);
    indices.push_back(i);
    indices.push_back(i + 1);
  }

  unsigned int centerIdx = vertices.size() / 8;
  vertices.push_back(0.0f);
  vertices.push_back(0.0f);
  vertices.push_back(0.0f);
  vertices.push_back(0.0f);
  vertices.push_back(-1.0f);
  vertices.push_back(0.0f);
  vertices.push_back(0.5f);
  vertices.push_back(0.5f);

  for (unsigned int i = 1; i <= sectorCount; ++i) {
    indices.push_back(centerIdx);
    indices.push_back(i + 1);
    indices.push_back(i);
  }
}

void generateSphere(float radius, int sectorCount, int stackCount,
                    std::vector<float> &vertices,
                    std::vector<unsigned int> &indices) {
  float x, y, z, xy; // vertex position
  float nx, ny, nz,
      lengthInv =
          1.0f / radius; // vertex normal, lenginv is the inverse of the radius

  float sectorStep = 2 * M_PI / sectorCount;
  float stackStep = M_PI / stackCount;
  float sectorAngle, stackAngle;

  for (int i = 0; i <= stackCount; ++i) {
    stackAngle = M_PI / 2 - i * stackStep; // starting from pi/2 to -pi/2
    xy = radius * cosf(stackAngle);        // r * cos(u)
    z = radius * sinf(stackAngle);         // r * sin(u)

    for (int j = 0; j <= sectorCount; ++j) {
      sectorAngle = j * sectorStep; // starting from 0 to 2pi

      // vertex position (x, y, z)
      x = xy * cosf(sectorAngle); // r * cos(u) * cos(v)
      y = xy * sinf(sectorAngle); // r * cos(u) * sin(v)
      vertices.push_back(x);
      vertices.push_back(y);
      vertices.push_back(z);

      // normalized vertex normal (nx, ny, nz)
      nx = x * lengthInv;
      ny = y * lengthInv;
      nz = z * lengthInv;
      vertices.push_back(nx);
      vertices.push_back(ny);
      vertices.push_back(nz);
    }
  }

  // generate indices
  int k1, k2;
  for (int i = 0; i < stackCount; ++i) {
    k1 = i * (sectorCount + 1); // beginning of current stack
    k2 = k1 + sectorCount + 1;  // beginning of next stack

    for (int j = 0; j < sectorCount; ++j, ++k1, ++k2) {
      // 2 triangles per sector excluding first and last stacks
      if (i != 0) {
        indices.push_back(k1);
        indices.push_back(k2);
        indices.push_back(k1 + 1);
      }

      if (i != (stackCount - 1)) {
        indices.push_back(k1 + 1);
        indices.push_back(k2);
        indices.push_back(k2 + 1);
      }
    }
  }
}

// Bezier curve globals
const double pi = 3.14159265389;
const int nt = 40;
const int ntheta = 20;

long long nCr(int n, int r) {
  if (r > n / 2)
    r = n - r;
  long long ans = 1;
  int i;
  for (i = 1; i <= r; i++) {
    ans *= n - r + i;
    ans /= i;
  }
  return ans;
}

void BezierCurve(double t, float xy[2], GLfloat ctrlpoints[], int L) {
  double y = 0;
  double x = 0;
  t = t > 1.0 ? 1.0 : t;
  for (int i = 0; i < L + 1; i++) {
    long long ncr = nCr(L, i);
    double oneMinusTpow = pow(1 - t, double(L - i));
    double tPow = pow(t, double(i));
    double coef = oneMinusTpow * tPow * ncr;
    x += coef * ctrlpoints[i * 3];
    y += coef * ctrlpoints[(i * 3) + 1];
  }
  xy[0] = float(x);
  xy[1] = float(y);
}

unsigned int hollowBezier(GLfloat ctrlpoints[], int L,
                          vector<float> &coordinates, vector<float> &normals,
                          vector<int> &indices, vector<float> &vertices,
                          float div = 1.0) {
  int i, j;
  float x, y, z, r;
  float theta;
  float nx, ny, nz, lengthInv;

  const float dtheta = (2 * pi / ntheta) / div;

  float t = 0;
  float dt = 1.0 / nt;
  float xy[2];
  vector<float> textureCoords;
  for (i = 0; i <= nt; ++i) {
    BezierCurve(t, xy, ctrlpoints, L);
    r = xy[0];
    y = xy[1];
    theta = 0;
    t += dt;
    lengthInv = 1.0 / r;

    for (j = 0; j <= ntheta; ++j) {
      double cosa = cos(theta);
      double sina = sin(theta);
      z = r * cosa;
      x = r * sina;

      coordinates.push_back(x);
      coordinates.push_back(y);
      coordinates.push_back(z);

      nx = (x - 0) * lengthInv;
      ny = (y - y) * lengthInv;
      nz = (z - 0) * lengthInv;

      normals.push_back(nx);
      normals.push_back(ny);
      normals.push_back(nz);

      float u = float(j) / float(ntheta);
      float v = float(i) / float(nt);
      textureCoords.push_back(u);
      textureCoords.push_back(v);

      theta += dtheta;
    }
  }

  int k1, k2;
  for (int i = 0; i < nt; ++i) {
    k1 = i * (ntheta + 1);
    k2 = k1 + ntheta + 1;

    for (int j = 0; j < ntheta; ++j, ++k1, ++k2) {
      indices.push_back(k1);
      indices.push_back(k2);
      indices.push_back(k1 + 1);

      indices.push_back(k1 + 1);
      indices.push_back(k2);
      indices.push_back(k2 + 1);
    }
  }

  size_t count = coordinates.size();
  for (int i = 0; i < count; i += 3) {
    vertices.push_back(coordinates[i]);
    vertices.push_back(coordinates[i + 1]);
    vertices.push_back(coordinates[i + 2]);

    vertices.push_back(normals[i]);
    vertices.push_back(normals[i + 1]);
    vertices.push_back(normals[i + 2]);

    vertices.push_back(textureCoords[i / 3 * 2]);
    vertices.push_back(textureCoords[i / 3 * 2 + 1]);
  }

  unsigned int bezierVAO;
  glGenVertexArrays(1, &bezierVAO);
  glBindVertexArray(bezierVAO);

  unsigned int bezierVBO;
  glGenBuffers(1, &bezierVBO);
  glBindBuffer(GL_ARRAY_BUFFER, bezierVBO);
  glBufferData(GL_ARRAY_BUFFER, (unsigned int)vertices.size() * sizeof(float),
               vertices.data(), GL_STATIC_DRAW);

  unsigned int bezierEBO;
  glGenBuffers(1, &bezierEBO);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, bezierEBO);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER,
               (unsigned int)indices.size() * sizeof(unsigned int),
               indices.data(), GL_STATIC_DRAW);

  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glEnableVertexAttribArray(2);

  int stride = 32;
  glVertexAttribPointer(0, 3, GL_FLOAT, false, stride, (void *)0);
  glVertexAttribPointer(1, 3, GL_FLOAT, false, stride,
                        (void *)(sizeof(float) * 3));
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride,
                        (void *)(6 * sizeof(float)));

  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

  return bezierVAO;
}

// Function to generate fractal tree branches with normals in 3D
void generateTree(std::vector<float> &vertices,
                  std::vector<unsigned int> &indices, glm::vec3 start,
                  glm::vec3 direction, float length, int depth, int maxDepth) {
  if (depth == 0 || length < 0.01f)
    return;

  // Calculate the end point of the current branch
  glm::vec3 end = start + direction * length;
  glm::vec3 normal =
      glm::normalize(glm::cross(direction, glm::vec3(0.0f, 0.0f, 1.0f)));

  // Add vertices and normals for the branch
  unsigned int startIndex = vertices.size() / 6;
  vertices.push_back(start.x);
  vertices.push_back(start.y);
  vertices.push_back(start.z);
  vertices.push_back(normal.x);
  vertices.push_back(normal.y);
  vertices.push_back(normal.z);

  vertices.push_back(end.x);
  vertices.push_back(end.y);
  vertices.push_back(end.z);
  vertices.push_back(normal.x);
  vertices.push_back(normal.y);
  vertices.push_back(normal.z);

  // Add indices for the branch
  indices.push_back(startIndex);
  indices.push_back(startIndex + 1);

  // Recursive branching (left, right, and up branches)
  float angleOffset =
      glm::radians(30.0f + (rand() % 10 - 5)); // Randomize angle slightly
  int numBranches = 3; // Three branches: left, right, and up

  for (int i = 0; i < numBranches; ++i) {
    glm::vec3 newDirection = direction; // Start with the current direction

    // Apply rotation based on the branch index
    if (i == 0) {
      glm::mat4 rotationMatrix =
          glm::rotate(glm::mat4(1.0f), angleOffset,
                      glm::vec3(1.0f, 0.0f, 0.0f)); // Rotate around x-axis
      newDirection = glm::vec3(rotationMatrix * glm::vec4(direction, 0.0f));
    } else if (i == 1) {
      glm::mat4 rotationMatrix =
          glm::rotate(glm::mat4(1.0f), angleOffset,
                      glm::vec3(0.0f, 1.0f, 0.0f)); // Rotate around y-axis
      newDirection = glm::vec3(rotationMatrix * glm::vec4(direction, 0.0f));
    } else {
      glm::mat4 rotationMatrix =
          glm::rotate(glm::mat4(1.0f), angleOffset,
                      glm::vec3(0.0f, 0.0f, 1.0f)); // Rotate around z-axis
      newDirection = glm::vec3(rotationMatrix * glm::vec4(direction, 0.0f));
    }

    // Random length factor to ensure it doesn't go to zero
    float lengthFactor = 0.6f + static_cast<float>(rand()) / RAND_MAX *
                                    0.2f; // Random length factor
    generateTree(vertices, indices, end, newDirection, length * lengthFactor,
                 depth - 1, maxDepth);
  }
}

// ─── Fractal Tree with Solid Cylinder Geometry ───────────────────────────────

// Build orthonormal basis given a direction vector.
static void buildBasis(glm::vec3 dir, glm::vec3 &outRight, glm::vec3 &outFwd) {
  if (fabsf(dir.y) < 0.99f)
    outRight = glm::normalize(glm::cross(dir, glm::vec3(0.0f, 1.0f, 0.0f)));
  else
    outRight = glm::normalize(glm::cross(dir, glm::vec3(1.0f, 0.0f, 0.0f)));
  outFwd = glm::cross(outRight, dir);
}

// Append a tapered cylinder from 'start' to 'end' with texture coords.
// Vertex layout: pos(3) + normal(3) + uv(2) = 8 floats
static void addCylinderSegment(std::vector<float> &vertices,
                               std::vector<unsigned int> &indices,
                               glm::vec3 start, glm::vec3 end, float r0,
                               float r1, int segs = 8) {
  glm::vec3 up = glm::normalize(end - start);
  glm::vec3 right, fwd;
  buildBasis(up, right, fwd);

  unsigned int base = (unsigned int)(vertices.size() / 8);

  for (int ring = 0; ring <= 1; ++ring) {
    float t = (float)ring;
    glm::vec3 center = start + (end - start) * t;
    float r = r0 + (r1 - r0) * t;
    for (int j = 0; j <= segs; ++j) {
      float angle = (float)j / segs * 2.0f * (float)M_PI;
      glm::vec3 localDir = right * cosf(angle) + fwd * sinf(angle);
      glm::vec3 pos = center + localDir * r;
      glm::vec3 norm = glm::normalize(localDir);
      float u = (float)j / segs;
      vertices.push_back(pos.x);  vertices.push_back(pos.y);
      vertices.push_back(pos.z);  vertices.push_back(norm.x);
      vertices.push_back(norm.y); vertices.push_back(norm.z);
      vertices.push_back(u);      vertices.push_back(t);
    }
  }
  for (int j = 0; j < segs; ++j) {
    unsigned int a = base + j,          b = base + j + 1;
    unsigned int c = base + (segs+1)+j, d = base + (segs+1)+j+1;
    indices.push_back(a); indices.push_back(c); indices.push_back(b);
    indices.push_back(b); indices.push_back(c); indices.push_back(d);
  }
}

// Generate a UV-sphere for foliage clusters.
// Vertex layout: pos(3) + normal(3) + uv(2) = 8 floats
static void generateFoliageSphere(std::vector<float> &vertices,
                                  std::vector<unsigned int> &indices,
                                  glm::vec3 center, float radius,
                                  int stacks = 7, int slices = 10) {
  unsigned int base = (unsigned int)(vertices.size() / 8);
  for (int i = 0; i <= stacks; ++i) {
    float phi = (float)M_PI * i / stacks - (float)M_PI * 0.5f;
    float v   = (float)i / stacks;
    for (int j = 0; j <= slices; ++j) {
      float theta = 2.0f * (float)M_PI * j / slices;
      float u     = (float)j / slices;
      glm::vec3 norm = glm::normalize(glm::vec3(cosf(phi) * cosf(theta),
                                                sinf(phi),
                                                cosf(phi) * sinf(theta)));
      glm::vec3 pos = center + norm * radius;
      vertices.push_back(pos.x);  vertices.push_back(pos.y);
      vertices.push_back(pos.z);  vertices.push_back(norm.x);
      vertices.push_back(norm.y); vertices.push_back(norm.z);
      vertices.push_back(u);      vertices.push_back(v);
    }
  }
  for (int i = 0; i < stacks; ++i) {
    for (int j = 0; j < slices; ++j) {
      unsigned int a = base + i     * (slices+1) + j;
      unsigned int b = base + (i+1) * (slices+1) + j;
      unsigned int c = base + i     * (slices+1) + j + 1;
      unsigned int d = base + (i+1) * (slices+1) + j + 1;
      indices.push_back(a); indices.push_back(b); indices.push_back(c);
      indices.push_back(c); indices.push_back(b); indices.push_back(d);
    }
  }
}

// Combined fractal tree generator: builds branch cylinders and foliage spheres
// in a single traversal so that both use the exact same random sequence.
// Every leaf branch (depth==1) is guaranteed a foliage sphere at its tip.
static void generateFractalTree(
    std::vector<float> &bVerts, std::vector<unsigned int> &bInds,
    std::vector<float> &fVerts, std::vector<unsigned int> &fInds,
    glm::vec3 start, glm::vec3 direction,
    float length, float radius, int depth) {
  if (depth <= 0 || length < 0.008f || radius < 0.003f) return;

  glm::vec3 end = start + direction * length;
  float endRadius = radius * 0.65f;
  addCylinderSegment(bVerts, bInds, start, end, radius, endRadius, 8);

  if (depth == 1) {
    // Leaf branch tip: guaranteed sphere, smaller random size [0.50, 1.10]
    float szR = 0.7f + static_cast<float>(rand()) / (float)RAND_MAX * 2.00f;
    float sphereR = (0.07f + endRadius * 2.2f) * szR;
    generateFoliageSphere(fVerts, fInds, end, sphereR);
    return;
  }

  glm::vec3 perp1, perp2;
  buildBasis(direction, perp1, perp2);

  const int numBranches = 4;
  for (int i = 0; i < numBranches; ++i) {
    float baseAzimuth = (float)i / numBranches * 2.0f * (float)M_PI;
    float jitter      = ((float)(rand() % 100) / 100.0f - 0.5f) * (float)M_PI * 0.6f;
    float phi = baseAzimuth + jitter;

    glm::vec3 deflectAxis = glm::normalize(perp1 * cosf(phi) + perp2 * sinf(phi));
    float deflect = glm::radians(20.0f + (float)(rand() % 30));

    glm::mat4 rot = glm::rotate(glm::mat4(1.0f), deflect, deflectAxis);
    glm::vec3 newDir =
        glm::normalize(glm::vec3(rot * glm::vec4(direction, 0.0f)));

    float lf = 0.55f + static_cast<float>(rand()) / (float)RAND_MAX * 0.20f;
    generateFractalTree(bVerts, bInds, fVerts, fInds,
                        end, newDir, length * lf, endRadius, depth - 1);
  }
}
// ─────────────────────────────────────────────────────────────────────────────

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace std;

void framebuffer_size_callback(GLFWwindow *window, int width, int height);
void mouse_callback(GLFWwindow *window, double xpos, double ypos);
void scroll_callback(GLFWwindow *window, double xoffset, double yoffset);
void processInput(GLFWwindow *window);
void lightEffect(unsigned int VAO, Shader lightShader, glm::mat4 model,
                 glm::vec3 color);
void drawCube(unsigned int VAO, Shader shader, glm::mat4 model,
              glm::vec4 color);
void key_callback(GLFWwindow *window, int key, int scancode, int action,
                  int mods);
unsigned int loadTexture(char const *path, GLenum textureWrappingModeS,
                         GLenum textureWrappingModeT,
                         GLenum textureFilteringModeMin,
                         GLenum textureFilteringModeMax);
void load_texture(unsigned int &texture, string image_name, GLenum format);
void read_file(string file_name, vector<float> &vec);

// draw object functions
// void drawCube(Shader shaderProgram, unsigned int VAO, glm::mat4 parentTrans,
// float posX = 0.0, float posY = 0.0, float posz = 0.0, float rotX = 0.0, float
// rotY = 0.0, float rotZ = 0.0, float scX = 1.0, float scY = 1.0, float scZ
// = 1.0);

// settings
const unsigned int SCR_WIDTH = 800;
const unsigned int SCR_HEIGHT = 600;

// modelling transform
float rotateAngle_X = 45.0;
float rotateAngle_Y = 45.0;
float rotateAngle_Z = 45.0;
float rotateAxis_X = 0.0;
float rotateAxis_Y = 0.0;
float rotateAxis_Z = 1.0;
float translate_X = 0.0;
float translate_Y = 0.0;
float translate_Z = 0.0;
float scale_X = 1.0;
float scale_Y = 1.0;
float scale_Z = 1.0;

//// camera
// float lastX = SCR_WIDTH / 2.0f;
// float lastY = SCR_HEIGHT / 2.0f;
bool firstMouse = true;

// camera
Camera camera(glm::vec3(2.0f, 5.0f, 10.0f));
float lastX = SCR_WIDTH / 2.0f;
float lastY = SCR_HEIGHT / 2.0f;

float eyeX = 1.35, eyeY = 4.8, eyeZ = 10.0;
float lookAtX = 4.0, lookAtY = 4.0, lookAtZ = 6.0;
glm::vec3 V = glm::vec3(0.0f, 1.0f, 0.0f);
BasicCamera basic_camera(eyeX, eyeY, eyeZ, lookAtX, lookAtY, lookAtZ, V);

// timing
float deltaTime = 0.0f; // time between current frame and last frame
float lastFrame = 0.0f;

// ── Ballista projectile (physics-based arrow) ──────────────────────────────
bool      arrowActive   = false;
glm::vec3 arrowPos      = glm::vec3(0.0f);
glm::vec3 arrowVelocity = glm::vec3(0.0f);
const float ARROW_SPEED   = 20.0f;  // world units / second
const float ARROW_GRAVITY = -12.0f; // world units / second²
// ── Ballista arm animation ──────────────────────────────────────────────────
float ballistaAnimT    = 0.0f;   // seconds since last fire
bool  ballistaAnimating = false;
// ───────────────────────────────────────────────────────────────────────────

bool on = false;

// birds eye
bool birdEye = false;
glm::vec3 cameraPos(-2.0f, 5.0f, 13.0f);
glm::vec3 target(-2.0f, 0.0f, 5.5f);
float birdEyeSpeed = 1.0f;

// positions of the point lights
glm::vec3 pointLightPositions[] = {glm::vec3(-6.0f, 1.5f, -5.0f),
                                   glm::vec3(0.0f, 1.5f, -5.0f),
                                   glm::vec3(-2.5f, 1.5f, -15.0f)

};

PointLight pointlight1(

    pointLightPositions[0].x, pointLightPositions[0].y,
    pointLightPositions[0].z, // position
    0.05f, 0.05f, 0.05f,      // ambient
    1.0f, 1.0f, 1.0f,         // diffuse
    1.0f, 1.0f, 1.0f,         // specular
    1.0f,                     // k_c
    0.09f,                    // k_l
    0.032f,                   // k_q
    1                         // light number
);
PointLight pointlight2(

    pointLightPositions[1].x, pointLightPositions[1].y,
    pointLightPositions[1].z, // position
    0.05f, 0.05f, 0.05f,      // ambient
    1.0f, 1.0f, 1.0f,         // diffuse
    1.0f, 1.0f, 1.0f,         // specular
    1.0f,                     // k_c
    0.09f,                    // k_l
    0.032f,                   // k_q
    2                         // light number
);

PointLight pointlight3(

    pointLightPositions[2].x, pointLightPositions[2].y,
    pointLightPositions[2].z, // position
    0.05f, 0.05f, 0.05f,      // ambient
    1.0f, 1.0f, 1.0f,         // diffuse
    1.0f, 1.0f, 1.0f,         // specular
    1.0f,                     // k_c
    0.09f,                    // k_l
    0.032f,                   // k_q
    3                         // light number
);

SpotLight spotlight1(6.5f, 3.5f, 6.0f,              // position
                     1.0f, 1.0f, 1.0f,              // ambient
                     1.0f, 1.0f, 1.0f,              // diffuse
                     1.0f, 1.0f, 1.0f,              // specular
                     1.0f,                          // k_c
                     0.09f,                         // k_l
                     0.032f,                        // k_q
                     1,                             // light number
                     glm::cos(glm::radians(20.5f)), // cutoff
                     0, -1, 0);

SpotLight spotlight2(-12.5f, 3.5f, 6.0f,            // position
                     1.0f, 1.0f, 1.0f,              // ambient
                     1.0f, 1.0f, 1.0f,              // diffuse
                     1.0f, 1.0f, 1.0f,              // specular
                     1.0f,                          // k_c
                     0.09f,                         // k_l
                     0.032f,                        // k_q
                     2,                             // light number
                     glm::cos(glm::radians(20.5f)), // cutoff
                     0, -1, 0);

// light settings
bool onOffToggle = true;
bool ambientToggle = true;
bool diffuseToggle = true;
bool specularToggle = true;
bool dl = true;
bool spt = true;
bool point1 = true;
bool point2 = true;
bool point3 = true;

// float d_amb_on = 1.0f;
// float d_def_on = 1.0f;
// float d_spec_on = 1.0f;

glm::vec3 amb(0.2f, 0.2f, 0.2f);
glm::vec3 def(0.8f, 0.8f, 0.8f);
glm::vec3 spec(1.0f, 1.0f, 1.0f);

float fov = glm::radians(camera.Zoom);
float aspect = (float)SCR_WIDTH / (float)SCR_HEIGHT;
float near = 0.1f;
float far = 100.0f;

float tanHalfFOV = tan(fov / 2.0f);

// texture
float extra = 4.0f;
float TXmin = 0.0f;
float TXmax = 1.0f;
float TYmin = 0.0f;
float TYmax = 1.0f;

// doors
bool openDoor = true;
float doorAngle = 90.0f;

bool texture_bool = true;

int colorMode = 2;       // 0=object, 1=tex, 2=mix
bool useGouraud = false; // toggle for shading model

float rightGunRotationAngle =
    0.0f; // Initial rotation angle for the third object
float rightBaseRotationAngle = 0.0f; // Initial rotation angle for both objects

bool rightGunRotation = false;
bool rightBaseRotation = false;

float leftBaseRotationAngle = 0.0f; // Initial rotation angle for both objects
float leftGunRotationAngle = 0.0f;

float t = 0.0f;      // Initial parameter for the parabolic path
float tSpeed = 0.3f; // Speed of the parameter change

// Diagonal translation speed
float translationSpeed = 3.0f;
glm::vec3 objectPosition1 = glm::vec3(-6.4, -0.45, 2.7); // Initial position
glm::vec3 objectPosition2 = glm::vec3(-6.7, -0.45, 2.75);
glm::vec3 objectPosition3 = glm::vec3(-6.65, -0.47, 3.0);
glm::vec3 objectPosition4 = glm::vec3(-6.4, -0.47, 3.0);

bool launched1 = false;
bool launched2 = false;
bool launched3 = false;
bool launched4 = false;

float launchAngle1 = 0.0f;
float launchAngle2 = 0.0f;
float launchAngle3 = 0.0f;
float launchAngle4 = 0.0f;

// ── Missile firing physics (objectPosition space) ────────────────────────
// Barrel tilts 45° around X, so world -Y gravity maps to obj (0, -g/√2, +g/√2)
bool      mslFiring      = false;
float     mslFiringTimer = 0.0f;
glm::vec3 mslVel1(0.0f), mslVel2(0.0f), mslVel3(0.0f), mslVel4(0.0f);
const float MSL_LAUNCH_V   = 16.0f;         // initial speed along barrel
const float MSL_G_OBJ      = 9.8f * 0.7071f; // 9.8/√2 ≈ 6.93
const float MSL_RESET_TIME = 3.5f;           // seconds until missiles return

// Function to draw a sphere
void drawSphere(unsigned int &VAO_S, Shader &lightingShader, glm::vec3 color,
                glm::mat4 model, std::vector<unsigned int> &indices) {
  lightingShader.use();

  // Setting up materialistic property
  lightingShader.setVec3("material.ambient", color);
  lightingShader.setVec3("material.diffuse", color);
  lightingShader.setVec3("material.specular", color);
  lightingShader.setFloat("material.shininess", 32.0f);
  // float emissiveIntensity = 0.05f; // Adjust this value to increase or
  // decrease the intensity glm::vec3 emissiveColor = glm::vec3(1.0f, 0.0f,
  // 0.0f) * emissiveIntensity;

  // lightingShader.setVec3("material.emissive", emissiveColor);

  lightingShader.setMat4("model", model);

  glBindVertexArray(VAO_S);
  glDrawElements(GL_TRIANGLES, indices.size(), GL_UNSIGNED_INT, 0);
  glBindVertexArray(0);
}

int main() {
  // glfw: initialize and configure
  // ------------------------------
  glfwInit();
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

#ifdef __APPLE__
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

  // glfw window creation
  // --------------------
  GLFWwindow *window =
      glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT,
                       "CSE 4208: Computer Graphics Laboratory", NULL, NULL);
  if (window == NULL) {
    std::cout << "Failed to create GLFW window" << std::endl;
    glfwTerminate();
    return -1;
  }
  glfwMakeContextCurrent(window);
  glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
  glfwSetKeyCallback(window, key_callback);
  glfwSetKeyCallback(window, key_callback);
  // glfwSetCursorPosCallback(window, mouse_callback);
  glfwSetScrollCallback(window, scroll_callback);

  // tell GLFW to capture our mouse
  // glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

  // glad: load all OpenGL function pointers
  // ---------------------------------------
  if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
    std::cout << "Failed to initialize GLAD" << std::endl;
    return -1;
  }

  // configure global opengl state
  // -----------------------------
  glEnable(GL_DEPTH_TEST);

  // build and compile our shader zprogram
  // ------------------------------------
  Shader ourShader("src/vertexShader.vs", "src/fragmentShader.fs");

  // Shader constantShader("vertexShader.vs", "fragmentShaderV2.fs");

  // Shader lightingShader("vertexShaderForGouraudShading.vs",
  // "fragmentShaderForGouraudShading.fs");
  Shader phongShaderWithTexture(
      "src/vertexShaderForPhongShadingWithTexture.vs",
      "src/fragmentShaderForPhongShadingWithTexture.fs");
  Shader gouraudShaderWithTexture("src/vertexShaderForGouraudShading.vs",
                                  "src/fragmentShaderForGouraudShading.fs");
  Shader textureShader("src/texture_vertex.vs", "src/texture_fragment.fs");
  Shader lightingShader("src/vertexShaderForPhongShading.vs",
                        "src/fragmentShaderForPhongShading.fs");

  float cube_vertices[] = {
      // positions      // normals         // texture coords
      0.0f, 0.0f, 0.0f, 0.0f,  0.0f,  -1.0f, TXmax, TYmin,
      1.0f, 0.0f, 0.0f, 0.0f,  0.0f,  -1.0f, TXmin, TYmin,
      1.0f, 1.0f, 0.0f, 0.0f,  0.0f,  -1.0f, TXmin, TYmax,
      0.0f, 1.0f, 0.0f, 0.0f,  0.0f,  -1.0f, TXmax, TYmax,

      1.0f, 0.0f, 0.0f, 1.0f,  0.0f,  0.0f,  TXmax, TYmin,
      1.0f, 1.0f, 0.0f, 1.0f,  0.0f,  0.0f,  TXmax, TYmax,
      1.0f, 0.0f, 1.0f, 1.0f,  0.0f,  0.0f,  TXmin, TYmin,
      1.0f, 1.0f, 1.0f, 1.0f,  0.0f,  0.0f,  TXmin, TYmax,

      0.0f, 0.0f, 1.0f, 0.0f,  0.0f,  1.0f,  TXmin, TYmin,
      1.0f, 0.0f, 1.0f, 0.0f,  0.0f,  1.0f,  TXmax, TYmin,
      1.0f, 1.0f, 1.0f, 0.0f,  0.0f,  1.0f,  TXmax, TYmax,
      0.0f, 1.0f, 1.0f, 0.0f,  0.0f,  1.0f,  TXmin, TYmax,

      0.0f, 0.0f, 1.0f, -1.0f, 0.0f,  0.0f,  TXmax, TYmin,
      0.0f, 1.0f, 1.0f, -1.0f, 0.0f,  0.0f,  TXmax, TYmax,
      0.0f, 1.0f, 0.0f, -1.0f, 0.0f,  0.0f,  TXmin, TYmax,
      0.0f, 0.0f, 0.0f, -1.0f, 0.0f,  0.0f,  TXmin, TYmin,

      1.0f, 1.0f, 1.0f, 0.0f,  1.0f,  0.0f,  TXmax, TYmin,
      1.0f, 1.0f, 0.0f, 0.0f,  1.0f,  0.0f,  TXmax, TYmax,
      0.0f, 1.0f, 0.0f, 0.0f,  1.0f,  0.0f,  TXmin, TYmax,
      0.0f, 1.0f, 1.0f, 0.0f,  1.0f,  0.0f,  TXmin, TYmin,

      0.0f, 0.0f, 0.0f, 0.0f,  -1.0f, 0.0f,  TXmin, TYmin,
      1.0f, 0.0f, 0.0f, 0.0f,  -1.0f, 0.0f,  TXmax, TYmin,
      1.0f, 0.0f, 1.0f, 0.0f,  -1.0f, 0.0f,  TXmax, TYmax,
      0.0f, 0.0f, 1.0f, 0.0f,  -1.0f, 0.0f,  TXmin, TYmax};
  unsigned int cube_indices[] = {0,  3,  2,  2,  1,  0,

                                 4,  5,  7,  7,  6,  4,

                                 8,  9,  10, 10, 11, 8,

                                 12, 13, 14, 14, 15, 12,

                                 16, 17, 18, 18, 19, 16,

                                 20, 21, 22, 22, 23, 20};
  unsigned int cubeVAO, cubeVBO, cubeEBO;
  glGenVertexArrays(1, &cubeVAO);
  glGenBuffers(1, &cubeVBO);
  glGenBuffers(1, &cubeEBO);

  glBindVertexArray(cubeVAO);

  glBindBuffer(GL_ARRAY_BUFFER, cubeVBO);
  glBufferData(GL_ARRAY_BUFFER, sizeof(cube_vertices), cube_vertices,
               GL_STATIC_DRAW);

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cubeEBO);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(cube_indices), cube_indices,
               GL_STATIC_DRAW);

  // position attribute
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(0);

  // vertex normal attribute
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                        (void *)12);
  glEnableVertexAttribArray(1);

  // texture attribute
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                        (void *)24);
  glEnableVertexAttribArray(2);

  // light's VAO
  unsigned int lightCubeVAO;
  glGenVertexArrays(1, &lightCubeVAO);
  glBindVertexArray(lightCubeVAO);

  glBindBuffer(GL_ARRAY_BUFFER, cubeVBO);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cubeEBO);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(0);

  float prism_vertices[] = {
      // Triangular End 1 (e.g., left end)
      // Positions        // Normals             // Texture Coords
      0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, TXmin, TYmin, // Vertex 0
      1.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, TXmax, TYmin, // Vertex 1
      0.0f, 1.0f, 0.0f, 0.0f, 0.0f, -1.0f, TXmin, TYmax, // Vertex 2

      // Triangular End 2 (e.g., right end)
      // Positions        // Normals             // Texture Coords
      0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, TXmin, TYmin, // Vertex 3
      1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, TXmax, TYmin, // Vertex 4
      0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, TXmin, TYmax, // Vertex 5

      // Rectangular Side 1 (Bottom Face)
      // Positions        // Normals             // Texture Coords
      0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, TXmin, TYmin, // Vertex 6
      1.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, TXmax, TYmin, // Vertex 7
      1.0f, 0.0f, 1.0f, 0.0f, -1.0f, 0.0f, TXmax, TYmax, // Vertex 8
      0.0f, 0.0f, 1.0f, 0.0f, -1.0f, 0.0f, TXmin, TYmax, // Vertex 9

      // Rectangular Side 2 (Vertical Face Adjacent to Y-axis)
      // Positions        // Normals             // Texture Coords
      0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, TXmax, TYmin, // Vertex 10
      0.0f, 1.0f, 0.0f, -1.0f, 0.0f, 0.0f, TXmax, TYmax, // Vertex 11
      0.0f, 1.0f, 1.0f, -1.0f, 0.0f, 0.0f, TXmin, TYmax, // Vertex 12
      0.0f, 0.0f, 1.0f, -1.0f, 0.0f, 0.0f, TXmin, TYmin, // Vertex 13

      // Rectangular Side 3 (Hypotenuse Face)
      // Positions        // Normals             // Texture Coords
      0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, TXmax, TYmin, // Vertex 14
      1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, TXmax, TYmax, // Vertex 15
      1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, TXmin, TYmax, // Vertex 16
      0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, TXmin, TYmin, // Vertex 17
  };

  unsigned int prism_indices[] = {
      // Triangular End 1
      0, 1, 2,

      // Triangular End 2
      3, 5, 4,

      // Rectangular Side 1 (Bottom Face)
      6, 7, 8, 8, 9, 6,

      // Rectangular Side 2 (Vertical Face Adjacent to Y-axis)
      10, 11, 12, 12, 13, 10,

      // Rectangular Side 3 (Hypotenuse Face)
      14, 15, 16, 16, 17, 14};

  unsigned int VAO_P, VBO_P, EBO_P;
  glGenVertexArrays(1, &VAO_P);
  glGenBuffers(1, &VBO_P);
  glGenBuffers(1, &EBO_P);

  glBindVertexArray(VAO_P);

  // Vertex Buffer
  glBindBuffer(GL_ARRAY_BUFFER, VBO_P);
  glBufferData(GL_ARRAY_BUFFER, sizeof(prism_vertices), prism_vertices,
               GL_STATIC_DRAW);

  // Element Buffer
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO_P);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(prism_indices), prism_indices,
               GL_STATIC_DRAW);

  // Position Attribute
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(0);

  // Normal Attribute
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                        (void *)(3 * sizeof(float)));
  glEnableVertexAttribArray(1);

  // Texture Coord Attribute
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                        (void *)(6 * sizeof(float)));
  glEnableVertexAttribArray(2);

  // Unbind VAO
  glBindVertexArray(0);

  // glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

  // ourShader.use();
  // constantShader.use();

  std::vector<glm::vec3> tvertices;
  std::vector<std::vector<int>> tfaces;

  // Load OBJ file
  parseOBJ("src/my.txt", tvertices, tfaces);

  // Compute normals and create vertex data array
  std::vector<float> vertexData = computeNormals(tvertices, tfaces);

  // Flatten faces into a single indices vector
  std::vector<unsigned int> tindices;
  for (const auto &face : tfaces) {
    for (int index : face) {
      tindices.push_back(static_cast<unsigned int>(index)); // Corrected
    }
  }

  // OpenGL Buffer Setup
  unsigned int VBOt, VAOt, EBOt;
  glGenVertexArrays(1, &VAOt);
  glGenBuffers(1, &VBOt);
  glGenBuffers(1, &EBOt);

  glBindVertexArray(VAOt);

  // VBO for vertex data (positions and normals are precomputed in vertexData)
  glBindBuffer(GL_ARRAY_BUFFER, VBOt);
  glBufferData(GL_ARRAY_BUFFER, vertexData.size() * sizeof(float),
               vertexData.data(), GL_STATIC_DRAW);

  // EBO for face indices
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBOt);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, tindices.size() * sizeof(unsigned int),
               tindices.data(), GL_STATIC_DRAW);

  // Position attribute
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(0);

  // Normal attribute
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                        (void *)(3 * sizeof(float)));
  glEnableVertexAttribArray(1);

  // Unbind VAO (optional for safety)
  glBindVertexArray(0);

  //------------------------------------------------------------------texture
  // test-------------

  std::vector<glm::vec3> tt_vertices;
  std::vector<std::vector<int>> tt_faces;

  // Load OBJ file
  parseOBJ("src/my.txt", tt_vertices, tt_faces);

  // Compute normals and create vertex data array
  std::vector<float> t_vertexData =
      computeNormalsAndGeneratedUV(tt_vertices, tt_faces);

  // Flatten faces into a single indices vector
  std::vector<unsigned int> tt_indices;
  for (const auto &face : tt_faces) {
    for (int index : face) {
      tt_indices.push_back(static_cast<unsigned int>(index)); // Corrected
    }
  }

  // OpenGL Buffer Setup
  unsigned int VBOtt, VAOtt, EBOtt;
  glGenVertexArrays(1, &VAOtt);
  glGenBuffers(1, &VBOtt);
  glGenBuffers(1, &EBOtt);

  glBindVertexArray(VAOtt);

  // VBO for vertex data (positions and normals are precomputed in vertexData)
  glBindBuffer(GL_ARRAY_BUFFER, VBOtt);
  glBufferData(GL_ARRAY_BUFFER, t_vertexData.size() * sizeof(float),
               t_vertexData.data(), GL_STATIC_DRAW);

  // EBO for face indices
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBOtt);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER,
               tt_indices.size() * sizeof(unsigned int), tt_indices.data(),
               GL_STATIC_DRAW);

  // Position attribute: 3 floats (x, y, z)
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(0);

  // Normal attribute: 3 floats (nx, ny, nz)
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                        (void *)(3 * sizeof(float)));
  glEnableVertexAttribArray(1);

  // Texture Coord Attribute: 2 floats (u, v)
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                        (void *)(6 * sizeof(float)));
  glEnableVertexAttribArray(2);

  // Unbind VAO
  glBindVertexArray(0);

  // Unbind VAO (optional for safety)
  glBindVertexArray(0);

  // Generate sphere data
  std::vector<float> vertices_s;
  std::vector<unsigned int> indices_s;
  generateSphere(1.0f, 72, 72, vertices_s, indices_s);

  // Create VAO_S, VBO_S, and EBO_S
  unsigned int VAO_S, VBO_S, EBO_S;
  glGenVertexArrays(1, &VAO_S);
  glGenBuffers(1, &VBO_S);
  glGenBuffers(1, &EBO_S);

  // Bind VAO
  glBindVertexArray(VAO_S);

  // Bind and set VBO
  glBindBuffer(GL_ARRAY_BUFFER, VBO_S);
  glBufferData(GL_ARRAY_BUFFER, vertices_s.size() * sizeof(float),
               vertices_s.data(), GL_STATIC_DRAW);

  // Bind and set EBO
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO_S);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices_s.size() * sizeof(unsigned int),
               indices_s.data(), GL_STATIC_DRAW);

  // Position attribute
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(0);

  // Normal attribute
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                        (void *)(3 * sizeof(float)));
  glEnableVertexAttribArray(1);

  // Unbind VAO
  glBindVertexArray(0);

  //------------------------------------------------------------------

  std::vector<glm::vec3> rbvertices;
  std::vector<std::vector<int>> rbfaces;

  // Load OBJ file
  parseOBJ("src/ram_main.txt", rbvertices, rbfaces);

  // Compute normals and create vertex data array
  std::vector<float> rbvertexData = computeNormals(rbvertices, rbfaces);

  // Flatten faces into a single indices vector
  std::vector<unsigned int> rbindices;
  for (const auto &face : rbfaces) {
    for (int index : face) {
      rbindices.push_back(static_cast<unsigned int>(index)); // Corrected
    }
  }

  // OpenGL Buffer Setup
  unsigned int VBOrb, VAOrb, EBOrb;
  glGenVertexArrays(1, &VAOrb);
  glGenBuffers(1, &VBOrb);
  glGenBuffers(1, &EBOrb);

  glBindVertexArray(VAOrb);

  // VBO for vertex data (positions and normals are precomputed in vertexData)
  glBindBuffer(GL_ARRAY_BUFFER, VBOrb);
  glBufferData(GL_ARRAY_BUFFER, rbvertexData.size() * sizeof(float),
               rbvertexData.data(), GL_STATIC_DRAW);

  // EBO for face indices
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBOrb);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, rbindices.size() * sizeof(unsigned int),
               rbindices.data(), GL_STATIC_DRAW);

  // Position attribute
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(0);

  // Normal attribute
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                        (void *)(3 * sizeof(float)));
  glEnableVertexAttribArray(1);

  // Unbind VAO (optional for safety)
  glBindVertexArray(0);

  //------------------------------------------------------------------

  std::vector<glm::vec3> rbbvertices;
  std::vector<std::vector<int>> rbbfaces;

  // Load OBJ file
  parseOBJ("src/ram_base.txt", rbbvertices, rbbfaces);

  // Compute normals and create vertex data array
  std::vector<float> rbbvertexData = computeNormals(rbbvertices, rbbfaces);

  // Flatten faces into a single indices vector
  std::vector<unsigned int> rbbindices;
  for (const auto &face : rbbfaces) {
    for (int index : face) {
      rbbindices.push_back(static_cast<unsigned int>(index)); // Corrected
    }
  }

  // OpenGL Buffer Setup
  unsigned int VBOrbb, VAOrbb, EBOrbb;
  glGenVertexArrays(1, &VAOrbb);
  glGenBuffers(1, &VBOrbb);
  glGenBuffers(1, &EBOrbb);

  glBindVertexArray(VAOrbb);

  // VBO for vertex data (positions and normals are precomputed in vertexData)
  glBindBuffer(GL_ARRAY_BUFFER, VBOrbb);
  glBufferData(GL_ARRAY_BUFFER, rbbvertexData.size() * sizeof(float),
               rbbvertexData.data(), GL_STATIC_DRAW);

  // EBO for face indices
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBOrbb);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER,
               rbbindices.size() * sizeof(unsigned int), rbbindices.data(),
               GL_STATIC_DRAW);

  // Position attribute
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(0);

  // Normal attribute
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                        (void *)(3 * sizeof(float)));
  glEnableVertexAttribArray(1);

  // Unbind VAO (optional for safety)
  glBindVertexArray(0);

  //------------------------------------------------------------------

  std::vector<glm::vec3> rbgvertices;
  std::vector<std::vector<int>> rbgfaces;

  // Load OBJ file
  parseOBJ("src/ram_gun.txt", rbgvertices, rbgfaces);

  // Compute normals and create vertex data array
  std::vector<float> rbgvertexData = computeNormals(rbgvertices, rbgfaces);

  // Flatten faces into a single indices vector
  std::vector<unsigned int> rbgindices;
  for (const auto &face : rbgfaces) {
    for (int index : face) {
      rbgindices.push_back(static_cast<unsigned int>(index)); // Corrected
    }
  }

  // OpenGL Buffer Setup
  unsigned int VBOrbg, VAOrbg, EBOrbg;
  glGenVertexArrays(1, &VAOrbg);
  glGenBuffers(1, &VBOrbg);
  glGenBuffers(1, &EBOrbg);

  glBindVertexArray(VAOrbg);

  // VBO for vertex data (positions and normals are precomputed in vertexData)
  glBindBuffer(GL_ARRAY_BUFFER, VBOrbg);
  glBufferData(GL_ARRAY_BUFFER, rbgvertexData.size() * sizeof(float),
               rbgvertexData.data(), GL_STATIC_DRAW);

  // EBO for face indices
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBOrbg);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER,
               rbgindices.size() * sizeof(unsigned int), rbgindices.data(),
               GL_STATIC_DRAW);

  // Position attribute
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(0);

  // Normal attribute
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                        (void *)(3 * sizeof(float)));
  glEnableVertexAttribArray(1);

  // Unbind VAO (optional for safety)
  glBindVertexArray(0);

  //-------------------------------------------pot-------------------
  std::vector<glm::vec3> potvertices;
  std::vector<std::vector<int>> potfaces;

  // Load OBJ file
  parseOBJ("src/pot.txt", potvertices, potfaces);

  // Compute normals and create vertex data array
  std::vector<float> potvertexData =
      computeNormalsAndGeneratedUV(potvertices, potfaces);

  // Flatten faces into a single indices vector
  std::vector<unsigned int> potindices;
  for (const auto &face : potfaces) {
    for (int index : face) {
      potindices.push_back(static_cast<unsigned int>(index)); // Corrected
    }
  }

  // OpenGL Buffer Setup
  unsigned int VBOpot, VAOpot, EBOpot;
  glGenVertexArrays(1, &VAOpot);
  glGenBuffers(1, &VBOpot);
  glGenBuffers(1, &EBOpot);

  glBindVertexArray(VAOpot);

  // VBO for vertex data (positions and normals are precomputed in vertexData)
  glBindBuffer(GL_ARRAY_BUFFER, VBOpot);
  glBufferData(GL_ARRAY_BUFFER, potvertexData.size() * sizeof(float),
               potvertexData.data(), GL_STATIC_DRAW);

  // EBO for face indices
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBOpot);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER,
               potindices.size() * sizeof(unsigned int), potindices.data(),
               GL_STATIC_DRAW);

  // Position attribute: 3 floats (x, y, z)
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(0);

  // Normal attribute: 3 floats (nx, ny, nz)
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                        (void *)(3 * sizeof(float)));
  glEnableVertexAttribArray(1);

  // Texture Coord Attribute: 2 floats (u, v)
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                        (void *)(6 * sizeof(float)));
  glEnableVertexAttribArray(2);

  // Unbind VAO
  glBindVertexArray(0);

  // Unbind VAO (optional for safety)
  glBindVertexArray(0);

  // Unbind VAO (optional for safety)
  glBindVertexArray(0);

  //----------------------------solid tree---------------------
  //------------------------------------------------------------------

  std::vector<glm::vec3> trvertices;
  std::vector<std::vector<int>> trfaces;

  // Load OBJ file
  parseOBJ("src/tree.txt", trvertices, trfaces);

  // Compute normals and create vertex data array
  std::vector<float> trvertexData =
      computeNormalsAndGeneratedUV(trvertices, trfaces);

  // Flatten faces into a single indices vector
  std::vector<unsigned int> trindices;
  for (const auto &face : trfaces) {
    for (int index : face) {
      trindices.push_back(static_cast<unsigned int>(index)); // Corrected
    }
  }

  // OpenGL Buffer Setup
  unsigned int VBOtr, VAOtr, EBOtr;
  glGenVertexArrays(1, &VAOtr);
  glGenBuffers(1, &VBOtr);
  glGenBuffers(1, &EBOtr);

  glBindVertexArray(VAOtr);

  // VBO for vertex data (positions and normals are precomputed in vertexData)
  glBindBuffer(GL_ARRAY_BUFFER, VBOtr);
  glBufferData(GL_ARRAY_BUFFER, trvertexData.size() * sizeof(float),
               trvertexData.data(), GL_STATIC_DRAW);

  // EBO for face indices
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBOtr);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, trindices.size() * sizeof(unsigned int),
               trindices.data(), GL_STATIC_DRAW);

  // Position attribute
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(0);

  // Normal attribute
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                        (void *)(3 * sizeof(float)));
  glEnableVertexAttribArray(1);

  // Texture Coord attribute
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                        (void *)(6 * sizeof(float)));
  glEnableVertexAttribArray(2);

  // Unbind VAO (optional for safety)
  glBindVertexArray(0);

  //------------------------------------------------------trunk

  std::vector<glm::vec3> trunkvertices;
  std::vector<std::vector<int>> trunkfaces;

  // Load OBJ file
  parseOBJ("src/trunk.txt", trunkvertices, trunkfaces);

  // Compute normals and create vertex data array
  std::vector<float> trunkvertexData =
      computeNormalsAndCylindricalUV(trunkvertices, trunkfaces);

  // Flatten faces into a single indices vector
  std::vector<unsigned int> trunkindices;
  for (const auto &face : trunkfaces) {
    for (int index : face) {
      trunkindices.push_back(static_cast<unsigned int>(index)); // Corrected
    }
  }

  // OpenGL Buffer Setup
  unsigned int VBOtrunk, VAOtrunk, EBOtrunk;
  glGenVertexArrays(1, &VAOtrunk);
  glGenBuffers(1, &VBOtrunk);
  glGenBuffers(1, &EBOtrunk);

  glBindVertexArray(VAOtrunk);

  // VBO for vertex data (positions and normals are precomputed in vertexData)
  glBindBuffer(GL_ARRAY_BUFFER, VBOtrunk);
  glBufferData(GL_ARRAY_BUFFER, trunkvertexData.size() * sizeof(float),
               trunkvertexData.data(), GL_STATIC_DRAW);

  // EBO for face indices
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBOtrunk);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER,
               trunkindices.size() * sizeof(unsigned int), trunkindices.data(),
               GL_STATIC_DRAW);

  // Position attribute
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(0);

  // Normal attribute
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                        (void *)(3 * sizeof(float)));
  glEnableVertexAttribArray(1);

  // Texture Coord attribute
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                        (void *)(6 * sizeof(float)));
  glEnableVertexAttribArray(2);

  // Unbind VAO (optional for safety)
  glBindVertexArray(0);

  //--------------------------------------------------------------------------rocket

  std::vector<glm::vec3> missilevertices;
  std::vector<std::vector<int>> missilefaces;

  // Load OBJ file
  parseOBJ("src/missile.txt", missilevertices, missilefaces);

  // Compute normals and create vertex data array
  std::vector<float> missilevertexData =
      computeNormals(missilevertices, missilefaces);

  // Flatten faces into a single indices vector
  std::vector<unsigned int> missileindices;
  for (const auto &face : missilefaces) {
    for (int index : face) {
      missileindices.push_back(static_cast<unsigned int>(index)); // Corrected
    }
  }

  // OpenGL Buffer Setup
  unsigned int VBOm, VAOm, EBOm;
  glGenVertexArrays(1, &VAOm);
  glGenBuffers(1, &VBOm);
  glGenBuffers(1, &EBOm);

  glBindVertexArray(VAOm);

  // VBO for vertex data (positions and normals are precomputed in vertexData)
  glBindBuffer(GL_ARRAY_BUFFER, VBOm);
  glBufferData(GL_ARRAY_BUFFER, missilevertexData.size() * sizeof(float),
               missilevertexData.data(), GL_STATIC_DRAW);

  // EBO for face indices
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBOm);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER,
               missileindices.size() * sizeof(unsigned int),
               missileindices.data(), GL_STATIC_DRAW);

  // Position attribute
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(0);

  // Normal attribute
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                        (void *)(3 * sizeof(float)));
  glEnableVertexAttribArray(1);

  // Unbind VAO (optional for safety)
  glBindVertexArray(0);

  // Generate tree vertices and indices
  std::vector<float> treeVertices;
  std::vector<unsigned int> treeIndices;
  generateTree(treeVertices, treeIndices, glm::vec3(0.0f, 0.0f, 0.0f),
               glm::vec3(0.0f, 1.0f, 0.0f), 1.0f, 5,
               10); // Start at origin, direction up, length 1.0, depth 5

  // Create VAO, VBO, and EBO for the tree
  unsigned int treeVAO, treeVBO, treeEBO;
  glGenVertexArrays(1, &treeVAO);
  glGenBuffers(1, &treeVBO);
  glGenBuffers(1, &treeEBO);

  glBindVertexArray(treeVAO);

  glBindBuffer(GL_ARRAY_BUFFER, treeVBO);
  glBufferData(GL_ARRAY_BUFFER, treeVertices.size() * sizeof(float),
               &treeVertices[0], GL_STATIC_DRAW);

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, treeEBO);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER,
               treeIndices.size() * sizeof(unsigned int), &treeIndices[0],
               GL_STATIC_DRAW);

  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                        (void *)(3 * sizeof(float)));
  glEnableVertexAttribArray(1);

  glBindVertexArray(0);

  // --- LARGE FRACTAL TREE SETUP ---
  // Branches: pos(3)+normal(3)+uv(2) = 8 floats per vertex
  std::vector<float> bigTreeVertices;
  std::vector<unsigned int> bigTreeIndices;
  // Foliage spheres: same 8-float layout
  std::vector<float> foliageVertices;
  std::vector<unsigned int> foliageIndices;
  srand(12345);
  generateFractalTree(bigTreeVertices, bigTreeIndices,
                      foliageVertices, foliageIndices,
                      glm::vec3(0.0f, 0.0f, 0.0f),
                      glm::vec3(0.0f, 1.0f, 0.0f), 1.4f, 0.13f, 6);

  unsigned int bigTreeVAO, bigTreeVBO, bigTreeEBO;
  glGenVertexArrays(1, &bigTreeVAO);
  glGenBuffers(1, &bigTreeVBO);
  glGenBuffers(1, &bigTreeEBO);
  glBindVertexArray(bigTreeVAO);
  glBindBuffer(GL_ARRAY_BUFFER, bigTreeVBO);
  glBufferData(GL_ARRAY_BUFFER, bigTreeVertices.size() * sizeof(float),
               bigTreeVertices.data(), GL_STATIC_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, bigTreeEBO);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER,
               bigTreeIndices.size() * sizeof(unsigned int),
               bigTreeIndices.data(), GL_STATIC_DRAW);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                        (void *)(3 * sizeof(float)));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                        (void *)(6 * sizeof(float)));
  glEnableVertexAttribArray(2);
  glBindVertexArray(0);

  unsigned int foliageVAO, foliageVBO, foliageEBO;
  glGenVertexArrays(1, &foliageVAO);
  glGenBuffers(1, &foliageVBO);
  glGenBuffers(1, &foliageEBO);
  glBindVertexArray(foliageVAO);
  glBindBuffer(GL_ARRAY_BUFFER, foliageVBO);
  glBufferData(GL_ARRAY_BUFFER, foliageVertices.size() * sizeof(float),
               foliageVertices.data(), GL_STATIC_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, foliageEBO);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER,
               foliageIndices.size() * sizeof(unsigned int),
               foliageIndices.data(), GL_STATIC_DRAW);
  // pos
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(0);
  // normal
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                        (void *)(3 * sizeof(float)));
  glEnableVertexAttribArray(1);
  // uv
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                        (void *)(6 * sizeof(float)));
  glEnableVertexAttribArray(2);
  glBindVertexArray(0);
  // --- END LARGE FRACTAL TREE SETUP ---

  // --- CONE ROOF SETUP ---
  std::vector<float> coneVertices;
  std::vector<unsigned int> coneIndices;
  generateCone(1.0f, 1.5f, 36, coneVertices, coneIndices);

  unsigned int coneVAO, coneVBO, coneEBO;
  glGenVertexArrays(1, &coneVAO);
  glGenBuffers(1, &coneVBO);
  glGenBuffers(1, &coneEBO);

  glBindVertexArray(coneVAO);
  glBindBuffer(GL_ARRAY_BUFFER, coneVBO);
  glBufferData(GL_ARRAY_BUFFER, coneVertices.size() * sizeof(float),
               coneVertices.data(), GL_STATIC_DRAW);

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, coneEBO);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER,
               coneIndices.size() * sizeof(unsigned int), coneIndices.data(),
               GL_STATIC_DRAW);

  unsigned int coneIndicesCount = (unsigned int)coneIndices.size();

  // Position attribute
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(0);
  // Normal attribute
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                        (void *)(3 * sizeof(float)));
  glEnableVertexAttribArray(1);
  // Texture attribute
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                        (void *)(6 * sizeof(float)));
  glEnableVertexAttribArray(2);

  glBindVertexArray(0);

  // --- BEZIER CURVE SETUP ---
  // Hill
  vector<float> cntrlPoints;
  vector<float> coordinatesHill, normalsHill, verticesHill;
  vector<int> indicesHill;
  unsigned int hillVAO;
  read_file("src/hill.txt", cntrlPoints);
  hillVAO = hollowBezier(
      cntrlPoints.data(), ((unsigned int)cntrlPoints.size() / 3) - 1,
      coordinatesHill, normalsHill, indicesHill, verticesHill, 1.0);

  // Rope
  vector<float> cntrlPointsRope;
  vector<float> coordinatesRope, normalsRope, verticesRope;
  vector<int> indicesRope;
  unsigned int ropeVAO;
  read_file("src/rope_points.txt", cntrlPointsRope);
  ropeVAO = hollowBezier(
      cntrlPointsRope.data(), ((unsigned int)cntrlPointsRope.size() / 3) - 1,
      coordinatesRope, normalsRope, indicesRope, verticesRope, 1.0);

  // Slider (cylinder)
  vector<float> cntrlPointsCylinder;
  vector<float> coordinatesCylinder, normalsCylinder, verticesCylinder;
  vector<int> indicesCylinder;
  unsigned int sliderVAO;
  read_file("src/slider_points.txt", cntrlPointsCylinder);
  sliderVAO = hollowBezier(cntrlPointsCylinder.data(),
                           ((unsigned int)cntrlPointsCylinder.size() / 3) - 1,
                           coordinatesCylinder, normalsCylinder,
                           indicesCylinder, verticesCylinder, 4.0);

  // Pillar (onion dome shape)
  vector<float> cntrlPointsPilar;
  vector<float> coordinatesPillar, normalsPillar, verticesPillar;
  vector<int> indicesPillar;
  unsigned int pillarVAO;
  read_file("src/pillar_points.txt", cntrlPointsPilar);
  pillarVAO = hollowBezier(
      cntrlPointsPilar.data(), ((unsigned int)cntrlPointsPilar.size() / 3) - 1,
      coordinatesPillar, normalsPillar, indicesPillar, verticesPillar, 1.0);

  // Texture loading

  // load_texture(texture1, "grass.jpg", GL_RGBA);

  unsigned int texture, texture2, texture3, texture4, texture5, texture6,
      treeRootTexture, treeFoliageTexture;

  std::cout << "Starting texture loading..." << std::flush << std::endl;

  string ImgPath = "src/dome.png";
  std::cout << "Loading: " << ImgPath << std::flush << std::endl;
  texture = loadTexture(ImgPath.c_str(), GL_REPEAT, GL_REPEAT,
                        GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR);

  ImgPath = "src/tile.png";
  std::cout << "Loading: " << ImgPath << std::flush << std::endl;
  texture2 = loadTexture(ImgPath.c_str(), GL_REPEAT, GL_REPEAT,
                         GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR);

  ImgPath = "src/wood.png";
  std::cout << "Loading: " << ImgPath << std::flush << std::endl;
  texture3 = loadTexture(ImgPath.c_str(), GL_REPEAT, GL_REPEAT,
                         GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR);

  ImgPath = "src/dome.png";
  std::cout << "Loading: " << ImgPath << " (2nd time)" << std::flush
            << std::endl;
  texture4 = loadTexture(ImgPath.c_str(), GL_REPEAT, GL_REPEAT,
                         GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR);

  ImgPath = "src/grass.jpg";
  std::cout << "Loading: " << ImgPath << std::flush << std::endl;
  texture5 = loadTexture(ImgPath.c_str(), GL_REPEAT, GL_REPEAT,
                         GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR);

  ImgPath = "src/tank5.png";
  std::cout << "Loading: " << ImgPath << std::flush << std::endl;
  texture6 = loadTexture(ImgPath.c_str(), GL_REPEAT, GL_REPEAT,
                         GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR);

  ImgPath = "src/TreeRoot.png";
  std::cout << "Loading: " << ImgPath << std::flush << std::endl;
  treeRootTexture = loadTexture(ImgPath.c_str(), GL_REPEAT, GL_REPEAT,
                                GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR);

  ImgPath = "src/TreeTexture - Copy.png";
  std::cout << "Loading: " << ImgPath << std::flush << std::endl;
  treeFoliageTexture = loadTexture(ImgPath.c_str(), GL_REPEAT, GL_REPEAT,
                                   GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR);

  std::cout << "Texture loading complete!" << std::flush << std::endl;

  // White fallback texture for when textures are disabled
  unsigned int whiteTexture;
  {
    unsigned char whitePixel[4] = {255, 255, 255, 255};
    glGenTextures(1, &whiteTexture);
    glBindTexture(GL_TEXTURE_2D, whiteTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 whitePixel);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  }

  float r = 0.0f;

  // render loop
  // -----------
  while (!glfwWindowShouldClose(window)) {
    Shader &lightingShaderWithTexture =
        useGouraud ? gouraudShaderWithTexture : phongShaderWithTexture;
    // per-frame time logic
    // --------------------
    float currentFrame = static_cast<float>(glfwGetTime());
    deltaTime = currentFrame - lastFrame;
    lastFrame = currentFrame;

    // ── Arrow projectile physics ──────────────────────────────────────────
    if (arrowActive) {
        arrowVelocity.y += ARROW_GRAVITY * deltaTime;
        arrowPos        += arrowVelocity  * deltaTime;
        if (arrowPos.y < -2.5f)       // hit floor → deactivate
            arrowActive = false;
    }
    // ── Ballista arm animation timer ──────────────────────────────────────
    if (ballistaAnimating) {
        ballistaAnimT += deltaTime;
        if (ballistaAnimT >= 0.85f) {
            ballistaAnimating = false;
            ballistaAnimT     = 0.0f;
        }
    }
    // ── Missile physics (objectPosition space, barrel +Y = forward) ──────────
    if (mslFiring) {
        mslFiringTimer += deltaTime;
        // gravity in objectPosition space: (0, -g/√2, +g/√2)
        glm::vec3 gravObjStep(0.0f, -MSL_G_OBJ * deltaTime, +MSL_G_OBJ * deltaTime);
        mslVel1 += gravObjStep; mslVel2 += gravObjStep;
        mslVel3 += gravObjStep; mslVel4 += gravObjStep;
        objectPosition1 += mslVel1 * deltaTime;
        objectPosition2 += mslVel2 * deltaTime;
        objectPosition3 += mslVel3 * deltaTime;
        objectPosition4 += mslVel4 * deltaTime;
        // Reset missiles to rest after arc completes
        if (mslFiringTimer >= MSL_RESET_TIME) {
            mslFiring = false;  mslFiringTimer = 0.0f;
            objectPosition1 = glm::vec3(-6.4f, -0.45f, 2.7f);
            objectPosition2 = glm::vec3(-6.7f, -0.45f, 2.75f);
            objectPosition3 = glm::vec3(-6.65f, -0.47f, 3.0f);
            objectPosition4 = glm::vec3(-6.4f, -0.47f, 3.0f);
            mslVel1 = mslVel2 = mslVel3 = mslVel4 = glm::vec3(0.0f);
            launched1 = launched2 = launched3 = launched4 = false;
        }
    }
    // ─────────────────────────────────────────────────────────────────────

    // input
    // -----
    processInput(window);

    // render
    // ------
    glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // pass projection matrix to shader (note that in this case it could change
    // every frame)
    // glm::mat4 projection = glm::perspective(glm::radians(basic_camera.Zoom),
    // (float)SCR_WIDTH / (float)SCR_HEIGHT, 0.1f, 100.0f);
    glm::mat4 projection =
        glm::perspective(glm::radians(camera.Zoom),
                         (float)SCR_WIDTH / (float)SCR_HEIGHT, 0.1f, 100.0f);
    // glm::mat4 projection = glm::ortho(-2.0f, +2.0f, -1.5f, +1.5f, 0.1f,
    // 100.0f); ourShader.setMat4("projection", projection);
    // constantShader.setMat4("projection", projection);

    // glm::mat4 projection(0.0f); // Initialize with zero matrix

    // projection[0][0] = 1.0f / (aspect * tanHalfFOV);
    // projection[1][1] = 1.0f / tanHalfFOV;
    // projection[2][2] = -(far + near) / (far - near);
    // projection[2][3] = -1.0f;
    // projection[3][2] = -(2.0f * far * near) / (far - near);

    // lightingShader.setMat4("projection", projection);

    // camera/view transformation

    glm::mat4 view;

    if (birdEye) {
      glm::vec3 up(0.0f, 1.0f, 0.0f);
      view = glm::lookAt(cameraPos, target, up);
    } else {
      // view = basic_camera.createViewMatrix();
      view = camera.GetViewMatrix();
    }

    glm::mat4 identityMatrix = glm::mat4(1.0f);
    glm::mat4 translateMatrix, rotateXMatrix, rotateYMatrix, rotateZMatrix,
        scaleMatrix, model, modelCentered, translateMatrixprev;

    lightingShaderWithTexture.use();
    lightingShaderWithTexture.setInt("colorMode", colorMode);
    lightingShaderWithTexture.setVec3("objectColor",
                                      glm::vec3(0.52f, 0.37f, 0.26f));
    lightingShaderWithTexture.setVec3("viewPos", camera.Position);
    lightingShaderWithTexture.setMat4("projection", projection);
    lightingShaderWithTexture.setMat4("view", view);

    // point light 1
    pointlight1.setUpPointLight(lightingShaderWithTexture);
    // point light 2
    pointlight2.setUpPointLight(lightingShaderWithTexture);
    // point light 3
    pointlight3.setUpPointLight(lightingShaderWithTexture);

    spotlight1.setUpspotLight(lightingShaderWithTexture);
    spotlight2.setUpspotLight(lightingShaderWithTexture);

    lightingShaderWithTexture.setVec3("directionalLight.direction", 0.0f, -3.0f,
                                      0.0f);
    lightingShaderWithTexture.setVec3("directionalLight.ambient", .2f, .2f,
                                      .2f);
    lightingShaderWithTexture.setVec3("directionalLight.diffuse", .5f, .5f,
                                      .5f);
    lightingShaderWithTexture.setVec3("directionalLight.specular", 1.0f, 1.0f,
                                      1.0f);

    lightingShaderWithTexture.setBool("directionLightOn", dl);

    // floor
    translateMatrix =
        glm::translate(identityMatrix, glm::vec3(-20.0, -2.0, -20.0));
    scaleMatrix = glm::scale(identityMatrix, glm::vec3(30.0, 0.1, 30.0));
    model = translateMatrix * scaleMatrix;
    lightingShaderWithTexture.setInt("material.diffuse", 1);
    lightingShaderWithTexture.setInt("material.specular", 0);
    lightingShaderWithTexture.setFloat("material.shininess", 32.0f);
    lightingShaderWithTexture.setMat4("model", model);
    glActiveTexture(GL_TEXTURE1);
    // Use grass texture for ground (no leafy tile texture)
    glBindTexture(GL_TEXTURE_2D, texture_bool ? texture2 : whiteTexture);
    glBindVertexArray(cubeVAO);
    glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, (void *)0);

    // left wall
    translateMatrix =
        glm::translate(identityMatrix, glm::vec3(-8.25, -2.0, -8.0));
    scaleMatrix = glm::scale(identityMatrix, glm::vec3(0.25, 2.25, 8.0));
    model = translateMatrix * scaleMatrix;
    lightingShaderWithTexture.setInt("material.diffuse", 0);
    lightingShaderWithTexture.setInt("material.specular", 0);
    lightingShaderWithTexture.setFloat("material.shininess", 32.0f);
    lightingShaderWithTexture.setMat4("model", model);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture_bool ? texture : whiteTexture);
    glBindVertexArray(cubeVAO);
    glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, (void *)0);

    // Right wall
    translateMatrix =
        glm::translate(identityMatrix, glm::vec3(2.0, -2.0, -8.0));
    scaleMatrix = glm::scale(identityMatrix, glm::vec3(0.25, 2.25, 8.0));
    model = translateMatrix * scaleMatrix;
    lightingShaderWithTexture.setInt("material.diffuse", 0);
    lightingShaderWithTexture.setInt("material.specular", 0);
    lightingShaderWithTexture.setFloat("material.shininess", 32.0f);
    lightingShaderWithTexture.setMat4("model", model);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture_bool ? texture : whiteTexture);
    glBindVertexArray(cubeVAO);
    glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, (void *)0);

    // back wall
    translateMatrix =
        glm::translate(identityMatrix, glm::vec3(-8.5, -2.0, -7.0));
    scaleMatrix = glm::scale(identityMatrix, glm::vec3(10.0, 2.25, 0.25));
    model = translateMatrix * scaleMatrix;
    lightingShaderWithTexture.setInt("material.diffuse", 0);
    lightingShaderWithTexture.setInt("material.specular", 0);
    lightingShaderWithTexture.setFloat("material.shininess", 32.0f);
    lightingShaderWithTexture.setMat4("model", model);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture_bool ? texture : whiteTexture);
    glBindVertexArray(cubeVAO);
    glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, (void *)0);

    // Front wall-left
    translateMatrix =
        glm::translate(identityMatrix, glm::vec3(-7.5, -2.0, 0.0));
    scaleMatrix = glm::scale(identityMatrix, glm::vec3(3.0, 2.25, 0.25));
    model = translateMatrix * scaleMatrix;
    lightingShaderWithTexture.setInt("material.diffuse", 0);
    lightingShaderWithTexture.setInt("material.specular", 0);
    lightingShaderWithTexture.setFloat("material.shininess", 32.0f);
    lightingShaderWithTexture.setMat4("model", model);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture_bool ? texture : whiteTexture);
    glBindVertexArray(cubeVAO);
    glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, (void *)0);

    // Front wall-right
    translateMatrix =
        glm::translate(identityMatrix, glm::vec3(-1.5, -2.0, 0.0));
    scaleMatrix = glm::scale(identityMatrix, glm::vec3(3.0, 2.25, 0.25));
    model = translateMatrix * scaleMatrix;
    lightingShaderWithTexture.setInt("material.diffuse", 0);
    lightingShaderWithTexture.setInt("material.specular", 0);
    lightingShaderWithTexture.setFloat("material.shininess", 32.0f);
    lightingShaderWithTexture.setMat4("model", model);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture_bool ? texture : whiteTexture);
    glBindVertexArray(cubeVAO);
    glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, (void *)0);

    // Front wall-top
    translateMatrix =
        glm::translate(identityMatrix, glm::vec3(-5.5, -0.5, 0.0));
    scaleMatrix = glm::scale(identityMatrix, glm::vec3(4.0, 0.75, 0.25));
    model = translateMatrix * scaleMatrix;
    lightingShaderWithTexture.setInt("material.diffuse", 0);
    lightingShaderWithTexture.setInt("material.specular", 0);
    lightingShaderWithTexture.setFloat("material.shininess", 32.0f);
    lightingShaderWithTexture.setMat4("model", model);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture_bool ? texture : whiteTexture);
    glBindVertexArray(cubeVAO);
    glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, (void *)0);

    // ---- ONION DOME PILLARS (BEZIER CURVE) ----

    // Pillar 1 (Front-Left)
    translateMatrix =
        glm::translate(identityMatrix, glm::vec3(-7.875, -2.0, 0.0));
    scaleMatrix = glm::scale(identityMatrix, glm::vec3(1.5f, 1.5f, 1.0f));
    model = translateMatrix * scaleMatrix;
    lightingShaderWithTexture.setInt("material.diffuse", 0);
    lightingShaderWithTexture.setInt("material.specular", 0);
    lightingShaderWithTexture.setFloat("material.shininess", 32.0f);
    lightingShaderWithTexture.setMat4("model", model);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture_bool ? texture : whiteTexture);
    glBindVertexArray(pillarVAO);
    glDrawElements(GL_TRIANGLES, (unsigned int)indicesPillar.size(),
                   GL_UNSIGNED_INT, (void *)0);

    // Pillar 2 (Front-Right)
    translateMatrix =
        glm::translate(identityMatrix, glm::vec3(1.875, -2.0, 0.0));
    scaleMatrix = glm::scale(identityMatrix, glm::vec3(1.5f, 1.5f, 1.0f));
    model = translateMatrix * scaleMatrix;
    lightingShaderWithTexture.setMat4("model", model);
    glBindVertexArray(pillarVAO);
    glDrawElements(GL_TRIANGLES, (unsigned int)indicesPillar.size(),
                   GL_UNSIGNED_INT, (void *)0);

    // Pillar 3 (Back-Left)
    translateMatrix =
        glm::translate(identityMatrix, glm::vec3(-7.875, -2.0, -7.0));
    scaleMatrix = glm::scale(identityMatrix, glm::vec3(1.5f, 1.5f, 1.0f));
    model = translateMatrix * scaleMatrix;
    lightingShaderWithTexture.setMat4("model", model);
    glBindVertexArray(pillarVAO);
    glDrawElements(GL_TRIANGLES, (unsigned int)indicesPillar.size(),
                   GL_UNSIGNED_INT, (void *)0);

    // Pillar 4 (Back-Right)
    translateMatrix =
        glm::translate(identityMatrix, glm::vec3(1.875, -2.0, -7.0));
    scaleMatrix = glm::scale(identityMatrix, glm::vec3(1.5f, 1.5f, 1.0f));
    model = translateMatrix * scaleMatrix;
    lightingShaderWithTexture.setMat4("model", model);
    glBindVertexArray(pillarVAO);
    glDrawElements(GL_TRIANGLES, (unsigned int)indicesPillar.size(),
                   GL_UNSIGNED_INT, (void *)0);
    glBindVertexArray(0);

    // left half-door
    translateMatrix =
        glm::translate(identityMatrix, glm::vec3(-4.5, -2.0, 0.0));
    scaleMatrix = glm::scale(identityMatrix, glm::vec3(1.5, 1.5, 0.125));
    rotateYMatrix = glm::rotate(identityMatrix, glm::radians(doorAngle),
                                glm::vec3(0.0f, 1.0f, 0.0f));
    model = translateMatrix * rotateYMatrix * scaleMatrix;
    lightingShaderWithTexture.setInt("material.diffuse", 0);
    lightingShaderWithTexture.setInt("material.specular", 0);
    lightingShaderWithTexture.setFloat("material.shininess", 32.0f);
    lightingShaderWithTexture.setMat4("model", model);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture_bool ? texture3 : whiteTexture);
    glBindVertexArray(cubeVAO);
    glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, (void *)0);
    glBindVertexArray(0);

    // right half-door
    translateMatrix =
        glm::translate(identityMatrix, glm::vec3(-1.5, -2.0, 0.0));
    scaleMatrix = glm::scale(identityMatrix, glm::vec3(-1.5, 1.5, 0.125));
    rotateYMatrix = glm::rotate(identityMatrix, glm::radians(-doorAngle),
                                glm::vec3(0.0f, 1.0f, 0.0f));
    model = translateMatrix * rotateYMatrix * scaleMatrix;
    lightingShaderWithTexture.setInt("material.diffuse", 0);
    lightingShaderWithTexture.setInt("material.specular", 0);
    lightingShaderWithTexture.setFloat("material.shininess", 32.0f);
    lightingShaderWithTexture.setMat4("model", model);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture_bool ? texture3 : whiteTexture);
    glBindVertexArray(cubeVAO);
    glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, (void *)0);
    glBindVertexArray(0);

    // moi 1

    translateMatrix =
        glm::translate(identityMatrix, glm::vec3(6.5f, -1.5f, -2.0f));
    scaleMatrix = glm::scale(identityMatrix, glm::vec3(0.3f, 0.3f, 0.3f));
    rotateXMatrix = glm::rotate(identityMatrix, glm::radians(-90.0f),
                                glm::vec3(1.0f, 0.0f, 0.0f));
    // translateMatrix = glm::translate(identityMatrix, sofaTranslation);
    model = rotateXMatrix * translateMatrix * scaleMatrix;
    lightingShaderWithTexture.setInt("material.diffuse", 0);
    lightingShaderWithTexture.setInt("material.specular", 0);
    lightingShaderWithTexture.setFloat("material.shininess", 0.5f);
    lightingShaderWithTexture.setMat4("model", model);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture_bool ? texture3 : whiteTexture);
    glBindVertexArray(VAOtt);
    glDrawElements(GL_TRIANGLES, (unsigned int)tt_indices.size(),
                   GL_UNSIGNED_INT, (void *)0);

    // moi 2
    translateMatrix =
        glm::translate(identityMatrix, glm::vec3(-12.5f, -1.5f, -2.0f));
    scaleMatrix = glm::scale(identityMatrix, glm::vec3(0.3f, 0.3f, 0.3f));
    rotateXMatrix = glm::rotate(identityMatrix, glm::radians(-90.0f),
                                glm::vec3(1.0f, 0.0f, 0.0f));
    model = rotateXMatrix * translateMatrix * scaleMatrix;
    lightingShaderWithTexture.setInt("material.diffuse", 0);
    lightingShaderWithTexture.setInt("material.specular", 0);
    lightingShaderWithTexture.setFloat("material.shininess", 0.5f);
    lightingShaderWithTexture.setMat4("model", model);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture_bool ? texture3 : whiteTexture);
    glBindVertexArray(VAOtt);
    glDrawElements(GL_TRIANGLES, (unsigned int)tt_indices.size(),
                   GL_UNSIGNED_INT, (void *)0);

    // moi3
    translateMatrix =
        glm::translate(identityMatrix, glm::vec3(6.5f, 8.5f, -2.0f));
    scaleMatrix = glm::scale(identityMatrix, glm::vec3(0.3f, 0.3f, 0.3f));
    // angleInRadians = glm::radians(-90.0f);
    rotateXMatrix = glm::rotate(identityMatrix, glm::radians(-90.0f),
                                glm::vec3(1.0f, 0.0f, 0.0f));
    // translateMatrix = glm::translate(identityMatrix, sofaTranslation);
    model = rotateXMatrix * translateMatrix * scaleMatrix;
    lightingShaderWithTexture.setInt("material.diffuse", 0);
    lightingShaderWithTexture.setInt("material.specular", 0);
    lightingShaderWithTexture.setFloat("material.shininess", 0.5f);
    lightingShaderWithTexture.setMat4("model", model);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture_bool ? texture3 : whiteTexture);
    glBindVertexArray(VAOtt);
    glDrawElements(GL_TRIANGLES, (unsigned int)tt_indices.size(),
                   GL_UNSIGNED_INT, (void *)0);

    // moi 4

    translateMatrix =
        glm::translate(identityMatrix, glm::vec3(-12.5f, 8.5f, -2.0f));
    scaleMatrix = glm::scale(identityMatrix, glm::vec3(0.3f, 0.3f, 0.3f));

    rotateXMatrix = glm::rotate(identityMatrix, glm::radians(-90.0f),
                                glm::vec3(1.0f, 0.0f, 0.0f));
    // translateMatrix = glm::translate(identityMatrix, sofaTranslation);
    model = rotateXMatrix * translateMatrix * scaleMatrix;
    lightingShaderWithTexture.setInt("material.diffuse", 0);
    lightingShaderWithTexture.setInt("material.specular", 0);
    lightingShaderWithTexture.setFloat("material.shininess", 0.5f);
    lightingShaderWithTexture.setMat4("model", model);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture_bool ? texture3 : whiteTexture);
    glBindVertexArray(VAOtt);
    glDrawElements(GL_TRIANGLES, (unsigned int)tt_indices.size(),
                   GL_UNSIGNED_INT, (void *)0);

    // ── INNER MEDIEVAL CASTLE ──────────────────────────────────────────────────
    // Courtyard interior: X[-8.0,2.0], Z[-6.75,0.0], floor at Y=-2.0
    // Castle bbox: X[-5.5,-0.5] × Z[-5.5,-1.5], centered at (-3.0, _, -3.5)
    // Corner towers: 1.0×2.8×1.0 (W×H×D), tower-top Y=0.8
    // Connecting walls: 0.25 thick, 1.8 tall (top Y=-0.2)
    // Keep: 1.5×3.6×1.5, top Y=1.6
    {
        // Utility lambda: translate × scale → draw a unit cube
        auto castleCube = [&](float px, float py, float pz,
                               float sx, float sy, float sz) {
            glm::mat4 m =
                glm::translate(identityMatrix, glm::vec3(px, py, pz)) *
                glm::scale(identityMatrix, glm::vec3(sx, sy, sz));
            lightingShaderWithTexture.setMat4("model", m);
            glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, (void *)0);
        };

        // ── Stone (dome.png, texture unit 0) ──────────────────────────────────
        lightingShaderWithTexture.setInt("material.diffuse", 0);
        lightingShaderWithTexture.setInt("material.specular", 0);
        lightingShaderWithTexture.setFloat("material.shininess", 28.0f);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture_bool ? texture : whiteTexture);
        glBindVertexArray(cubeVAO);

        // Base platform (slightly raised under whole castle)
        castleCube(-5.5f, -2.0f, -5.5f,  5.0f, 0.18f, 4.0f);

        // 4 corner towers
        castleCube(-5.5f, -2.0f, -2.5f,  1.0f, 2.8f, 1.0f); // FL
        castleCube(-1.5f, -2.0f, -2.5f,  1.0f, 2.8f, 1.0f); // FR
        castleCube(-5.5f, -2.0f, -5.5f,  1.0f, 2.8f, 1.0f); // BL
        castleCube(-1.5f, -2.0f, -5.5f,  1.0f, 2.8f, 1.0f); // BR

        // Connecting walls (0.25 thick, 1.8 tall)
        castleCube(-5.5f,  -2.0f, -4.5f, 0.25f, 1.8f, 2.0f); // Left
        castleCube(-0.75f, -2.0f, -4.5f, 0.25f, 1.8f, 2.0f); // Right
        castleCube(-4.5f,  -2.0f, -1.5f, 1.0f,  1.8f, 0.25f); // Front-left
        castleCube(-2.5f,  -2.0f, -1.5f, 1.0f,  1.8f, 0.25f); // Front-right
        castleCube(-3.5f,  -0.2f, -1.5f, 1.0f,  0.4f, 0.25f); // Gate lintel
        castleCube(-4.5f,  -2.0f, -5.5f, 3.0f,  1.8f, 0.25f); // Back wall

        // Tower crenellations — 2×2 merlons per tower (size 0.3×0.45×0.3)
        // FL (origin -5.5,-2.0,-2.5)
        castleCube(-5.5f,  0.8f, -2.5f,  0.3f, 0.45f, 0.3f);
        castleCube(-5.5f,  0.8f, -1.85f, 0.3f, 0.45f, 0.3f);
        castleCube(-4.85f, 0.8f, -2.5f,  0.3f, 0.45f, 0.3f);
        castleCube(-4.85f, 0.8f, -1.85f, 0.3f, 0.45f, 0.3f);
        // FR (origin -1.5,-2.0,-2.5)
        castleCube(-1.5f,  0.8f, -2.5f,  0.3f, 0.45f, 0.3f);
        castleCube(-1.5f,  0.8f, -1.85f, 0.3f, 0.45f, 0.3f);
        castleCube(-0.85f, 0.8f, -2.5f,  0.3f, 0.45f, 0.3f);
        castleCube(-0.85f, 0.8f, -1.85f, 0.3f, 0.45f, 0.3f);
        // BL (origin -5.5,-2.0,-5.5)
        castleCube(-5.5f,  0.8f, -5.5f,  0.3f, 0.45f, 0.3f);
        castleCube(-5.5f,  0.8f, -4.85f, 0.3f, 0.45f, 0.3f);
        castleCube(-4.85f, 0.8f, -5.5f,  0.3f, 0.45f, 0.3f);
        castleCube(-4.85f, 0.8f, -4.85f, 0.3f, 0.45f, 0.3f);
        // BR (origin -1.5,-2.0,-5.5)
        castleCube(-1.5f,  0.8f, -5.5f,  0.3f, 0.45f, 0.3f);
        castleCube(-1.5f,  0.8f, -4.85f, 0.3f, 0.45f, 0.3f);
        castleCube(-0.85f, 0.8f, -5.5f,  0.3f, 0.45f, 0.3f);
        castleCube(-0.85f, 0.8f, -4.85f, 0.3f, 0.45f, 0.3f);

        // Wall battlements (top at Y=-0.2, merlon 0.25×0.35×0.28)
        for (int i = 0; i < 4; ++i)  // Left wall
            castleCube(-5.5f, -0.2f, -4.4f + i * 0.52f, 0.25f, 0.35f, 0.28f);
        for (int i = 0; i < 4; ++i)  // Right wall
            castleCube(-0.75f, -0.2f, -4.4f + i * 0.52f, 0.25f, 0.35f, 0.28f);
        for (int i = 0; i < 6; ++i)  // Back wall
            castleCube(-4.4f + i * 0.52f, -0.2f, -5.5f, 0.28f, 0.35f, 0.25f);
        for (int i = 0; i < 2; ++i)  // Front-left section
            castleCube(-4.4f + i * 0.6f, -0.2f, -1.5f, 0.28f, 0.35f, 0.25f);
        for (int i = 0; i < 2; ++i)  // Front-right section
            castleCube(-2.4f + i * 0.6f, -0.2f, -1.5f, 0.28f, 0.35f, 0.25f);

        // ── Keep (tile.png for visual distinction) ────────────────────────────
        glBindTexture(GL_TEXTURE_2D, texture_bool ? texture2 : whiteTexture);
        // Keep body: X[-3.75,-2.25], Z[-4.25,-2.75], top Y=1.6
        castleCube(-3.75f, -2.0f, -4.25f, 1.5f, 3.6f, 1.5f);

        // Keep crenellations (Y=1.6, size 0.35×0.5×0.35, back to stone)
        glBindTexture(GL_TEXTURE_2D, texture_bool ? texture : whiteTexture);
        for (int i = 0; i < 3; ++i)  // Keep front edge  (Z=-2.75)
            castleCube(-3.75f + i * 0.575f, 1.6f, -2.75f, 0.35f, 0.5f, 0.35f);
        for (int i = 0; i < 3; ++i)  // Keep back edge   (Z=-4.25)
            castleCube(-3.75f + i * 0.575f, 1.6f, -4.25f, 0.35f, 0.5f, 0.35f);
        for (int i = 0; i < 3; ++i)  // Keep left edge   (X=-3.75)
            castleCube(-3.75f, 1.6f, -4.25f + i * 0.575f, 0.35f, 0.5f, 0.35f);
        for (int i = 0; i < 3; ++i)  // Keep right edge  (X=-2.6)
            castleCube(-2.6f,  1.6f, -4.25f + i * 0.575f, 0.35f, 0.5f, 0.35f);

        // ── Wood: doors + tower windows (wood.png) ────────────────────────────
        lightingShaderWithTexture.setFloat("material.shininess", 16.0f);
        glBindTexture(GL_TEXTURE_2D, texture_bool ? texture3 : whiteTexture);

        // Gate door (gate opening X[-3.5,-2.5], door slightly inset)
        castleCube(-3.3f, -2.0f, -1.42f,  0.6f, 1.5f, 0.08f);
        // Keep entrance door (keep front face Z=-2.75)
        castleCube(-3.2f, -2.0f, -2.74f,  0.4f, 1.2f, 0.08f);
        // Tower front windows (front face Z=-1.5)
        castleCube(-5.14f, -0.65f, -1.44f, 0.28f, 0.38f, 0.07f); // FL
        castleCube(-1.14f, -0.65f, -1.44f, 0.28f, 0.38f, 0.07f); // FR
        // Keep upper window (front face Z=-2.75)
        castleCube(-3.25f, 0.3f, -2.74f,   0.5f, 0.55f, 0.07f);
    }
    // ── END INNER MEDIEVAL CASTLE ──────────────────────────────────────────────

    // ── WOODEN BALLISTA on keep top ──────────────────────────────────────────
    // Keep top center: X=-3.0, Y=1.6, Z=-3.5. Fits within 1.5×1.5 footprint.
    // Trough faces -Z (toward back wall). Bow arms extend in ±X direction.
    // Wood = texture3, metal fittings = lightingShader dark-gray.
    {
        lightingShaderWithTexture.use();
        lightingShaderWithTexture.setInt("material.diffuse", 0);
        lightingShaderWithTexture.setInt("material.specular", 0);
        lightingShaderWithTexture.setFloat("material.shininess", 20.0f);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture_bool ? texture3 : whiteTexture);
        glBindVertexArray(cubeVAO);

        // 180° Y-rotation around ballista center (-3.0, 0, -3.5) so trough faces +Z
        glm::mat4 ballistaBase =
            glm::translate(identityMatrix, glm::vec3(-3.0f, 0.0f, -3.5f)) *
            glm::rotate(identityMatrix, glm::radians(180.0f), glm::vec3(0,1,0)) *
            glm::translate(identityMatrix, glm::vec3(3.0f, 0.0f, 3.5f));

        // min-corner cube draw
        auto bW = [&](float px, float py, float pz,
                       float sx, float sy, float sz) {
            glm::mat4 m = ballistaBase
                        * glm::translate(identityMatrix, glm::vec3(px, py, pz))
                        * glm::scale(identityMatrix, glm::vec3(sx, sy, sz));
            lightingShaderWithTexture.setMat4("model", m);
            glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, (void *)0);
        };
        // center-pivot cube with optional Y-axis or Z-axis rotation
        auto bWR = [&](float cx, float cy, float cz,
                        float sx, float sy, float sz,
                        float deg, glm::vec3 axis) {
            glm::mat4 m =
                ballistaBase *
                glm::translate(identityMatrix, glm::vec3(cx, cy, cz)) *
                glm::rotate(identityMatrix, glm::radians(deg), axis) *
                glm::translate(identityMatrix, glm::vec3(-sx*0.5f, -sy*0.5f, -sz*0.5f)) *
                glm::scale(identityMatrix, glm::vec3(sx, sy, sz));
            lightingShaderWithTexture.setMat4("model", m);
            glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, (void *)0);
        };

        // ── Turntable platform ──────────────────────────────────────────────
        bW(-3.58f, 1.60f, -3.97f,  1.16f, 0.09f, 0.94f);

        // ── Left wheel (center X=-3.56, Y=1.92, Z=-3.50) ───────────────────
        bW(-3.60f, 1.885f, -3.80f, 0.08f, 0.07f, 0.60f); // H spoke
        bW(-3.60f, 1.62f,  -3.535f,0.08f, 0.60f, 0.07f); // V spoke
        bW(-3.60f, 1.875f, -3.545f,0.08f, 0.09f, 0.09f); // hub
        // rim corner blocks
        bW(-3.60f, 1.62f,  -3.84f, 0.08f, 0.08f, 0.08f);
        bW(-3.60f, 1.62f,  -3.24f, 0.08f, 0.08f, 0.08f);
        bW(-3.60f, 2.19f,  -3.84f, 0.08f, 0.08f, 0.08f);
        bW(-3.60f, 2.19f,  -3.24f, 0.08f, 0.08f, 0.08f);

        // ── Right wheel (center X=-2.44, Y=1.92, Z=-3.50) ──────────────────
        bW(-2.52f, 1.885f, -3.80f, 0.08f, 0.07f, 0.60f);
        bW(-2.52f, 1.62f,  -3.535f,0.08f, 0.60f, 0.07f);
        bW(-2.52f, 1.875f, -3.545f,0.08f, 0.09f, 0.09f);
        bW(-2.52f, 1.62f,  -3.84f, 0.08f, 0.08f, 0.08f);
        bW(-2.52f, 1.62f,  -3.24f, 0.08f, 0.08f, 0.08f);
        bW(-2.52f, 2.19f,  -3.84f, 0.08f, 0.08f, 0.08f);
        bW(-2.52f, 2.19f,  -3.24f, 0.08f, 0.08f, 0.08f);

        // Axle between wheels
        bW(-3.56f, 1.875f, -3.545f, 1.12f, 0.09f, 0.09f);

        // ── Upright frame (two posts + crossbar + X-brace diagonals) ────────
        bW(-3.53f, 1.70f, -3.535f, 0.10f, 0.82f, 0.10f); // left post
        bW(-2.57f, 1.70f, -3.535f, 0.10f, 0.82f, 0.10f); // right post
        bW(-3.53f, 2.32f, -3.535f, 1.06f, 0.10f, 0.10f); // top crossbar
        // diagonal braces (Z-rotation around frame center)
        bWR(-3.0f, 2.02f, -3.49f,  0.08f, 0.78f, 0.08f,  22.0f, glm::vec3(0,0,1));
        bWR(-3.0f, 2.02f, -3.49f,  0.08f, 0.78f, 0.08f, -22.0f, glm::vec3(0,0,1));

        // ── Bolt trough (launching channel, points toward -Z) ────────────────
        bW(-3.11f, 1.93f, -4.22f,  0.22f, 0.10f, 0.72f); // main beam
        bW(-3.11f, 1.99f, -4.22f,  0.05f, 0.08f, 0.72f); // left rail
        bW(-2.94f, 1.99f, -4.22f,  0.05f, 0.08f, 0.72f); // right rail
        bW(-3.13f, 1.80f, -3.52f,  0.26f, 0.22f, 0.10f); // rear trough support

        // ── Bow arm animation state ───────────────────────────────────────────
        // armR: +15 = cocked/at-rest (tips angled toward windlass, away from target)
        //        0  = horizontal
        //       -35 = fired (tips angled toward muzzle/target)
        // Animation: arms start cocked (+15°), snap to fired (-35°), return to cocked.
        float armR = 15.0f; // degrees; positive = backward (cocked)
        {
            float t = ballistaAnimT;
            const float snapEnd  = 0.12f;
            const float totalEnd = 0.85f;
            float prog = 0.0f;
            if (t > 0.0f && t <= snapEnd) {
                float u = t / snapEnd;
                prog = u * (2.0f - u);                     // ease-out: fast snap
            } else if (t > snapEnd && t < totalEnd) {
                float u = (t - snapEnd) / (totalEnd - snapEnd);
                prog = 1.0f - u * u * (3.0f - 2.0f * u);  // smoothstep return
            }
            armR = 15.0f - glm::clamp(prog, 0.0f, 1.0f) * 50.0f;
        }
        // String Z offset: follows arm tip's local-Z movement relative to rest
        const float snapZStr = 0.64f * (sinf(glm::radians(armR))
                                       - sinf(glm::radians(15.0f)));

        // ── Bow arms (extend ±X, animated) ────────────────────────────────────
        // Positive armR rotates left-arm tip toward local +Z (windlass = cocked).
        // Negative armR rotates left-arm tip toward local -Z (muzzle = fired).
        // After ballistaBase 180°Y flip: local -Z → world +Z (toward gate/target). ✓
        // Left arm: pivot at right root (-3.50, 2.06, -3.50)
        {
            glm::mat4 m = ballistaBase
                * glm::translate(identityMatrix, glm::vec3(-3.50f, 2.06f, -3.50f))
                * glm::rotate(identityMatrix, glm::radians(armR), glm::vec3(0,1,0))
                * glm::translate(identityMatrix, glm::vec3(-0.64f, 0.0f, 0.0f))
                * glm::scale(identityMatrix, glm::vec3(0.64f, 0.09f, 0.09f));
            lightingShaderWithTexture.setMat4("model", m);
            glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, (void*)0);
        }
        bW(-4.14f, 1.70f, -3.50f,  0.09f, 0.42f, 0.09f); // left arm brace (static)
        // Right arm: pivot at left root (-2.57, 2.06, -3.50), mirrored rotation
        {
            glm::mat4 m = ballistaBase
                * glm::translate(identityMatrix, glm::vec3(-2.57f, 2.06f, -3.50f))
                * glm::rotate(identityMatrix, glm::radians(-armR), glm::vec3(0,1,0))
                * glm::scale(identityMatrix, glm::vec3(0.64f, 0.09f, 0.09f));
            lightingShaderWithTexture.setMat4("model", m);
            glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, (void*)0);
        }
        bW(-2.57f, 1.70f, -3.50f,  0.09f, 0.42f, 0.09f); // right arm brace (static)

        // Torsion bundle columns (flanking the frame, vertical thick posts)
        bW(-3.63f, 1.70f, -3.46f,  0.10f, 0.60f, 0.10f); // left torsion post
        bW(-2.47f, 1.70f, -3.46f,  0.10f, 0.60f, 0.10f); // right torsion post

        // Bowstring: spans arm tips dynamically as arms rotate.
        // Left tip X  = -3.50 - 0.64*cos(armR)
        // Right tip X = -2.57 + 0.64*cos(armR)  →  width = 0.93 + 1.28*cos(armR)
        {
            float strWidth = 0.93f + 1.28f * cosf(glm::radians(armR));
            float strMinX  = -3.50f - 0.64f * cosf(glm::radians(armR));
            bW(strMinX, 2.06f, -3.34f + snapZStr, strWidth, 0.03f, 0.03f);
        }

        // ── Windlass at back (draws the string) ──────────────────────────────
        bW(-3.50f, 1.78f, -3.16f,  1.00f, 0.09f, 0.09f); // drum
        bW(-3.50f, 1.75f, -3.07f,  0.07f, 0.24f, 0.07f); // left crank arm
        bW(-2.58f, 1.75f, -3.07f,  0.07f, 0.24f, 0.07f); // right crank arm
        bW(-3.50f, 1.75f, -2.96f,  1.00f, 0.07f, 0.07f); // crank cross-handle

        // ── Metal reinforcement straps (dark gray) ───────────────────────────
        glBindVertexArray(0);
        lightingShader.use();
        lightingShader.setVec3("material.ambient",  glm::vec3(0.10f, 0.10f, 0.12f));
        lightingShader.setVec3("material.diffuse",  glm::vec3(0.22f, 0.22f, 0.25f));
        lightingShader.setVec3("material.specular", glm::vec3(0.50f, 0.50f, 0.55f));
        lightingShader.setFloat("material.shininess", 48.0f);
        glBindVertexArray(cubeVAO);
        auto bM = [&](float px, float py, float pz, float sx, float sy, float sz) {
            glm::mat4 m = ballistaBase
                        * glm::translate(identityMatrix, glm::vec3(px, py, pz))
                        * glm::scale(identityMatrix, glm::vec3(sx, sy, sz));
            lightingShader.setMat4("model", m);
            glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, (void *)0);
        };
        bM(-3.60f, 1.88f, -3.545f, 0.08f, 0.08f, 0.08f); // left wheel hub bolt
        bM(-2.52f, 1.88f, -3.545f, 0.08f, 0.08f, 0.08f); // right wheel hub bolt
        bM(-3.55f, 2.31f, -3.49f,  1.10f, 0.05f, 0.05f); // crossbar strap
        bM(-4.12f, 2.05f, -3.49f,  0.05f, 0.05f, 0.18f); // left bow tip fitting
        bM(-2.48f, 2.05f, -3.49f,  0.05f, 0.05f, 0.18f); // right bow tip fitting
        glBindVertexArray(0);

        // Restore lightingShaderWithTexture for subsequent objects
        lightingShaderWithTexture.use();
        lightingShaderWithTexture.setInt("material.diffuse", 0);
        lightingShaderWithTexture.setInt("material.specular", 0);
        lightingShaderWithTexture.setFloat("material.shininess", 32.0f);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture_bool ? texture : whiteTexture);
        glBindVertexArray(cubeVAO);
    }
    // ── END WOODEN BALLISTA ───────────────────────────────────────────────────

    // ── BALLISTA ARROW (physics projectile) ───────────────────────────────────
    if (arrowActive) {
        // Align the arrow's local +Z axis with the current velocity direction.
        glm::mat4 arrowRot(1.0f);
        glm::vec3 vel = glm::normalize(arrowVelocity);
        glm::vec3 defDir(0.0f, 0.0f, 1.0f);
        float cosA   = glm::clamp(glm::dot(defDir, vel), -1.0f, 1.0f);
        glm::vec3 crossV = glm::cross(defDir, vel);
        float sinA   = glm::length(crossV);
        if (sinA > 0.001f)
            arrowRot = glm::rotate(identityMatrix, atan2f(sinA, cosA),
                                   glm::normalize(crossV));
        else if (cosA < 0.0f)  // vel ≈ (0,0,-1): 180° around Y
            arrowRot = glm::rotate(identityMatrix, glm::radians(180.0f),
                                   glm::vec3(0.0f, 1.0f, 0.0f));

        // Wood texture for shaft / fletching
        lightingShaderWithTexture.use();
        lightingShaderWithTexture.setInt("material.diffuse", 0);
        lightingShaderWithTexture.setInt("material.specular", 0);
        lightingShaderWithTexture.setFloat("material.shininess", 20.0f);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture_bool ? texture3 : whiteTexture);
        glBindVertexArray(cubeVAO);

        // Helper: cube centered at arrowPos after arrowRot, offset in local space
        auto arrowCube = [&](float ox, float oy, float oz,
                              float sx, float sy, float sz) {
            model = glm::translate(identityMatrix, arrowPos)
                  * arrowRot
                  * glm::translate(identityMatrix, glm::vec3(ox, oy, oz))
                  * glm::scale(identityMatrix, glm::vec3(sx, sy, sz));
            lightingShaderWithTexture.setMat4("model", model);
            glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, (void *)0);
        };

        // ── Shaft (along +Z, centred at origin in local space) ──────────────
        arrowCube(-0.02f, -0.02f, -0.275f,  0.04f, 0.04f, 0.55f);

        // ── Arrowhead (slightly thicker, darker — just metal texture swap) ───
        glBindTexture(GL_TEXTURE_2D, texture_bool ? texture3 : whiteTexture);
        arrowCube(-0.025f, -0.025f,  0.275f,  0.05f, 0.05f, 0.16f);

        // ── Fletching — top fin ───────────────────────────────────────────────
        arrowCube(-0.006f,  0.02f,  -0.275f,  0.012f, 0.09f, 0.13f);
        // ── Fletching — side fin ─────────────────────────────────────────────
        arrowCube(-0.04f,  -0.006f, -0.275f,  0.09f, 0.012f, 0.13f);

        glBindVertexArray(0);
    }
    // ── END BALLISTA ARROW ────────────────────────────────────────────────────

    // pot1

    translateMatrix =
        glm::translate(identityMatrix, glm::vec3(-5.9, -2.0, 0.5));
    scaleMatrix = glm::scale(identityMatrix, glm::vec3(0.012f, 0.015f, 0.015f));
    rotateXMatrix = glm::rotate(identityMatrix, glm::radians(-0.0f),
                                glm::vec3(1.0f, 0.0f, 0.0f));
    // translateMatrix = glm::translate(identityMatrix, sofaTranslation);
    model = rotateXMatrix * translateMatrix * scaleMatrix;
    lightingShaderWithTexture.setInt("material.diffuse", 0);
    lightingShaderWithTexture.setInt("material.specular", 0);
    lightingShaderWithTexture.setFloat("material.shininess", 0.5f);
    lightingShaderWithTexture.setMat4("model", model);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture_bool ? texture4 : whiteTexture);
    glBindVertexArray(VAOpot);
    glDrawElements(GL_TRIANGLES, (unsigned int)potindices.size(),
                   GL_UNSIGNED_INT, (void *)0);

    // pot2
    translateMatrix =
        glm::translate(identityMatrix, glm::vec3(-0.5, -2.0, 1.0));
    scaleMatrix = glm::scale(identityMatrix, glm::vec3(0.012f, 0.015f, 0.015f));
    rotateXMatrix = glm::rotate(identityMatrix, glm::radians(-0.0f),
                                glm::vec3(1.0f, 0.0f, 0.0f));
    // translateMatrix = glm::translate(identityMatrix, sofaTranslation);
    model = rotateXMatrix * translateMatrix * scaleMatrix;
    lightingShaderWithTexture.setInt("material.diffuse", 0);
    lightingShaderWithTexture.setInt("material.specular", 0);
    lightingShaderWithTexture.setFloat("material.shininess", 0.5f);
    lightingShaderWithTexture.setMat4("model", model);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture_bool ? texture4 : whiteTexture);
    glBindVertexArray(VAOpot);
    glDrawElements(GL_TRIANGLES, (unsigned int)potindices.size(),
                   GL_UNSIGNED_INT, (void *)0);

    // lightingshader
    lightingShader.use();
    lightingShader.setVec3("viewPos", camera.Position);
    lightingShader.setMat4("projection", projection);
    lightingShader.setMat4("view", view);

    // point light 1
    pointlight1.setUpPointLight(lightingShader);
    // point light 2
    pointlight2.setUpPointLight(lightingShader);
    // point light 3
    pointlight3.setUpPointLight(lightingShader);

    spotlight1.setUpspotLight(lightingShader);
    spotlight2.setUpspotLight(lightingShader);

    // constantShader.setMat4("view", view);
    // lightingShader.setMat4("view", view);

    lightingShader.setVec3("directionalLight.direction", 0.0f, -3.0f, 0.0f);
    lightingShader.setVec3("directionalLight.ambient", .5f, .5f, .5f);
    lightingShader.setVec3("directionalLight.diffuse", .8f, .8f, .8f);
    lightingShader.setVec3("directionalLight.specular", 1.0f, 1.0f, 1.0f);

    lightingShader.setBool("directionLightOn", true);

    // Switch back to lightingShader
    lightingShader.use();

    // --- FRACTAL TREES IN POTS ---
    lightingShader.use();
    lightingShader.setVec3("material.ambient", glm::vec3(0.25f, 0.15f, 0.05f));
    lightingShader.setVec3("material.diffuse", glm::vec3(0.4f, 0.25f, 0.1f));
    lightingShader.setVec3("material.specular", glm::vec3(0.05f, 0.05f, 0.05f));
    lightingShader.setFloat("material.shininess", 16.0f);

    // Tree in pot1: located at (-5.9, -2.0, 0.5), we raise it slightly to -1.8
    glm::mat4 treeScale =
        glm::scale(identityMatrix, glm::vec3(0.8f, 0.8f, 0.8f));
    glm::mat4 treeRot =
        glm::rotate(identityMatrix, glm::radians(0.0f),
                    glm::vec3(0.0f, 1.0f, 0.0f)); // No rotation needed

    // Tree in pot1: placed centered on top of the pot.
    // Pot1 origin is (-5.9, -2.0, 0.5). Box is scaled by (0.012, 0.015, 0.015)
    // with center offset (0.45, 0.84, 0.57) to the top surface.
    translateMatrix =
        glm::translate(identityMatrix, glm::vec3(-5.45f, -1.16f, 1.07f));
    model = translateMatrix * treeRot * treeScale;
    lightingShader.setMat4("model", model);
    glBindVertexArray(treeVAO);
    glDrawElements(GL_LINES, treeIndices.size(), GL_UNSIGNED_INT, 0);

    // Tree in pot2: located at (-0.5, -2.0, 1.0)
    // Tree in pot2: placed centered on top of the pot.
    translateMatrix =
        glm::translate(identityMatrix, glm::vec3(-0.05f, -1.16f, 1.57f));
    model = translateMatrix * treeRot * treeScale;
    lightingShader.setMat4("model", model);
    glDrawElements(GL_LINES, treeIndices.size(), GL_UNSIGNED_INT, 0);
    // Sw`itch back to lightingShader after trees
    lightingShader.use();

    // ── CASTLE PINE TREES ─────────────────────────────────────────────────────
    // 4 pine trees outside castle corners. Trunks: lightingShader (brown).
    // Cones: lightingShaderWithTexture + treeFoliageTexture.
    {
        float pinePos[4][2] = {
            { -6.5f, -1.2f },  // outside FL corner
            {  0.8f, -1.2f },  // outside FR corner
            { -6.5f, -5.8f },  // outside BL corner
            {  0.8f, -5.8f },  // outside BR corner
        };

        // --- Trunks (brown, untextured) ---
        lightingShader.use();
        lightingShader.setVec3("material.ambient",  glm::vec3(0.20f, 0.10f, 0.04f));
        lightingShader.setVec3("material.diffuse",  glm::vec3(0.38f, 0.22f, 0.08f));
        lightingShader.setVec3("material.specular", glm::vec3(0.02f, 0.02f, 0.02f));
        lightingShader.setFloat("material.shininess", 8.0f);
        glBindVertexArray(cubeVAO);
        for (int t = 0; t < 4; ++t) {
            float tx = pinePos[t][0], tz = pinePos[t][1];
            model = glm::translate(identityMatrix, glm::vec3(tx - 0.06f, -2.0f, tz - 0.06f))
                  * glm::scale(identityMatrix, glm::vec3(0.12f, 0.45f, 0.12f));
            lightingShader.setMat4("model", model);
            glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, (void *)0);
        }

        // --- Cones: lightingShaderWithTexture + treeFoliageTexture ---
        lightingShaderWithTexture.use();
        lightingShaderWithTexture.setInt("material.diffuse", 0);
        lightingShaderWithTexture.setInt("material.specular", 0);
        lightingShaderWithTexture.setFloat("material.shininess", 12.0f);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture_bool ? treeFoliageTexture : whiteTexture);
        glBindVertexArray(coneVAO);
        for (int t = 0; t < 4; ++t) {
            float tx = pinePos[t][0], tz = pinePos[t][1];
            // Lower cone: base Y=-1.75, radius=0.55, height factor=0.87
            model = glm::translate(identityMatrix, glm::vec3(tx, -1.75f, tz))
                  * glm::scale(identityMatrix, glm::vec3(0.55f, 0.87f, 0.55f));
            lightingShaderWithTexture.setMat4("model", model);
            glDrawElements(GL_TRIANGLES, coneIndicesCount, GL_UNSIGNED_INT, (void *)0);
            // Upper cone: base Y=-1.2, radius=0.38, height factor=0.40
            model = glm::translate(identityMatrix, glm::vec3(tx, -1.2f, tz))
                  * glm::scale(identityMatrix, glm::vec3(0.38f, 0.40f, 0.38f));
            lightingShaderWithTexture.setMat4("model", model);
            glDrawElements(GL_TRIANGLES, coneIndicesCount, GL_UNSIGNED_INT, (void *)0);
        }
        glBindVertexArray(0);
        lightingShader.use();  // restore for next section
    }
    // ── END CASTLE PINE TREES ──────────────────────────────────────────────────

    // ── LARGE FRACTAL TREES between moi pairs (90° sides) ───────────────────
    // Moi world positions (after rotX-90°):
    //   moi1=(6.5,-2,1.5)  moi3=(6.5,-2,-8.5)  → RIGHT side, midpoint Z=-3.5
    //   moi2=(-12.5,-2,1.5) moi4=(-12.5,-2,-8.5)→ LEFT side,  midpoint Z=-3.5
    // Trees are placed slightly inside the fort from each side wall.
    {
      glm::mat4 bigTreeScale =
          glm::scale(identityMatrix, glm::vec3(1.5f, 1.5f, 1.5f));
      // right wall side (moi1 & moi3), slightly inside (X = 4.0)
      glm::mat4 bigTree1Trans =
          glm::translate(identityMatrix, glm::vec3(6.0f, -2.0f, -3.5f));
      // left wall side (moi2 & moi4), slightly inside (X = -10.0)
      glm::mat4 bigTree2Trans =
          glm::translate(identityMatrix, glm::vec3(-12.0f, -2.0f, -3.5f));

      // — Trunk + branches: lightingShaderWithTexture + TreeRoot.png —
      lightingShaderWithTexture.use();
      lightingShaderWithTexture.setInt("material.diffuse", 0);
      lightingShaderWithTexture.setInt("material.specular", 0);
      lightingShaderWithTexture.setFloat("material.shininess", 16.0f);
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, texture_bool ? treeRootTexture : whiteTexture);

      lightingShaderWithTexture.setMat4("model", bigTree1Trans * bigTreeScale);
      glBindVertexArray(bigTreeVAO);
      glDrawElements(GL_TRIANGLES, (unsigned int)bigTreeIndices.size(),
                     GL_UNSIGNED_INT, 0);

      lightingShaderWithTexture.setMat4("model", bigTree2Trans * bigTreeScale);
      glDrawElements(GL_TRIANGLES, (unsigned int)bigTreeIndices.size(),
                     GL_UNSIGNED_INT, 0);
      glBindVertexArray(0);

      // — Foliage spheres: lightingShaderWithTexture + TreeTexture.png —
      lightingShaderWithTexture.setInt("material.diffuse", 0);
      lightingShaderWithTexture.setInt("material.specular", 0);
      lightingShaderWithTexture.setFloat("material.shininess", 12.0f);
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D,
                    texture_bool ? treeFoliageTexture : whiteTexture);

      lightingShaderWithTexture.setMat4("model", bigTree1Trans * bigTreeScale);
      glBindVertexArray(foliageVAO);
      glDrawElements(GL_TRIANGLES, (unsigned int)foliageIndices.size(),
                     GL_UNSIGNED_INT, 0);

      lightingShaderWithTexture.setMat4("model", bigTree2Trans * bigTreeScale);
      glDrawElements(GL_TRIANGLES, (unsigned int)foliageIndices.size(),
                     GL_UNSIGNED_INT, 0);
      glBindVertexArray(0);

      // Restore lightingShader for subsequent objects
      lightingShader.use();
    }
    // ─────────────────────────────────────────────────────────────────────────

    // Use shader program
    lightingShader.use();
    translateMatrix =
        glm::translate(identityMatrix, glm::vec3(6.5f, -1.5f, -2.0f));
    scaleMatrix = glm::scale(identityMatrix, glm::vec3(0.3f, 0.3f, 0.3f));
    float angleInRadians = glm::radians(-90.0f);
    rotateXMatrix = glm::rotate(identityMatrix, angleInRadians,
                                glm::vec3(1.0f, 0.0f, 0.0f));
    // translateMatrix = glm::translate(identityMatrix, sofaTranslation);
    model = rotateXMatrix * translateMatrix * scaleMatrix;
    // Define the wood color
    glm::vec4 woodColor =
        glm::vec4(0.52f, 0.37f, 0.26f, 1.0f); // Medium brown (wood-like)

    // Setting up material properties
    lightingShader.setVec3("material.ambient",
                           glm::vec3(woodColor) * 0.5f); // Darker for ambient
    lightingShader.setVec3("material.diffuse",
                           glm::vec3(woodColor)); // Main color for diffuse
    lightingShader.setVec3(
        "material.specular",
        glm::vec3(0.2f, 0.2f, 0.2f)); // Subtle highlights for specular
    lightingShader.setFloat("material.shininess", 16.0f);

    lightingShader.setMat4("model", model);
    // Bind VAO
    glBindVertexArray(VAOt);

    // Draw the object
    // glDrawElements(GL_TRIANGLES, tindices.size(), GL_UNSIGNED_INT, 0);

    // moi 2---------------------------------------

    // Use shader program
    lightingShader.use();
    translateMatrix =
        glm::translate(identityMatrix, glm::vec3(-12.5f, -1.5f, -2.0f));
    scaleMatrix = glm::scale(identityMatrix, glm::vec3(0.3f, 0.3f, 0.3f));
    angleInRadians = glm::radians(-90.0f);
    rotateXMatrix = glm::rotate(identityMatrix, angleInRadians,
                                glm::vec3(1.0f, 0.0f, 0.0f));
    // translateMatrix = glm::translate(identityMatrix, sofaTranslation);
    model = rotateXMatrix * translateMatrix * scaleMatrix;
    // Define the wood color
    woodColor =
        glm::vec4(0.52f, 0.37f, 0.26f, 1.0f); // Medium brown (wood-like)

    // Setting up material properties
    lightingShader.setVec3("material.ambient",
                           glm::vec3(woodColor) * 0.5f); // Darker for ambient
    lightingShader.setVec3("material.diffuse",
                           glm::vec3(woodColor)); // Main color for diffuse
    lightingShader.setVec3(
        "material.specular",
        glm::vec3(0.2f, 0.2f, 0.2f)); // Subtle highlights for specular
    lightingShader.setFloat("material.shininess", 16.0f);

    lightingShader.setMat4("model", model);

    // Bind VAO
    glBindVertexArray(VAOt);

    // Draw the object
    // glDrawElements(GL_TRIANGLES, tindices.size(), GL_UNSIGNED_INT, 0);

    // moi 3---------------------------------------

    // Use shader program
    lightingShader.use();
    translateMatrix =
        glm::translate(identityMatrix, glm::vec3(6.5f, 8.5f, -2.0f));
    scaleMatrix = glm::scale(identityMatrix, glm::vec3(0.3f, 0.3f, 0.3f));
    angleInRadians = glm::radians(-90.0f);
    rotateXMatrix = glm::rotate(identityMatrix, angleInRadians,
                                glm::vec3(1.0f, 0.0f, 0.0f));
    // translateMatrix = glm::translate(identityMatrix, sofaTranslation);
    model = rotateXMatrix * translateMatrix * scaleMatrix;
    // Define the wood color
    woodColor =
        glm::vec4(0.52f, 0.37f, 0.26f, 1.0f); // Medium brown (wood-like)

    // Setting up material properties
    lightingShader.setVec3("material.ambient",
                           glm::vec3(woodColor) * 0.5f); // Darker for ambient
    lightingShader.setVec3("material.diffuse",
                           glm::vec3(woodColor)); // Main color for diffuse
    lightingShader.setVec3(
        "material.specular",
        glm::vec3(0.2f, 0.2f, 0.2f)); // Subtle highlights for specular
    lightingShader.setFloat("material.shininess", 16.0f);

    lightingShader.setMat4("model", model);

    // Bind VAO
    glBindVertexArray(VAOt);

    // Draw the object
    // glDrawElements(GL_TRIANGLES, tindices.size(), GL_UNSIGNED_INT, 0);

    // moi 4---------------------------------------

    // Use shader program
    lightingShader.use();
    translateMatrix =
        glm::translate(identityMatrix, glm::vec3(-12.5f, 8.5f, -2.0f));
    scaleMatrix = glm::scale(identityMatrix, glm::vec3(0.3f, 0.3f, 0.3f));
    angleInRadians = glm::radians(-90.0f);
    rotateXMatrix = glm::rotate(identityMatrix, angleInRadians,
                                glm::vec3(1.0f, 0.0f, 0.0f));
    // translateMatrix = glm::translate(identityMatrix, sofaTranslation);
    model = rotateXMatrix * translateMatrix * scaleMatrix;
    // Define the wood color
    woodColor =
        glm::vec4(0.52f, 0.37f, 0.26f, 1.0f); // Medium brown (wood-like)

    // Setting up material properties
    lightingShader.setVec3("material.ambient",
                           glm::vec3(woodColor) * 0.5f); // Darker for ambient
    lightingShader.setVec3("material.diffuse",
                           glm::vec3(woodColor)); // Main color for diffuse
    lightingShader.setVec3(
        "material.specular",
        glm::vec3(0.2f, 0.2f, 0.2f)); // Subtle highlights for specular
    lightingShader.setFloat("material.shininess", 16.0f);

    lightingShader.setMat4("model", model);

    // Bind VAO
    // glBindVertexArray(VAOt);

    // Draw the object
    glDrawElements(GL_TRIANGLES, tindices.size(), GL_UNSIGNED_INT, 0);

    glm::mat4 RotateTranslateMatrix = glm::mat4(1.0f);
    glm::mat4 InvRotateTranslateMatrix = glm::mat4(1.0f);

    // Common translation matrix for all three objects

    // Use shader program
    lightingShader.use();

    // First object
    scaleMatrix = glm::scale(identityMatrix, glm::vec3(0.018f, 0.018f, 0.018f));
    angleInRadians = glm::radians(leftBaseRotationAngle + 27.0f);
    rotateXMatrix = glm::rotate(identityMatrix, angleInRadians,
                                glm::vec3(1.0f, 0.0f, 0.0f));
    translateMatrix =
        glm::translate(identityMatrix, glm::vec3(-6.5, -0.4, 2.5));
    RotateTranslateMatrix =
        glm::translate(identityMatrix, glm::vec3(-6.45, -2.0, 3.05));
    InvRotateTranslateMatrix =
        glm::translate(identityMatrix, glm::vec3(6.45, 2.0, -3.05));

    rotateYMatrix =
        glm::rotate(identityMatrix, angleInRadians,
                    glm::vec3(0.0f, 1.0f, 0.0f)); // Rotate around the y-axis
    model = RotateTranslateMatrix * rotateYMatrix * InvRotateTranslateMatrix *
            translateMatrix * scaleMatrix;
    // Define the wood color
    glm::vec4 ironColor = glm::vec4(0.56f, 0.57f, 0.58f, 1.0f); // Metallic gray

    // Setting up material properties
    lightingShader.setVec3("material.ambient",
                           glm::vec3(ironColor) * 0.5f); // Darker for ambient
    lightingShader.setVec3("material.diffuse",
                           glm::vec3(ironColor)); // Main color for diffuse
    lightingShader.setVec3(
        "material.specular",
        glm::vec3(0.2f, 0.2f, 0.2f)); // Subtle highlights for specular
    lightingShader.setFloat("material.shininess", 16.0f);

    lightingShader.setMat4("model", model);

    // Bind VAO
    glBindVertexArray(VAOrb);

    // Draw the object
    glDrawElements(GL_TRIANGLES, rbindices.size(), GL_UNSIGNED_INT, 0);

    //------------------------------------------------------------------------------------

    // Second object
    scaleMatrix = glm::scale(identityMatrix, glm::vec3(0.018f, 0.018f, 0.018f));
    angleInRadians = glm::radians(0.0f);
    rotateXMatrix = glm::rotate(identityMatrix, angleInRadians,
                                glm::vec3(1.0f, 0.0f, 0.0f));
    translateMatrix =
        glm::translate(identityMatrix, glm::vec3(-6.5, -0.4, 2.5));
    model = rotateXMatrix * translateMatrix * scaleMatrix;

    // Setting up material properties
    lightingShader.setVec3("material.ambient",
                           glm::vec3(ironColor) * 0.5f); // Darker for ambient
    lightingShader.setVec3("material.diffuse",
                           glm::vec3(ironColor)); // Main color for diffuse
    lightingShader.setVec3(
        "material.specular",
        glm::vec3(0.2f, 0.2f, 0.2f)); // Subtle highlights for specular
    lightingShader.setFloat("material.shininess", 16.0f);

    lightingShader.setMat4("model", model);

    // Bind VAO
    glBindVertexArray(VAOrbb);

    // Draw the object
    glDrawElements(GL_TRIANGLES, rbbindices.size(), GL_UNSIGNED_INT, 0);

    //------------------------------------------------------------------------------------

    // Third object
    scaleMatrix = glm::scale(identityMatrix, glm::vec3(0.018f, 0.018f, 0.018f));
    angleInRadians = glm::radians(leftBaseRotationAngle + 27.0f);
    rotateXMatrix = glm::rotate(identityMatrix, angleInRadians,
                                glm::vec3(1.0f, 0.0f, 0.0f));
    translateMatrix =
        glm::translate(identityMatrix, glm::vec3(-6.5, -0.4, 2.5));
    RotateTranslateMatrix =
        glm::translate(identityMatrix, glm::vec3(-6.5, -2.0, 3.0));
    InvRotateTranslateMatrix =
        glm::translate(identityMatrix, glm::vec3(6.5, 2.0, -3.0));

    rotateYMatrix =
        glm::rotate(identityMatrix, angleInRadians,
                    glm::vec3(0.0f, 1.0f, 0.0f)); // Rotate around the y-axis
    model = RotateTranslateMatrix * rotateYMatrix * InvRotateTranslateMatrix *
            translateMatrix * scaleMatrix;

    // Define the wood color
    woodColor =
        glm::vec4(0.52f, 0.37f, 0.26f, 1.0f); // Medium brown (wood-like)

    // Setting up material properties
    lightingShader.setVec3("material.ambient",
                           glm::vec3(ironColor) * 0.5f); // Darker for ambient
    lightingShader.setVec3("material.diffuse",
                           glm::vec3(ironColor)); // Main color for diffuse
    lightingShader.setVec3(
        "material.specular",
        glm::vec3(0.2f, 0.2f, 0.2f)); // Subtle highlights for specular
    lightingShader.setFloat("material.shininess", 16.0f);

    lightingShader.setMat4("model", model);

    // Bind VAO
    glBindVertexArray(VAOrbg);

    // Draw the object
    glDrawElements(GL_TRIANGLES, rbgindices.size(), GL_UNSIGNED_INT, 0);

    //---------------------------second missile

    RotateTranslateMatrix = glm::mat4(1.0f);
    InvRotateTranslateMatrix = glm::mat4(1.0f);
    // gun base

    // Use shader program
    lightingShader.use();

    // First object
    scaleMatrix = glm::scale(identityMatrix, glm::vec3(0.018f, 0.018f, 0.018f));
    angleInRadians =
        glm::radians(rightBaseRotationAngle); // Use the common rotation angle
    translateMatrix = glm::translate(identityMatrix, glm::vec3(1.0, -0.4, 2.5));
    RotateTranslateMatrix =
        glm::translate(identityMatrix, glm::vec3(1.05, -2.05, 3.05));
    InvRotateTranslateMatrix =
        glm::translate(identityMatrix, glm::vec3(-1.05, 2.05, -3.05));

    rotateYMatrix =
        glm::rotate(identityMatrix, angleInRadians,
                    glm::vec3(0.0f, 1.0f, 0.0f)); // Rotate around the y-axis
    model = RotateTranslateMatrix * rotateYMatrix * InvRotateTranslateMatrix *
            translateMatrix * scaleMatrix;

    // Define the wood color
    ironColor = glm::vec4(0.56f, 0.57f, 0.58f, 1.0f); // Metallic gray

    // Setting up material properties
    lightingShader.setVec3("material.ambient",
                           glm::vec3(ironColor) * 0.5f); // Darker for ambient
    lightingShader.setVec3("material.diffuse",
                           glm::vec3(ironColor)); // Main color for diffuse
    lightingShader.setVec3(
        "material.specular",
        glm::vec3(0.2f, 0.2f, 0.2f)); // Subtle highlights for specular
    lightingShader.setFloat("material.shininess", 16.0f);

    lightingShader.setMat4("model", model);

    // Bind VAO
    glBindVertexArray(VAOrb);

    // Draw the object
    glDrawElements(GL_TRIANGLES, rbindices.size(), GL_UNSIGNED_INT, 0);

    // Calculate new axes after rotation of the first object
    glm::vec3 xAxis(1.0f, 0.0f, 0.0f); // Local X-axis
    glm::vec3 yAxis(0.0f, 1.0f, 0.0f); // Local Y-axis

    // Transform the axes using the rotation matrix
    glm::vec3 newXAxis =
        glm::vec3(rotateYMatrix * glm::vec4(xAxis, 0.0f)); // Transformed X-axis
    glm::vec3 newYAxis =
        glm::vec3(rotateYMatrix * glm::vec4(yAxis, 0.0f)); // Transformed Y-axis

    //------------------------------------------------------------------------------------

    // Second object
    scaleMatrix = glm::scale(identityMatrix, glm::vec3(0.018f, 0.018f, 0.018f));
    angleInRadians = glm::radians(0.0f);
    rotateXMatrix = glm::rotate(identityMatrix, angleInRadians,
                                glm::vec3(1.0f, 0.0f, 0.0f));
    translateMatrix = glm::translate(identityMatrix, glm::vec3(1.0, -0.4, 2.5));
    model = rotateXMatrix * translateMatrix * scaleMatrix;

    // Setting up material properties
    lightingShader.setVec3("material.ambient",
                           glm::vec3(ironColor) * 0.5f); // Darker for ambient
    lightingShader.setVec3("material.diffuse",
                           glm::vec3(ironColor)); // Main color for diffuse
    lightingShader.setVec3(
        "material.specular",
        glm::vec3(0.2f, 0.2f, 0.2f)); // Subtle highlights for specular
    lightingShader.setFloat("material.shininess", 16.0f);

    lightingShader.setMat4("model", model);

    // Bind VAO
    glBindVertexArray(VAOrbb);

    // Draw the object
    glDrawElements(GL_TRIANGLES, rbbindices.size(), GL_UNSIGNED_INT, 0);

    //------------------------------------------------------------------------------------

    // right Gun

    // Right gun transformation
    scaleMatrix = glm::scale(identityMatrix, glm::vec3(0.018f, 0.018f, 0.018f));
    angleInRadians =
        glm::radians(rightBaseRotationAngle); // Rotation angle for the gun
    translateMatrix = glm::translate(identityMatrix, glm::vec3(1.0, -0.4, 2.5));
    RotateTranslateMatrix =
        glm::translate(identityMatrix, glm::vec3(1, -0.75, 3.0));
    InvRotateTranslateMatrix =
        glm::translate(identityMatrix, glm::vec3(-1, 0.75, -3.0));

    rotateYMatrix =
        glm::rotate(identityMatrix, angleInRadians,
                    glm::vec3(0.0f, 1.0f, 0.0f)); // Rotate around the y-axis
    model = RotateTranslateMatrix * rotateYMatrix * InvRotateTranslateMatrix *
            translateMatrix * scaleMatrix;

    // Define the wood color
    woodColor =
        glm::vec4(0.52f, 0.37f, 0.26f, 1.0f); // Medium brown (wood-like)

    // Setting up material properties
    lightingShader.setVec3("material.ambient",
                           glm::vec3(ironColor) * 0.5f); // Darker for ambient
    lightingShader.setVec3("material.diffuse",
                           glm::vec3(ironColor)); // Main color for diffuse
    lightingShader.setVec3(
        "material.specular",
        glm::vec3(0.2f, 0.2f, 0.2f)); // Subtle highlights for specular
    lightingShader.setFloat("material.shininess", 16.0f);

    lightingShader.setMat4("model", model);

    // Bind VAO
    glBindVertexArray(VAOrbg);

    // Draw the object
    glDrawElements(GL_TRIANGLES, rbgindices.size(), GL_UNSIGNED_INT, 0);

    glBindVertexArray(0); // Optional for safety

    // Removed the two solid trees (tree1 and tree2) as requested.

    // Removed the two extra solid trees as requested

    // Switch back to lightingShader for missiles
    lightingShader.use();
    lightingShader.setVec3("viewPos", camera.Position);
    lightingShader.setMat4("projection", projection);
    lightingShader.setMat4("view", view);

    pointlight1.setUpPointLight(lightingShader);
    pointlight2.setUpPointLight(lightingShader);
    pointlight3.setUpPointLight(lightingShader);
    spotlight1.setUpspotLight(lightingShader);
    spotlight2.setUpspotLight(lightingShader);

    lightingShader.setVec3("directionalLight.direction", 0.0f, -3.0f, 0.0f);
    lightingShader.setVec3("directionalLight.ambient", .5f, .5f, .5f);
    lightingShader.setVec3("directionalLight.diffuse", .8f, .8f, .8f);
    lightingShader.setVec3("directionalLight.specular", 1.0f, 1.0f, 1.0f);
    lightingShader.setBool("directionLightOn", true);

    //-------------------------------------missile 1
    // Pre-compute dynamic orientation so missile nose tracks velocity direction
    glm::mat4 dynRot      = identityMatrix;
    glm::vec3 mslVelDir   = glm::vec3(0.0f, 1.0f, 0.0f); // default: barrel up
    glm::vec3 mslWCenter  = glm::vec3(0.0f);  // reused per missile below
    if (mslFiring) {
      float A   = glm::radians(launchAngle1);
      float vy  = mslVel1.y,  vz = mslVel1.z;
      // Map obj-space velocity to world space (45° X-tilt + Y-rotation)
      glm::vec3 worldVel(
          0.7071f * sinf(A) * (vy + vz),
          0.7071f * (vy - vz),
          0.7071f * cosf(A) * (vy + vz));
      mslVelDir = glm::normalize(worldVel);
      glm::vec3 modelUp(0.0f, 1.0f, 0.0f);
      float d = glm::clamp(glm::dot(modelUp, mslVelDir), -1.0f, 1.0f);
      if (d > 0.9999f) {
        dynRot = identityMatrix;
      } else if (d < -0.9999f) {
        dynRot = glm::rotate(identityMatrix, glm::radians(180.0f),
                             glm::vec3(1.0f, 0.0f, 0.0f));
      } else {
        glm::vec3 ax = glm::normalize(glm::cross(modelUp, mslVelDir));
        dynRot = glm::rotate(identityMatrix, acosf(d), ax);
      }
    }

    // missile 1

    // Compute the model matrix
    glm::mat4 translatemMatrix =
        glm::translate(identityMatrix, objectPosition1);
    glm::mat4 scalemMatrix =
        glm::scale(identityMatrix, glm::vec3(0.0125f, 0.0125f, 0.0125f));
    angleInRadians = glm::radians(45.0); // Use the common rotation angle
    if (objectPosition1.y > -0.449f && !launched1) {
      launched1 = true;
      launchAngle1 = leftBaseRotationAngle;
    }
    float angleToUse1 = launched1 ? launchAngle1 : leftBaseRotationAngle;
    float angle = glm::radians(angleToUse1);
    RotateTranslateMatrix =
        glm::translate(identityMatrix, glm::vec3(-6.5, -0.4, 2.5));
    glm::mat4 pivotTranslateMatrix =
        glm::translate(identityMatrix, glm::vec3(-6.5, -2.0, 3.0));
    glm::mat4 invPivotTranslateMatrix =
        glm::translate(identityMatrix, glm::vec3(6.5, 2.0, -3.0));
    InvRotateTranslateMatrix =
        glm::translate(identityMatrix, glm::vec3(6.5, 0.4, -2.5));

    rotateXMatrix =
        glm::rotate(identityMatrix, angleInRadians,
                    glm::vec3(1.0f, 0.0f, 0.0f)); // Rotate around the y-axis
    glm::mat4 rotatetest =
        glm::rotate(identityMatrix, angle,
                    glm::vec3(0.0f, 1.0f, 0.0f)); // Rotate around the y-axis

    // model = RotateTranslateMatrix * rotateXMatrix * InvRotateTranslateMatrix
    // * translateMatrix * scaleMatrix;

    glm::mat4 mmodel = pivotTranslateMatrix * rotatetest *
                       invPivotTranslateMatrix * RotateTranslateMatrix *
                       rotateXMatrix * InvRotateTranslateMatrix *
                       translatemMatrix * scalemMatrix;

    // Dynamic orientation: nose follows velocity during flight
    if (mslFiring) {
      glm::vec4 wc = mmodel * glm::vec4(13.648f, 13.648f, 13.648f, 1.0f);
      mslWCenter = glm::vec3(wc.x, wc.y, wc.z);
      mmodel = glm::translate(identityMatrix, mslWCenter) * dynRot *
               glm::scale(identityMatrix, glm::vec3(0.0125f)) *
               glm::translate(identityMatrix, glm::vec3(-13.648f, -13.648f, -13.648f));
    }

    lightingShader.setVec3("material.ambient",  glm::vec3(ironColor));
    lightingShader.setVec3("material.diffuse",  glm::vec3(ironColor));
    lightingShader.setVec3("material.specular", glm::vec3(0.2f, 0.2f, 0.2f));
    lightingShader.setFloat("material.shininess", 16.0f);
    lightingShader.setMat4("model", mmodel);
    glBindVertexArray(VAOm);
    glDrawElements(GL_TRIANGLES, missileindices.size(), GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);

    // Static flame (on launcher, not during flight)
    if (!mslFiring && objectPosition1.y > -0.449f) {
      float pulse = 1.0f + 0.3f * sin(glfwGetTime() * 20.0f);
      glm::mat4 flameModel =
          pivotTranslateMatrix * rotatetest * invPivotTranslateMatrix *
          RotateTranslateMatrix * rotateXMatrix * InvRotateTranslateMatrix *
          translatemMatrix *
          glm::translate(identityMatrix, glm::vec3(0.1706f, 0.0f, 0.1706f)) *
          glm::scale(identityMatrix, glm::vec3(0.08f, 0.4f * pulse, 0.08f)) *
          glm::rotate(identityMatrix, glm::radians(180.0f), glm::vec3(1,0,0));
      lightingShader.setMat4("model", flameModel);
      lightingShader.setVec3("material.ambient",  glm::vec3(1.0f, 0.4f, 0.0f));
      lightingShader.setVec3("material.diffuse",  glm::vec3(1.0f, 0.4f, 0.0f));
      lightingShader.setVec3("material.specular", glm::vec3(1.0f, 1.0f, 1.0f));
      lightingShader.setFloat("material.shininess", 4.0f);
      glBindVertexArray(coneVAO);
      glDrawElements(GL_TRIANGLES, coneIndicesCount, GL_UNSIGNED_INT, 0);
      glBindVertexArray(0);
    }
    // Dynamic flame (in flight) – tail trails opposite to velocity
    if (mslFiring) {
      float pulse = 1.0f + 0.3f * sinf(glfwGetTime() * 20.0f);
      glm::vec3 tailPos = mslWCenter - mslVelDir * 0.1706f;
      glm::mat4 dynFlame =
          glm::translate(identityMatrix, tailPos) * dynRot *
          glm::scale(identityMatrix, glm::vec3(0.08f, 0.4f * pulse, 0.08f)) *
          glm::rotate(identityMatrix, glm::radians(180.0f), glm::vec3(1,0,0));
      lightingShader.setVec3("material.ambient",  glm::vec3(1.0f, 0.4f, 0.0f));
      lightingShader.setVec3("material.diffuse",  glm::vec3(1.0f, 0.4f, 0.0f));
      lightingShader.setVec3("material.specular", glm::vec3(1.0f, 1.0f, 1.0f));
      lightingShader.setFloat("material.shininess", 4.0f);
      lightingShader.setMat4("model", dynFlame);
      glBindVertexArray(coneVAO);
      glDrawElements(GL_TRIANGLES, coneIndicesCount, GL_UNSIGNED_INT, 0);
      glBindVertexArray(0);
    }

    //-------------------------------------missile 2
    // Compute the model matrix
    translatemMatrix = glm::translate(identityMatrix, objectPosition2);
    scalemMatrix =
        glm::scale(identityMatrix, glm::vec3(0.0125f, 0.0125f, 0.0125f));
    angleInRadians = glm::radians(45.0); // Use the common rotation angle
    if (objectPosition2.y > -0.449f && !launched2) {
      launched2 = true;
      launchAngle2 = leftBaseRotationAngle;
    }
    float angleToUse2 = launched2 ? launchAngle2 : leftBaseRotationAngle;
    angle = glm::radians(angleToUse2);
    RotateTranslateMatrix =
        glm::translate(identityMatrix, glm::vec3(-6.5, -0.4, 2.5));
    InvRotateTranslateMatrix =
        glm::translate(identityMatrix, glm::vec3(6.5, 0.4, -2.5));

    rotateXMatrix =
        glm::rotate(identityMatrix, angleInRadians,
                    glm::vec3(1.0f, 0.0f, 0.0f)); // Rotate around the y-axis
    rotatetest =
        glm::rotate(identityMatrix, angle,
                    glm::vec3(0.0f, 1.0f, 0.0f)); // Rotate around the y-axis

    // model = RotateTranslateMatrix * rotateXMatrix * InvRotateTranslateMatrix
    // * translateMatrix * scaleMatrix;

    mmodel = pivotTranslateMatrix * rotatetest * invPivotTranslateMatrix *
             RotateTranslateMatrix * rotateXMatrix * InvRotateTranslateMatrix *
             translatemMatrix * scalemMatrix;

    if (mslFiring) {
      glm::vec4 wc = mmodel * glm::vec4(13.648f, 13.648f, 13.648f, 1.0f);
      mslWCenter = glm::vec3(wc.x, wc.y, wc.z);
      mmodel = glm::translate(identityMatrix, mslWCenter) * dynRot *
               glm::scale(identityMatrix, glm::vec3(0.0125f)) *
               glm::translate(identityMatrix, glm::vec3(-13.648f, -13.648f, -13.648f));
    }

    lightingShader.setVec3("material.ambient",  glm::vec3(ironColor));
    lightingShader.setVec3("material.diffuse",  glm::vec3(ironColor));
    lightingShader.setVec3("material.specular", glm::vec3(0.2f, 0.2f, 0.2f));
    lightingShader.setFloat("material.shininess", 16.0f);
    lightingShader.setMat4("model", mmodel);
    glBindVertexArray(VAOm);
    glDrawElements(GL_TRIANGLES, missileindices.size(), GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);

    if (!mslFiring && objectPosition2.y > -0.449f) {
      float pulse = 1.0f + 0.3f * sin(glfwGetTime() * 20.0f);
      glm::mat4 flameModel =
          pivotTranslateMatrix * rotatetest * invPivotTranslateMatrix *
          RotateTranslateMatrix * rotateXMatrix * InvRotateTranslateMatrix *
          translatemMatrix *
          glm::translate(identityMatrix, glm::vec3(0.1706f, 0.0f, 0.1706f)) *
          glm::scale(identityMatrix, glm::vec3(0.08f, 0.4f * pulse, 0.08f)) *
          glm::rotate(identityMatrix, glm::radians(180.0f), glm::vec3(1,0,0));
      lightingShader.setMat4("model", flameModel);
      lightingShader.setVec3("material.ambient",  glm::vec3(1.0f, 0.4f, 0.0f));
      lightingShader.setVec3("material.diffuse",  glm::vec3(1.0f, 0.4f, 0.0f));
      lightingShader.setVec3("material.specular", glm::vec3(1.0f, 1.0f, 1.0f));
      lightingShader.setFloat("material.shininess", 4.0f);
      glBindVertexArray(coneVAO);
      glDrawElements(GL_TRIANGLES, coneIndicesCount, GL_UNSIGNED_INT, 0);
      glBindVertexArray(0);
    }
    if (mslFiring) {
      float pulse = 1.0f + 0.3f * sinf(glfwGetTime() * 20.0f);
      glm::vec3 tailPos = mslWCenter - mslVelDir * 0.1706f;
      glm::mat4 dynFlame =
          glm::translate(identityMatrix, tailPos) * dynRot *
          glm::scale(identityMatrix, glm::vec3(0.08f, 0.4f * pulse, 0.08f)) *
          glm::rotate(identityMatrix, glm::radians(180.0f), glm::vec3(1,0,0));
      lightingShader.setVec3("material.ambient",  glm::vec3(1.0f, 0.4f, 0.0f));
      lightingShader.setVec3("material.diffuse",  glm::vec3(1.0f, 0.4f, 0.0f));
      lightingShader.setVec3("material.specular", glm::vec3(1.0f, 1.0f, 1.0f));
      lightingShader.setFloat("material.shininess", 4.0f);
      lightingShader.setMat4("model", dynFlame);
      glBindVertexArray(coneVAO);
      glDrawElements(GL_TRIANGLES, coneIndicesCount, GL_UNSIGNED_INT, 0);
      glBindVertexArray(0);
    }

    //-------------------------------------missile 3
    // Compute the model matrix
    translatemMatrix = glm::translate(identityMatrix, objectPosition3);
    scalemMatrix =
        glm::scale(identityMatrix, glm::vec3(0.0125f, 0.0125f, 0.0125f));
    angleInRadians = glm::radians(45.0); // Use the common rotation angle
    if (objectPosition3.y > -0.469f && !launched3) {
      launched3 = true;
      launchAngle3 = leftBaseRotationAngle;
    }
    float angleToUse3 = launched3 ? launchAngle3 : leftBaseRotationAngle;
    angle = glm::radians(angleToUse3);
    RotateTranslateMatrix =
        glm::translate(identityMatrix, glm::vec3(-6.5, -0.4, 2.5));
    InvRotateTranslateMatrix =
        glm::translate(identityMatrix, glm::vec3(6.5, 0.4, -2.5));

    rotateXMatrix =
        glm::rotate(identityMatrix, angleInRadians,
                    glm::vec3(1.0f, 0.0f, 0.0f)); // Rotate around the y-axis
    rotatetest =
        glm::rotate(identityMatrix, angle,
                    glm::vec3(0.0f, 1.0f, 0.0f)); // Rotate around the y-axis

    // model = RotateTranslateMatrix * rotateXMatrix * InvRotateTranslateMatrix
    // * translateMatrix * scaleMatrix;

    mmodel = pivotTranslateMatrix * rotatetest * invPivotTranslateMatrix *
             RotateTranslateMatrix * rotateXMatrix * InvRotateTranslateMatrix *
             translatemMatrix * scalemMatrix;

    if (mslFiring) {
      glm::vec4 wc = mmodel * glm::vec4(13.648f, 13.648f, 13.648f, 1.0f);
      mslWCenter = glm::vec3(wc.x, wc.y, wc.z);
      mmodel = glm::translate(identityMatrix, mslWCenter) * dynRot *
               glm::scale(identityMatrix, glm::vec3(0.0125f)) *
               glm::translate(identityMatrix, glm::vec3(-13.648f, -13.648f, -13.648f));
    }

    lightingShader.setVec3("material.ambient",  glm::vec3(ironColor));
    lightingShader.setVec3("material.diffuse",  glm::vec3(ironColor));
    lightingShader.setVec3("material.specular", glm::vec3(0.2f, 0.2f, 0.2f));
    lightingShader.setFloat("material.shininess", 16.0f);
    lightingShader.setMat4("model", mmodel);
    glBindVertexArray(VAOm);
    glDrawElements(GL_TRIANGLES, missileindices.size(), GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);

    if (!mslFiring && objectPosition3.y > -0.469f) {
      float pulse = 1.0f + 0.3f * sin(glfwGetTime() * 20.0f);
      glm::mat4 flameModel =
          pivotTranslateMatrix * rotatetest * invPivotTranslateMatrix *
          RotateTranslateMatrix * rotateXMatrix * InvRotateTranslateMatrix *
          translatemMatrix *
          glm::translate(identityMatrix, glm::vec3(0.1706f, 0.0f, 0.1706f)) *
          glm::scale(identityMatrix, glm::vec3(0.08f, 0.4f * pulse, 0.08f)) *
          glm::rotate(identityMatrix, glm::radians(180.0f), glm::vec3(1,0,0));
      lightingShader.setMat4("model", flameModel);
      lightingShader.setVec3("material.ambient",  glm::vec3(1.0f, 0.4f, 0.0f));
      lightingShader.setVec3("material.diffuse",  glm::vec3(1.0f, 0.4f, 0.0f));
      lightingShader.setVec3("material.specular", glm::vec3(1.0f, 1.0f, 1.0f));
      lightingShader.setFloat("material.shininess", 4.0f);
      glBindVertexArray(coneVAO);
      glDrawElements(GL_TRIANGLES, coneIndicesCount, GL_UNSIGNED_INT, 0);
      glBindVertexArray(0);
    }
    if (mslFiring) {
      float pulse = 1.0f + 0.3f * sinf(glfwGetTime() * 20.0f);
      glm::vec3 tailPos = mslWCenter - mslVelDir * 0.1706f;
      glm::mat4 dynFlame =
          glm::translate(identityMatrix, tailPos) * dynRot *
          glm::scale(identityMatrix, glm::vec3(0.08f, 0.4f * pulse, 0.08f)) *
          glm::rotate(identityMatrix, glm::radians(180.0f), glm::vec3(1,0,0));
      lightingShader.setVec3("material.ambient",  glm::vec3(1.0f, 0.4f, 0.0f));
      lightingShader.setVec3("material.diffuse",  glm::vec3(1.0f, 0.4f, 0.0f));
      lightingShader.setVec3("material.specular", glm::vec3(1.0f, 1.0f, 1.0f));
      lightingShader.setFloat("material.shininess", 4.0f);
      lightingShader.setMat4("model", dynFlame);
      glBindVertexArray(coneVAO);
      glDrawElements(GL_TRIANGLES, coneIndicesCount, GL_UNSIGNED_INT, 0);
      glBindVertexArray(0);
    }

    //-------------------------------------missile 4
    // Compute the model matrix
    translatemMatrix = glm::translate(identityMatrix, objectPosition4);
    scalemMatrix =
        glm::scale(identityMatrix, glm::vec3(0.0125f, 0.0125f, 0.0125f));
    angleInRadians = glm::radians(45.0); // Use the common rotation angle
    if (objectPosition4.y > -0.469f && !launched4) {
      launched4 = true;
      launchAngle4 = leftBaseRotationAngle;
    }
    float angleToUse4 = launched4 ? launchAngle4 : leftBaseRotationAngle;
    angle = glm::radians(angleToUse4);
    RotateTranslateMatrix =
        glm::translate(identityMatrix, glm::vec3(-6.5, -0.4, 2.5));
    InvRotateTranslateMatrix =
        glm::translate(identityMatrix, glm::vec3(6.5, 0.4, -2.5));

    rotateXMatrix =
        glm::rotate(identityMatrix, angleInRadians,
                    glm::vec3(1.0f, 0.0f, 0.0f)); // Rotate around the y-axis
    rotatetest =
        glm::rotate(identityMatrix, angle,
                    glm::vec3(0.0f, 1.0f, 0.0f)); // Rotate around the y-axis

    // model = RotateTranslateMatrix * rotateXMatrix * InvRotateTranslateMatrix
    // * translateMatrix * scaleMatrix;

    mmodel = pivotTranslateMatrix * rotatetest * invPivotTranslateMatrix *
             RotateTranslateMatrix * rotateXMatrix * InvRotateTranslateMatrix *
             translatemMatrix * scalemMatrix;

    if (mslFiring) {
      glm::vec4 wc = mmodel * glm::vec4(13.648f, 13.648f, 13.648f, 1.0f);
      mslWCenter = glm::vec3(wc.x, wc.y, wc.z);
      mmodel = glm::translate(identityMatrix, mslWCenter) * dynRot *
               glm::scale(identityMatrix, glm::vec3(0.0125f)) *
               glm::translate(identityMatrix, glm::vec3(-13.648f, -13.648f, -13.648f));
    }

    lightingShader.setVec3("material.ambient",  glm::vec3(ironColor));
    lightingShader.setVec3("material.diffuse",  glm::vec3(ironColor));
    lightingShader.setVec3("material.specular", glm::vec3(0.2f, 0.2f, 0.2f));
    lightingShader.setFloat("material.shininess", 16.0f);
    lightingShader.setMat4("model", mmodel);
    glBindVertexArray(VAOm);
    glDrawElements(GL_TRIANGLES, missileindices.size(), GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);

    if (!mslFiring && objectPosition4.y > -0.469f) {
      float pulse = 1.0f + 0.3f * sin(glfwGetTime() * 20.0f);
      glm::mat4 flameModel =
          pivotTranslateMatrix * rotatetest * invPivotTranslateMatrix *
          RotateTranslateMatrix * rotateXMatrix * InvRotateTranslateMatrix *
          translatemMatrix *
          glm::translate(identityMatrix, glm::vec3(0.1706f, 0.0f, 0.1706f)) *
          glm::scale(identityMatrix, glm::vec3(0.08f, 0.4f * pulse, 0.08f)) *
          glm::rotate(identityMatrix, glm::radians(180.0f), glm::vec3(1,0,0));
      lightingShader.setMat4("model", flameModel);
      lightingShader.setVec3("material.ambient",  glm::vec3(1.0f, 0.4f, 0.0f));
      lightingShader.setVec3("material.diffuse",  glm::vec3(1.0f, 0.4f, 0.0f));
      lightingShader.setVec3("material.specular", glm::vec3(1.0f, 1.0f, 1.0f));
      lightingShader.setFloat("material.shininess", 4.0f);
      glBindVertexArray(coneVAO);
      glDrawElements(GL_TRIANGLES, coneIndicesCount, GL_UNSIGNED_INT, 0);
      glBindVertexArray(0);
    }
    if (mslFiring) {
      float pulse = 1.0f + 0.3f * sinf(glfwGetTime() * 20.0f);
      glm::vec3 tailPos = mslWCenter - mslVelDir * 0.1706f;
      glm::mat4 dynFlame =
          glm::translate(identityMatrix, tailPos) * dynRot *
          glm::scale(identityMatrix, glm::vec3(0.08f, 0.4f * pulse, 0.08f)) *
          glm::rotate(identityMatrix, glm::radians(180.0f), glm::vec3(1,0,0));
      lightingShader.setVec3("material.ambient",  glm::vec3(1.0f, 0.4f, 0.0f));
      lightingShader.setVec3("material.diffuse",  glm::vec3(1.0f, 0.4f, 0.0f));
      lightingShader.setVec3("material.specular", glm::vec3(1.0f, 1.0f, 1.0f));
      lightingShader.setFloat("material.shininess", 4.0f);
      lightingShader.setMat4("model", dynFlame);
      glBindVertexArray(coneVAO);
      glDrawElements(GL_TRIANGLES, coneIndicesCount, GL_UNSIGNED_INT, 0);
      glBindVertexArray(0);
    }

    // Update the parameter t
    if (glfwGetKey(window, GLFW_KEY_O) == GLFW_PRESS) {
      objectPosition2.y +=
          translationSpeed * deltaTime; // Move along the Y axis
      objectPosition1.y +=
          translationSpeed * deltaTime; // Move along the Z axis
      objectPosition3.y += translationSpeed * deltaTime;
      objectPosition4.y += translationSpeed * deltaTime;
    }

    // Compute the model matrix
    translatemMatrix =
        glm::translate(identityMatrix, glm::vec3(-13.0, -2.0, 6.5));
    scalemMatrix =
        glm::scale(identityMatrix, glm::vec3(0.025f, 0.025f, 0.025f));

    mmodel = translatemMatrix * scalemMatrix;

    // Set up material properties
    lightingShader.setVec3("material.ambient",
                           glm::vec3(ironColor)); // Ambient light
    lightingShader.setVec3("material.diffuse",
                           glm::vec3(ironColor)); // Diffuse light
    lightingShader.setVec3("material.specular",
                           glm::vec3(0.2f, 0.2f, 0.2f)); // Specular highlights
    lightingShader.setFloat("material.shininess", 16.0f);

    // Pass the model matrix to the shader
    lightingShader.setMat4("model", mmodel);

    // Bind VAO
    glBindVertexArray(VAOm);

    // Draw the object
    glDrawElements(GL_TRIANGLES, missileindices.size(), GL_UNSIGNED_INT, 0);

    // Unbind VAO (optional for safety)
    glBindVertexArray(0);

    // Compute the model matrix
    translatemMatrix =
        glm::translate(identityMatrix, glm::vec3(6.5, -2.0, 6.0));
    scalemMatrix =
        glm::scale(identityMatrix, glm::vec3(0.025f, 0.025f, 0.025f));

    mmodel = translatemMatrix * scalemMatrix;

    // Set up material properties
    lightingShader.setVec3("material.ambient",
                           glm::vec3(ironColor)); // Ambient light
    lightingShader.setVec3("material.diffuse",
                           glm::vec3(ironColor)); // Diffuse light
    lightingShader.setVec3("material.specular",
                           glm::vec3(0.2f, 0.2f, 0.2f)); // Specular highlights
    lightingShader.setFloat("material.shininess", 16.0f);

    // Pass the model matrix to the shader
    lightingShader.setMat4("model", mmodel);

    // Bind VAO
    glBindVertexArray(VAOm);

    // Draw the object
    glDrawElements(GL_TRIANGLES, missileindices.size(), GL_UNSIGNED_INT, 0);

    // Unbind VAO (optional for safety)
    glBindVertexArray(0);

    if (openDoor) {
      if (doorAngle < 90.0f) {
        doorAngle += 0.25;
      }
    }

    if (!openDoor) {
      if (doorAngle > 0.0f) {
        doorAngle -= 0.25;
      }
    }

    lightingShader.use();
    lightingShader.setVec3("viewPos", camera.Position);
    lightingShader.setMat4("projection", projection);
    lightingShader.setMat4("view", view);

    // point light 1
    pointlight1.setUpPointLight(lightingShader);
    // point light 2
    pointlight2.setUpPointLight(lightingShader);
    // point light 3
    pointlight3.setUpPointLight(lightingShader);

    spotlight1.setUpspotLight(lightingShader);

    // constantShader.setMat4("view", view);
    // lightingShader.setMat4("view", view);

    for (unsigned int i = 0; i < 3; i++) {
      scaleMatrix = glm::scale(identityMatrix, glm::vec3(0.5f, 0.5f, 0.5f));
      translateMatrix = glm::translate(
          identityMatrix,
          glm::vec3(0.12, -0.1, 0) +
              pointLightPositions[i]); // Adjust the offset as needed
      model = translateMatrix * scaleMatrix;
      drawSphere(VAO_S, lightingShader, glm::vec3(1.0f, 1.0f, 0.8f), model,
                 indices_s);
    }

    ourShader.use();
    ourShader.setMat4("projection", projection);

    // glm::mat4 view = basic_camera.createViewMatrix();
    ourShader.setMat4("view", view);

    glBindVertexArray(lightCubeVAO);
    for (unsigned int i = 0; i < 3; i++) {
      model = glm::mat4(1.0f);
      model = glm::translate(model,
                             glm::vec3(0.05, -3.5, 0) + pointLightPositions[i]);
      model = glm::scale(model,
                         glm::vec3(0.1f, 3.0f, 0.1f)); // Make it a smaller cube
      ourShader.setMat4("model", model);
      ourShader.setVec4("color", glm::vec4(0.3f, 0.3f, 0.3f, 1.0f));
      glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
      // glDrawArrays(GL_TRIANGLES, 0, 36);
    }

    // glfw: swap buffers and poll IO events (keys pressed/released, mouse moved
    // etc.)
    // -------------------------------------------------------------------------------
    glfwSwapBuffers(window);
    glfwPollEvents();
  }

  // optional: de-allocate all resources once they've outlived their purpose:
  // ------------------------------------------------------------------------
  glDeleteVertexArrays(1, &cubeVAO);
  glDeleteVertexArrays(1, &lightCubeVAO);
  glDeleteBuffers(1, &cubeVAO);
  glDeleteBuffers(1, &cubeVAO);

  // glfw: terminate, clearing all previously allocated GLFW resources.
  // ------------------------------------------------------------------
  glfwTerminate();
  return 0;
}

// process all input: query GLFW whether relevant keys are pressed/released this
// frame and react accordingly
// ---------------------------------------------------------------------------------------------------------

void lightEffect(unsigned int VAO, Shader lightShader, glm::mat4 model,
                 glm::vec3 color) {
  lightShader.use();
  lightShader.setVec3("material.ambient", color);
  lightShader.setVec3("material.diffuse", color);
  lightShader.setVec3("material.specular", glm::vec3(0.5f, 0.5f, 0.5f));
  lightShader.setFloat("material.shininess", 32.0f);

  lightShader.setMat4("model", model);
  glBindVertexArray(VAO);
  glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, (void *)0);
  glBindVertexArray(0);
}

void drawCube(unsigned int VAO, Shader shader, glm::mat4 model,
              glm::vec4 color) {
  shader.setMat4("model", model);
  shader.setVec4("color", color);
  glBindVertexArray(VAO);
  glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
}
void read_file(string file_name, vector<float> &vec) {
  ifstream file(file_name);
  float number;

  while (file >> number)
    vec.push_back(number);

  file.close();
}

void processInput(GLFWwindow *window) {
  if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
    glfwSetWindowShouldClose(window, true);

  if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
    camera.ProcessKeyboard(FORWARD, deltaTime);
  }
  if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
    camera.ProcessKeyboard(BACKWARD, deltaTime);
  }
  if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
    camera.ProcessKeyboard(LEFT, deltaTime);
  }
  if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
    camera.ProcessKeyboard(RIGHT, deltaTime);
  }
  if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) {
    camera.ProcessKeyboard(UP, deltaTime);
  }
  if (glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS) {
    camera.ProcessKeyboard(DOWN, deltaTime);
  }
  if (glfwGetKey(window, GLFW_KEY_T) == GLFW_PRESS) {
    camera.ProcessKeyboard(P_UP, deltaTime);
  }
  if (glfwGetKey(window, GLFW_KEY_Y) == GLFW_PRESS) {
    camera.ProcessKeyboard(P_DOWN, deltaTime);
  }
  if (glfwGetKey(window, GLFW_KEY_X) == GLFW_PRESS) {
    camera.ProcessKeyboard(Y_LEFT, deltaTime);
  }
  if (glfwGetKey(window, GLFW_KEY_C) == GLFW_PRESS) {
    camera.ProcessKeyboard(Y_RIGHT, deltaTime);
  }
  if (glfwGetKey(window, GLFW_KEY_Z) == GLFW_PRESS) {
    camera.ProcessKeyboard(R_LEFT, deltaTime);
  }
  if (glfwGetKey(window, GLFW_KEY_V) == GLFW_PRESS) {
    camera.ProcessKeyboard(R_RIGHT, deltaTime);
  }
  if (glfwGetKey(window, GLFW_KEY_O) == GLFW_PRESS)
    on = true;
  if (glfwGetKey(window, GLFW_KEY_P) == GLFW_PRESS)
    on = false;
  if (glfwGetKey(window, GLFW_KEY_B) == GLFW_PRESS)
    birdEye = true;
  if (glfwGetKey(window, GLFW_KEY_N) == GLFW_PRESS)
    birdEye = false;

  if (glfwGetKey(window, GLFW_KEY_L) == GLFW_PRESS) {
    leftBaseRotationAngle += 1.0f; // Increase the rotation angle
    if (leftBaseRotationAngle >= 360.0f) {
      leftBaseRotationAngle -= 360.0f; // Keep the angle within 0-360 degrees
    }
  }

  if (glfwGetKey(window, GLFW_KEY_K) == GLFW_PRESS) {
    rightBaseRotationAngle += 1.0f; // Increase the common rotation angle
    if (rightBaseRotationAngle >= 360.0f) {
      rightBaseRotationAngle -= 360.0f; // Keep the angle within 0-360 degrees
    }
  }

  /*// Update position along the diagonal of the yz-plane when the P key is
  pressed if (glfwGetKey(window, GLFW_KEY_P) == GLFW_PRESS) { objectPosition.y
  += translationSpeed * deltaTime; // Move along the Y axis objectPosition.z +=
  translationSpeed * deltaTime; // Move along the Z axis
  }*/

  if (birdEye) {
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
      cameraPos.z -= birdEyeSpeed * deltaTime;
      target.z -= birdEyeSpeed * deltaTime;
      if (cameraPos.z <= 4.0) {
        cameraPos.z = 4.0;
      }

      if (target.z <= -3.5) {
        target.z = -3.5;
      }
    }
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
      cameraPos.z += birdEyeSpeed * deltaTime;
      target.z += birdEyeSpeed * deltaTime;
      /*cout << "tgt: " << target.z << endl;
      cout << "pos: " << cameraPos.z << endl;*/
      if (cameraPos.z >= 13.5) {
        cameraPos.z = 13.5;
      }
      if (target.z >= 6.0) {
        target.z = 6.0;
      }
    }
  }
}

// glfw: whenever the window size changed (by OS or user resize) this callback
// function executes
// ---------------------------------------------------------------------------------------------

void key_callback(GLFWwindow *window, int key, int scancode, int action,
                  int mods) {

  if (glfwGetKey(window, GLFW_KEY_1) == GLFW_PRESS) {
    if (dl)
      dl = false;
    else
      dl = true;
  }
  if (glfwGetKey(window, GLFW_KEY_2) == GLFW_PRESS) {
    if (point1) {
      point1 = false;
      pointlight1.turnOff();
    } else {
      point1 = true;
      pointlight1.turnOn();
    }
  }
  if (glfwGetKey(window, GLFW_KEY_3) == GLFW_PRESS) {
    if (point2) {
      point2 = false;
      pointlight2.turnOff();
    } else {
      point2 = true;
      pointlight2.turnOn();
    }
  }
  if (glfwGetKey(window, GLFW_KEY_5) == GLFW_PRESS) {
    if (spt) {
      spt = false;
      spotlight1.turnOff();
      spotlight2.turnOff();
    } else {
      spt = true;
      spotlight1.turnOn();
      spotlight2.turnOn();
    }
  }
  if (glfwGetKey(window, GLFW_KEY_4) == GLFW_PRESS) {
    if (point3) {
      point3 = false;
      pointlight3.turnOff();
    } else {
      point3 = true;
      pointlight3.turnOn();
    }
  }

  if (glfwGetKey(window, GLFW_KEY_6) == GLFW_PRESS) {
    if (ambientToggle) {
      pointlight1.turnAmbientOff();
      pointlight2.turnAmbientOff();
      ambientToggle = false;
    } else {
      pointlight1.turnAmbientOn();
      pointlight2.turnAmbientOn();
      ambientToggle = true;
    }
  }

  if (glfwGetKey(window, GLFW_KEY_7) == GLFW_PRESS) {
    if (diffuseToggle) {
      pointlight1.turnDiffuseOff();
      pointlight2.turnDiffuseOff();
      // d_def_on = 0.0f;

      diffuseToggle = false;
    } else {
      pointlight1.turnDiffuseOn();
      pointlight2.turnDiffuseOn();

      // d_def_on = 1.0f;
      diffuseToggle = true;
    }
  }

  if (glfwGetKey(window, GLFW_KEY_8) == GLFW_PRESS) {
    if (specularToggle) {
      pointlight1.turnSpecularOff();
      pointlight2.turnSpecularOff();
      // d_def_on = 0.0f;

      specularToggle = false;
    } else {
      pointlight1.turnSpecularOn();
      pointlight2.turnSpecularOn();

      // d_def_on = 1.0f;
      specularToggle = true;
    }
  }

  if (glfwGetKey(window, GLFW_KEY_9) == GLFW_PRESS) {
    texture_bool = !texture_bool;
  }

  if (glfwGetKey(window, GLFW_KEY_M) == GLFW_PRESS) {
    openDoor = true;
  }

  if (glfwGetKey(window, GLFW_KEY_N) == GLFW_PRESS) {
    openDoor = false;
  }

  if (key == GLFW_KEY_BACKSLASH && action == GLFW_PRESS) {
    openDoor = !openDoor;  // toggle door open/close
  }

  if (key == GLFW_KEY_I && action == GLFW_PRESS) {
    colorMode = (colorMode + 1) % 3;
    std::cout << "Color Mode: " << colorMode
              << " (0: Object, 1: Texture, 2: Mixed)" << std::endl;
  }

  if (key == GLFW_KEY_G && action == GLFW_PRESS) {
    useGouraud = !useGouraud;
    std::cout << (useGouraud ? "Using Gouraud Shading" : "Using Phong Shading")
              << std::endl;
  }

  if (key == GLFW_KEY_F && action == GLFW_PRESS) {
    // Trigger arm snap animation
    ballistaAnimating = true;
    ballistaAnimT     = 0.0f;
    // Launch arrow from trough muzzle at 30° upward toward +Z
    arrowActive = true;
    arrowPos = glm::vec3(-3.0f, 2.05f, -2.90f);
    float radA = glm::radians(30.0f);
    arrowVelocity = glm::vec3(
        0.0f,
        ARROW_SPEED * sinf(radA),   // +Y: upward component
        ARROW_SPEED * cosf(radA));  // +Z: forward (away from castle)
  }

  if (key == GLFW_KEY_P && action == GLFW_PRESS) {
    if (!mslFiring) {
      // Lock each missile's Y-rotation to current trunk angle at the moment
      // of firing so they travel in the aimed direction
      launched1 = launched2 = launched3 = launched4 = true;
      launchAngle1 = launchAngle2 = launchAngle3 = launchAngle4 =
          leftBaseRotationAngle;
      // Fire all 4 missiles along barrel (+Y in objectPosition space)
      mslFiring      = true;
      mslFiringTimer = 0.0f;
      mslVel1 = mslVel2 = mslVel3 = mslVel4 =
          glm::vec3(0.0f, MSL_LAUNCH_V, 0.0f);
    }
  }
}

void framebuffer_size_callback(GLFWwindow *window, int width, int height) {
  // make sure the viewport matches the new window dimensions; note that width
  // and height will be significantly larger than specified on retina displays.
  glViewport(0, 0, width, height);
}

// glfw: whenever the mouse moves, this callback is called
// -------------------------------------------------------
void mouse_callback(GLFWwindow *window, double xposIn, double yposIn) {
  float xpos = static_cast<float>(xposIn);
  float ypos = static_cast<float>(yposIn);

  if (firstMouse) {
    lastX = xpos;
    lastY = ypos;
    firstMouse = false;
  }

  float xoffset = xpos - lastX;
  float yoffset =
      lastY - ypos; // reversed since y-coordinates go from bottom to top

  lastX = xpos;
  lastY = ypos;

  camera.ProcessMouseMovement(xoffset, yoffset);
}

// glfw: whenever the mouse scroll wheel scrolls, this callback is called
// ----------------------------------------------------------------------
void scroll_callback(GLFWwindow *window, double xoffset, double yoffset) {
  camera.ProcessMouseScroll(static_cast<float>(yoffset));
}

unsigned int loadTexture(char const *path, GLenum textureWrappingModeS,
                         GLenum textureWrappingModeT,
                         GLenum textureFilteringModeMin,
                         GLenum textureFilteringModeMax) {
  unsigned int textureID;
  glGenTextures(1, &textureID);

  int width, height, nrComponents;
  stbi_set_flip_vertically_on_load(true);
  unsigned char *data = stbi_load(path, &width, &height, &nrComponents, 0);

  if (data) {
    GLenum format = GL_RGB; // Default format
    if (nrComponents == 1)
      format = GL_RED;
    else if (nrComponents == 3)
      format = GL_RGB;
    else if (nrComponents == 4)
      format = GL_RGBA;

    glBindTexture(GL_TEXTURE_2D, textureID);
    glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format,
                 GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, textureWrappingModeS);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, textureWrappingModeT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    textureFilteringModeMin);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                    textureFilteringModeMax);

    std::cout << "Texture loaded successfully: " << path << " (width: " << width
              << ", height: " << height << ", components: " << nrComponents
              << ")" << std::flush << std::endl;
    stbi_image_free(data);
  } else {
    std::cout << "Texture failed to load at path: " << path
              << " | Error: " << stbi_failure_reason() << std::endl;
    // Create a white fallback texture
    unsigned char white[4] = {255, 255, 255, 255};
    glBindTexture(GL_TEXTURE_2D, textureID);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 white);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  }

  return textureID;
}

void load_texture(unsigned int &texture, string image_name, GLenum format) {
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  int width, height, nrChannels;
  stbi_set_flip_vertically_on_load(true);
  unsigned char *data =
      stbi_load(image_name.c_str(), &width, &height, &nrChannels, 0);
  if (data) {
    glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format,
                 GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
  } else {
    cout << "Failed to load texture " << image_name << endl;
  }
  stbi_image_free(data);
}
