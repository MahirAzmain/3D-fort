
#ifndef basic_camera_h
#define basic_camera_h

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>


class BasicCamera {
public:

    glm::vec3 eye;
    glm::vec3 lookAt, direction;
    glm::vec3 V;
    float Yaw, Pitch;
    float Zoom, MouseSensitivity, MovementSpeed;

    BasicCamera(float eyeX = 0.0, float eyeY = 1.0, float eyeZ = 3.0, float lookAtX = 0.0, float lookAtY = 0.0, float lookAtZ = 0.0, glm::vec3 viewUpVector = glm::vec3(0.0f, 1.0f, 0.0f))
    {
        eye = glm::vec3(eyeX, eyeY, eyeZ);
        lookAt = glm::vec3(lookAtX, lookAtY, lookAtZ);
        V = viewUpVector;

        Yaw = -90.0f;
        Pitch = 0.0f;
        MovementSpeed = 2.5f;
        MouseSensitivity = 0.1f;
        Zoom = 45.0;

        direction = glm::normalize(eye - lookAt);
    }

    glm::mat4 createViewMatrix()
    {
        direction = glm::normalize(eye - lookAt);
        return glm::lookAt(eye, direction, V);
    }

    // processes input received from a mouse scroll-wheel event. Only requires input on the vertical wheel-axis
    void ProcessMouseScroll(float yoffset)
    {
        Zoom -= (float)yoffset;
        if (Zoom < 1.0f)
            Zoom = 1.0f;
        if (Zoom > 45.0f)
            Zoom = 45.0f;
    }

    // processes input received from a mouse input system. Expects the offset value in both the x and y direction.
    void ProcessMouseMovement(float xoffset, float yoffset, GLboolean constrainPitch = true)
    {
        Yaw += xoffset * MouseSensitivity;
        Pitch += yoffset * MouseSensitivity;

        // make sure that when pitch is out of bounds, screen doesn't get flipped
        if (constrainPitch)
        {
            if (Pitch > 89.0f)
                Pitch = 89.0f;
            if (Pitch < -89.0f)
                Pitch = -89.0f;
        }

        // update Front, Right and Up Vectors using the updated Euler angles
        updateCameraVectors();
    }



private:
    glm::vec3 u;
    glm::vec3 v;
    glm::vec3 n;

    // calculates the front vector from the Camera's (updated) Euler Angles
    void updateCameraVectors()
    {
        // calculate the new Front vector
        glm::vec3 front;
        front.x = cos(glm::radians(Yaw)) * cos(glm::radians(Pitch));
        front.y = sin(glm::radians(Pitch));
        front.z = sin(glm::radians(Yaw)) * cos(glm::radians(Pitch));
        direction = glm::normalize(front);
    }
};

#endif /* basic_camera_h */
#pragma once

