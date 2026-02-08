#version 450

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec3 fragPosition;

layout(location = 0) out vec4 outColor;

void main() {
    // Basic directional lighting
    vec3 lightDir = normalize(vec3(0.5, 0.7, 1.0));
    vec3 normal = normalize(fragNormal);

    // Ambient + Diffuse
    float ambient = 0.15;
    float diffuse = max(dot(normal, lightDir), 0.0);

    // Base color (light gray)
    vec3 baseColor = vec3(0.8, 0.8, 0.85);

    vec3 finalColor = baseColor * (ambient + diffuse);
    outColor = vec4(finalColor, 1.0);
}
