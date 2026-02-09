#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragWorldPos;
layout(location = 2) in vec3 fragT;
layout(location = 3) in vec3 fragB;
layout(location = 4) in vec3 fragN;

layout(set = 0, binding = 0) uniform sampler2D albedoMap;
layout(set = 0, binding = 1) uniform sampler2D normalMap;

layout(location = 0) out vec4 outColor;

void main() {
    // Sample albedo texture
    vec3 albedo = texture(albedoMap, fragTexCoord).rgb;

    // Sample normal map and remap from [0,1] to [-1,1]
    vec3 sampledNormal = texture(normalMap, fragTexCoord).rgb * 2.0 - 1.0;

    // Build TBN matrix and transform sampled normal to world space
    mat3 TBN = mat3(normalize(fragT), normalize(fragB), normalize(fragN));
    vec3 worldNormal = normalize(TBN * sampledNormal);

    // Directional lighting
    vec3 lightDir = normalize(vec3(0.5, 0.7, 1.0));
    float ambient = 0.15;
    float diffuse = max(dot(worldNormal, lightDir), 0.0);

    vec3 finalColor = albedo * (ambient + diffuse);
    outColor = vec4(finalColor, 1.0);
}
