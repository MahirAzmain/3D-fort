#ifndef CANNON_H
#define CANNON_H

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "shader.h"

// Struct to encapsulate cannon state
struct Cannon {
    // Positioning
    glm::vec3 position;
    float yaw;   // Rotation left/right
    float pitch; // Elevation up/down

    // Projectile state
    bool isFiring;
    glm::vec3 projectilePos;
    glm::vec3 projectileVel;
    float gravity;

    // Visual configuration
    unsigned int barrelVAO;
    unsigned int barrelIndicesCount;
    unsigned int baseVAO;
    unsigned int wheelVAO;
    unsigned int wheelIndicesCount;

    Cannon() : position(0.0f), yaw(0.0f), pitch(45.0f),
               isFiring(false), projectilePos(0.0f), projectileVel(0.0f),
               gravity(9.8f), barrelVAO(0), barrelIndicesCount(0),
               baseVAO(0), wheelVAO(0), wheelIndicesCount(0) {}

    void update(float deltaTime) {
        if (isFiring) {
            projectilePos += projectileVel * deltaTime;
            projectileVel.y -= gravity * deltaTime;

            // Simple collision with ground
            if (projectilePos.y <= -2.0f) {
                isFiring = false;
            }
        }
    }

    void fire() {
        if (!isFiring) {
            isFiring = true;
            
            // Calculate launch velocity vector based on pitch and yaw
            // The barrel rests roughly in the +Z direction originally
            float speed = 25.0f;
            float yawRad = glm::radians(yaw);
            float pitchRad = glm::radians(pitch);

            glm::vec3 projectileDir;
            projectileDir.x = sin(yawRad) * cos(pitchRad);
            projectileDir.y = sin(pitchRad);
            projectileDir.z = cos(yawRad) * cos(pitchRad);
            
            // Tip of the barrel
            projectilePos = position + glm::vec3(0.0f, 1.0f, 0.0f) + projectileDir * 2.5f;
            projectileVel = projectileDir * speed;
        }
    }

    // Helper for rendering
    void draw(Shader& shader, unsigned int sphereVAO, unsigned int sphereIndicesCount) {
        glm::mat4 identity = glm::mat4(1.0f);
        
        // --- Shared Iron Material for all parts ---
        shader.use();
        shader.setVec3("material.ambient", glm::vec3(0.2f, 0.2f, 0.2f));
        shader.setVec3("material.diffuse", glm::vec3(0.35f, 0.35f, 0.35f));
        shader.setVec3("material.specular", glm::vec3(0.3f, 0.3f, 0.3f));
        shader.setFloat("material.shininess", 16.0f);

        // Adjust base height
        glm::mat4 baseModel = glm::translate(identity, position + glm::vec3(0.0f, 0.2f, 0.0f));
        baseModel = glm::rotate(baseModel, glm::radians(yaw), glm::vec3(0.0f, 1.0f, 0.0f));
        
        // 1. Draw Carriage Base (Stepped side cheeks + connecting slabs)
        // Left Cheek (Stepped: 3 blocks)
        glm::mat4 cheekL1 = glm::translate(baseModel, glm::vec3(0.4f, 0.45f, 0.4f));
        cheekL1 = glm::scale(cheekL1, glm::vec3(0.15f, 0.5f, 0.6f));
        shader.setMat4("model", cheekL1);
        glBindVertexArray(baseVAO);
        glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);

