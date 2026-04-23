#version 330 core
out vec4 FragColor;
in vec3 BaseLighting;
in vec3 TexLighting;
in vec3 SpecularLighting;
in vec2 TexCoords;
struct Material {
    sampler2D diffuse;
    sampler2D specular;
    float shininess;
};
uniform Material material;
uniform int colorMode;
void main()
{
   vec4 texColor = texture(material.diffuse, TexCoords);
   vec4 specColor = texture(material.specular, TexCoords);
   
   vec3 result = vec3(0.0);
   vec3 specResult = vec3(0.0);
   
   if (colorMode == 0) {
       result = BaseLighting;
   } else if (colorMode == 1) {
       result = TexLighting * vec3(texColor);
   } else {
       // Combine object color lighting with texture lighting appropriately
       result = BaseLighting + (TexLighting * vec3(texColor));
   }
   // Always use specular map as a mask
   specResult = SpecularLighting * vec3(specColor);
   
   FragColor = vec4(result + specResult, 1.0);

}