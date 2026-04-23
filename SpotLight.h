//
//  spotLight.h
//  test
//
//  Created by Nazirul Hasan on 22/9/23.
//

#ifndef spotLight_h
#define spotLight_h

#include "shader.h"
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <iostream>
#include <string>

using namespace std;

class SpotLight {
public:
  glm::vec3 position;
  glm::vec3 ambient;
  glm::vec3 diffuse;
  glm::vec3 specular;
  glm::vec3 direction;
  float k_c;
  float k_l;
  float k_q;
  float cutoff;
  int Number;

  SpotLight(float posX, float posY, float posZ, float ambR, float ambG,
            float ambB, float diffR, float diffG, float diffB, float specR,
            float specG, float specB, float constant, float linear,
            float quadratic, int num, float cutoff_angle,
            float dirX, float dirY, float dirZ) {

    position = glm::vec3(posX, posY, posZ);
    ambient = glm::vec3(ambR, ambG, ambB);
    diffuse = glm::vec3(diffR, diffG, diffB);
    specular = glm::vec3(specR, specG, specB);
    k_c = constant;
    k_l = linear;
    k_q = quadratic;
    cutoff = cutoff_angle;
    direction = glm::vec3(dirX, dirY, dirZ);
    Number = num - 1;
  }
  SpotLight() {
    position = glm::vec3(0, 0, 0);
    ambient = glm::vec3(1.0f, 1.0f, 1.0f);
    diffuse = glm::vec3(1.0f, 1.0f, 1.0f);
    specular = glm::vec3(1.0f, 1.0f, 1.0f);
    k_c = 1.0f;
    k_l = 0.09f;
    k_q = 0.032f;
    cutoff = glm::cos(glm::radians(12.5f));
    direction = glm::vec3(0, -1, 0);
    Number = 1;
  }
  void setUpspotLight(Shader &lightingShader) {
    lightingShader.use();
    string lightNumber = to_string(Number);
    lightingShader.setVec3("spotLights[" + lightNumber + "].position",
                           position);
    lightingShader.setVec3("spotLights[" + lightNumber + "].ambient",
                           ambient * ambientOn);
    lightingShader.setVec3("spotLights[" + lightNumber + "].diffuse",
                           diffuse * diffuseOn);
    lightingShader.setVec3("spotLights[" + lightNumber + "].specular",
                           specular * specularOn);
    lightingShader.setFloat("spotLights[" + lightNumber + "].k_c", k_c);
    lightingShader.setFloat("spotLights[" + lightNumber + "].k_l", k_l);
    lightingShader.setFloat("spotLights[" + lightNumber + "].k_q", k_q);
    lightingShader.setFloat("spotLights[" + lightNumber + "].cutoff",
                            cutoff);
    lightingShader.setVec3("spotLights[" + lightNumber + "].direction",
                           direction);
  }
  void turnOff() {
    ambientOn = 0.0;
    diffuseOn = 0.0;
    specularOn = 0.0;
  }
  void turnOn() {
    ambientOn = 1.0;
    diffuseOn = 1.0;
    specularOn = 1.0;
  }
  void turnAmbientOn() {
    ambientOn = 1.0;
    diffuseOn = 0.0;
    specularOn = 0.0;
  }
  void turnDiffuseOn() {
    ambientOn = 0.0;
    diffuseOn = 1.0;
    specularOn = 0.0;
  }

  void turnSpecularOn() {
    ambientOn = 0.0;
    diffuseOn = 0.0;
    specularOn = 1.0;
  }

private:
  float ambientOn = 1.0;
  float diffuseOn = 1.0;
  float specularOn = 1.0;
};

#endif /* spotLight_h */