        glm::mat4 cheekL2 = glm::translate(baseModel, glm::vec3(0.4f, 0.35f, -0.15f));
        cheekL2 = glm::scale(cheekL2, glm::vec3(0.15f, 0.3f, 0.5f));
        shader.setMat4("model", cheekL2);
        glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);

        glm::mat4 cheekL3 = glm::translate(baseModel, glm::vec3(0.4f, 0.3f, -0.6f));
        cheekL3 = glm::scale(cheekL3, glm::vec3(0.15f, 0.2f, 0.4f));
        shader.setMat4("model", cheekL3);
        glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);

        // Right Cheek (Stepped: 3 blocks)
        glm::mat4 cheekR1 = glm::translate(baseModel, glm::vec3(-0.4f, 0.45f, 0.4f));
        cheekR1 = glm::scale(cheekR1, glm::vec3(0.15f, 0.5f, 0.6f));
        shader.setMat4("model", cheekR1);
        glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);

        glm::mat4 cheekR2 = glm::translate(baseModel, glm::vec3(-0.4f, 0.35f, -0.15f));
        cheekR2 = glm::scale(cheekR2, glm::vec3(0.15f, 0.3f, 0.5f));
        shader.setMat4("model", cheekR2);
        glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);

        glm::mat4 cheekR3 = glm::translate(baseModel, glm::vec3(-0.4f, 0.3f, -0.6f));
        cheekR3 = glm::scale(cheekR3, glm::vec3(0.15f, 0.2f, 0.4f));
        shader.setMat4("model", cheekR3);
        glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);

        // Connecting Base Slabs
        glm::mat4 slabFront = glm::translate(baseModel, glm::vec3(0.0f, 0.25f, 0.4f));
        slabFront = glm::scale(slabFront, glm::vec3(0.65f, 0.1f, 0.6f));
        shader.setMat4("model", slabFront);
        glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);

        glm::mat4 slabBack = glm::translate(baseModel, glm::vec3(0.0f, 0.25f, -0.3f));
        slabBack = glm::scale(slabBack, glm::vec3(0.65f, 0.1f, 0.8f));
        shader.setMat4("model", slabBack);
        glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);

        // Add trunnion support half-circles/blocks
        glm::mat4 trunL = glm::translate(baseModel, glm::vec3(0.4f, 0.72f, 0.3f));
        trunL = glm::scale(trunL, glm::vec3(0.16f, 0.1f, 0.2f));
        shader.setMat4("model", trunL);
        glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);

        glm::mat4 trunR = glm::translate(baseModel, glm::vec3(-0.4f, 0.72f, 0.3f));
        trunR = glm::scale(trunR, glm::vec3(0.16f, 0.1f, 0.2f));
        shader.setMat4("model", trunR);
        glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);

        // 2. Draw Wheels (4 small disc wheels, as seen in the reference)
        glBindVertexArray(sphereVAO);

        // Front Left wheel
        glm::mat4 wFL = glm::translate(baseModel, glm::vec3(0.55f, 0.15f, 0.5f));
        wFL = glm::rotate(wFL, glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
        wFL = glm::scale(wFL, glm::vec3(0.25f, 0.05f, 0.25f));
        shader.setMat4("model", wFL);
        glDrawElements(GL_TRIANGLES, sphereIndicesCount, GL_UNSIGNED_INT, 0);

        // Front Right wheel
        glm::mat4 wFR = glm::translate(baseModel, glm::vec3(-0.55f, 0.15f, 0.5f));
        wFR = glm::rotate(wFR, glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
        wFR = glm::scale(wFR, glm::vec3(0.25f, 0.05f, 0.25f));
        shader.setMat4("model", wFR);
        glDrawElements(GL_TRIANGLES, sphereIndicesCount, GL_UNSIGNED_INT, 0);

        // Back Left wheel
        glm::mat4 wBL = glm::translate(baseModel, glm::vec3(0.55f, 0.15f, -0.6f));
        wBL = glm::rotate(wBL, glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
        wBL = glm::scale(wBL, glm::vec3(0.25f, 0.05f, 0.25f));
        shader.setMat4("model", wBL);
        glDrawElements(GL_TRIANGLES, sphereIndicesCount, GL_UNSIGNED_INT, 0);

        // Back Right wheel
        glm::mat4 wBR = glm::translate(baseModel, glm::vec3(-0.55f, 0.15f, -0.6f));
        wBR = glm::rotate(wBR, glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
        wBR = glm::scale(wBR, glm::vec3(0.25f, 0.05f, 0.25f));
        shader.setMat4("model", wBR);
        glDrawElements(GL_TRIANGLES, sphereIndicesCount, GL_UNSIGNED_INT, 0);

        // 3. (Barrel Rendering Removed per User Request)

        // 4. Draw Projectile if firing
        if (isFiring) {
            shader.setVec3("material.ambient", glm::vec3(0.1f, 0.1f, 0.1f));
            shader.setVec3("material.diffuse", glm::vec3(0.2f, 0.2f, 0.2f));
            shader.setVec3("material.specular", glm::vec3(0.8f, 0.8f, 0.8f));
            shader.setFloat("material.shininess", 64.0f);

            glm::mat4 projModel = glm::translate(identity, projectilePos);
            projModel = glm::scale(projModel, glm::vec3(0.22f, 0.22f, 0.22f));
            shader.setMat4("model", projModel);
            glBindVertexArray(sphereVAO);
            glDrawElements(GL_TRIANGLES, sphereIndicesCount, GL_UNSIGNED_INT, 0);
        }

        glBindVertexArray(0);
    }
};

#endif
