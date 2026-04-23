#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;
out vec3 BaseLighting;
out vec3 TexLighting;
out vec3 SpecularLighting;
out vec2 TexCoords;
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
struct Material {
    sampler2D diffuse;
    sampler2D specular;
    float shininess;
};
uniform vec3 objectColor;
struct PointLight {
    vec3 position;
    
    float k_c;  // attenuation factors
    float k_l;  // attenuation factors
    float k_q;  // attenuation factors
    
    vec3 ambient;
    vec3 diffuse;
    vec3 specular;
};
struct DirectionalLight {
    vec3 direction;
    vec3 ambient;
    vec3 diffuse;
    vec3 specular;
};
struct SpotLight {
    vec3 position;
    vec3 direction;
    float cutoff;
    
    float k_c;  // attenuation factors
    float k_l;  // attenuation factors
    float k_q;  // attenuation factors
    
    vec3 ambient;
    vec3 diffuse;
    vec3 specular;
};
#define NR_POINT_LIGHTS 3
#define NR_SPOT_LIGHTS 2
uniform vec3 viewPos;
uniform PointLight pointLights[NR_POINT_LIGHTS];
uniform Material material;
uniform DirectionalLight directionalLight;
uniform bool directionLightOn = true;
uniform bool spotLightOn = true;
uniform SpotLight spotLights[NR_SPOT_LIGHTS];
uniform float d_amb_on = 1.0f;
uniform float d_def_on = 1.0f;
uniform float d_spec_on = 1.0f;
// function prototypes
void CalcPointLight(Material material, PointLight light, vec3 N, vec3 Pos, vec3 V, out vec3 baseAcc, out vec3 texAcc, out vec3 specAcc);
void CalcDirectionalLight(Material material, DirectionalLight light, vec3 N, vec3 V, out vec3 baseAcc, out vec3 texAcc, out vec3 specAcc);
void CalcSpotLight(Material material, SpotLight light, vec3 N, vec3 Pos, vec3 V, out vec3 baseAcc, out vec3 texAcc, out vec3 specAcc);
void main()
{
    gl_Position = projection * view * model * vec4(aPos, 1.0);
    
    vec3 Pos = vec3(model * vec4(aPos, 1.0));
    vec3 Normal = mat3(transpose(inverse(model))) * aNormal;
    TexCoords = aTexCoords;
    // TexCoords is computed for texture lookup below
    
    // properties
    vec3 N = normalize(Normal);
    vec3 V = normalize(viewPos - Pos);
    BaseLighting = vec3(0.0);
    TexLighting = vec3(0.0);
    SpecularLighting = vec3(0.0);
  
    
    vec3 bAcc, tAcc, sAcc;
    
    // point lights
    for(int i = 0; i < NR_POINT_LIGHTS; i++) {
        CalcPointLight(material, pointLights[i], N, Pos, V, bAcc, tAcc, sAcc);
        BaseLighting += bAcc; TexLighting += tAcc; SpecularLighting += sAcc;
    }
        
    if(directionLightOn) {
        CalcDirectionalLight(material, directionalLight, N, V, bAcc, tAcc, sAcc);
        BaseLighting += bAcc; TexLighting += tAcc; SpecularLighting += sAcc;
    }
        
    if(spotLightOn) {
        for(int i = 0; i < NR_SPOT_LIGHTS; i++) {
            CalcSpotLight(material, spotLights[i], N, Pos, V, bAcc, tAcc, sAcc);
            BaseLighting += bAcc; TexLighting += tAcc; SpecularLighting += sAcc;
        }
    }
    
    
}
void CalcPointLight(Material material, PointLight light, vec3 N, vec3 Pos, vec3 V, out vec3 baseAcc, out vec3 texAcc, out vec3 specAcc)
{
    vec3 L = normalize(light.position - Pos);
    vec3 R = reflect(-L, N);
    
    // attenuation
    float d = length(light.position - Pos);
    float attenuation = 1.0 / (light.k_c + light.k_l * d + light.k_q * (d * d));
    
    float diffFactor = max(dot(N, L), 0.0);
    float specFactor = pow(max(dot(V, R), 0.0), material.shininess);
    
    vec3 ambientBase = objectColor * light.ambient * attenuation;
    vec3 diffuseBase = objectColor * diffFactor * light.diffuse * attenuation;
    baseAcc = ambientBase + diffuseBase;
    
    vec3 ambientTex = vec3(1.0) * light.ambient * attenuation;
    vec3 diffuseTex = vec3(1.0) * diffFactor * light.diffuse * attenuation;
    texAcc = ambientTex + diffuseTex;
    
    // We can assume specular color is white for base, or just 1.0
    specAcc = vec3(1.0) * specFactor * light.specular * attenuation;
}
void CalcDirectionalLight(Material material, DirectionalLight light, vec3 N, vec3 V, out vec3 baseAcc, out vec3 texAcc, out vec3 specAcc)
{
    vec3 L = normalize(-light.direction);
    vec3 R = reflect(-L, N);
    
    float diffFactor = max(dot(N, L), 0.001);
    float specFactor = pow(max(dot(V, R), 0.001), material.shininess);
    
    vec3 ambientBase = objectColor * light.ambient;
    vec3 diffuseBase = objectColor * diffFactor * light.diffuse;
    baseAcc = ambientBase + diffuseBase;
    
    vec3 ambientTex = vec3(1.0) * light.ambient;
    vec3 diffuseTex = vec3(1.0) * diffFactor * light.diffuse;
    texAcc = ambientTex + diffuseTex;
    
    specAcc = vec3(1.0) * specFactor * light.specular;
}
void CalcSpotLight(Material material, SpotLight light, vec3 N, vec3 Pos, vec3 V, out vec3 baseAcc, out vec3 texAcc, out vec3 specAcc)
{
    vec3 L = normalize(light.position - Pos);
    vec3 R = reflect(-L, N);
    
    // attenuation
    float d = length(light.position - Pos);
    float attenuation = 1.0 / (light.k_c + light.k_l * d + light.k_q * (d * d));
    
    float diffFactor = max(dot(N, L), 0.0);
    float specFactor = pow(max(dot(V, R), 0.0), material.shininess);
    
    float cos_alpha = dot(L, normalize(-light.direction));
    float intensity = cos_alpha > light.cutoff ? 1.0 : 0.0;
    
    vec3 ambientBase = objectColor * light.ambient * attenuation * intensity;
    vec3 diffuseBase = objectColor * diffFactor * light.diffuse * attenuation * intensity;
    baseAcc = ambientBase + diffuseBase;
    
    vec3 ambientTex = vec3(1.0) * light.ambient * attenuation * intensity;
    vec3 diffuseTex = vec3(1.0) * diffFactor * light.diffuse * attenuation * intensity;
    texAcc = ambientTex + diffuseTex;
    
    specAcc = vec3(1.0) * specFactor * light.specular * attenuation * intensity;
}
