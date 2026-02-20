#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragWorldPos;
layout(location = 2) in vec3 fragT;
layout(location = 3) in vec3 fragB;
layout(location = 4) in vec3 fragN;

// PBR texture bindings
layout(set = 0, binding = 0) uniform sampler2D albedoMap;
layout(set = 0, binding = 1) uniform sampler2D normalMap;
layout(set = 0, binding = 2) uniform sampler2D roughnessMap;
layout(set = 0, binding = 3) uniform sampler2D metallicMap;
layout(set = 0, binding = 4) uniform sampler2D aoMap;

layout(location = 0) out vec4 outColor;

const float PI = 3.14159265359;

// ── PBR BRDF Functions ──────────────────────────────────────────────

float distributionGGX(vec3 N, vec3 H, float roughness) {
    float a  = roughness * roughness;
    float a2 = a * a;
    float NdotH  = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return a2 / max(denom, 0.0001);
}

float geometrySchlickGGX(float NdotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float geometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx1  = geometrySchlickGGX(NdotV, roughness);
    float ggx2  = geometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// ── Main ─────────────────────────────────────────────────────────────

void main() {
    // Sample PBR textures
    vec3  albedo    = pow(texture(albedoMap, fragTexCoord).rgb, vec3(2.2)); // sRGB → linear
    float roughness = texture(roughnessMap, fragTexCoord).g; // often in green channel
    float metallic  = texture(metallicMap, fragTexCoord).b;  // often in blue channel
    float ao        = texture(aoMap, fragTexCoord).r;

    // Sample and transform normal
    vec3 sampledNormal = texture(normalMap, fragTexCoord).rgb * 2.0 - 1.0;
    mat3 TBN = mat3(normalize(fragT), normalize(fragB), normalize(fragN));
    vec3 N = normalize(TBN * sampledNormal);

    // Camera at origin for now — approximate view direction
    vec3 V = normalize(-fragWorldPos);

    // Reflectance at normal incidence (F0)
    // Dielectrics ≈ 0.04, metals use albedo color
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    // ── Lighting ─────────────────────────────────────────────────────
    vec3 Lo = vec3(0.0);

    // Directional light (sun)
    vec3  lightDir   = normalize(vec3(0.5, 0.7, 1.0));
    vec3  lightColor = vec3(3.0); // bright white sun
    float NdotL      = max(dot(N, lightDir), 0.0);

    if (NdotL > 0.0) {
        vec3 H = normalize(V + lightDir);

        // Cook-Torrance specular BRDF
        float D = distributionGGX(N, H, roughness);
        float G = geometrySmith(N, V, lightDir, roughness);
        vec3  F = fresnelSchlick(max(dot(H, V), 0.0), F0);

        vec3 numerator   = D * G * F;
        float denominator = 4.0 * max(dot(N, V), 0.0) * NdotL + 0.0001;
        vec3 specular    = numerator / denominator;

        // Energy conservation: diffuse + specular must not exceed 1
        vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);

        Lo += (kD * albedo / PI + specular) * lightColor * NdotL;
    }

    // Fill / sky light (subtle upwards hemisphere)
    {
        vec3  fillDir   = normalize(vec3(-0.3, 0.5, -0.4));
        vec3  fillColor = vec3(0.4, 0.5, 0.7); // cool blue fill
        float fillNdotL = max(dot(N, fillDir), 0.0);

        if (fillNdotL > 0.0) {
            vec3 H = normalize(V + fillDir);

            float D = distributionGGX(N, H, roughness);
            float G = geometrySmith(N, V, fillDir, roughness);
            vec3  F = fresnelSchlick(max(dot(H, V), 0.0), F0);

            vec3  spec  = (D * G * F) / (4.0 * max(dot(N, V), 0.0) * fillNdotL + 0.0001);
            vec3  kD    = (vec3(1.0) - F) * (1.0 - metallic);

            Lo += (kD * albedo / PI + spec) * fillColor * fillNdotL;
        }
    }

    // Ambient / IBL approximation
    vec3 ambient = vec3(0.03) * albedo * ao;

    vec3 color = ambient + Lo;

    // Tone mapping (Reinhard) + gamma correction
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));

    outColor = vec4(color, 1.0);
}
