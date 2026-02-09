#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inTangent;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec3 fragWorldPos;
layout(location = 2) out vec3 fragT;
layout(location = 3) out vec3 fragB;
layout(location = 4) out vec3 fragN;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    mat4 model;
} pc;

void main() {
    gl_Position = pc.mvp * vec4(inPosition, 1.0);

    fragWorldPos = (pc.model * vec4(inPosition, 1.0)).xyz;
    fragTexCoord = inTexCoord;

    // Build TBN basis vectors in world space
    vec3 N = normalize((pc.model * vec4(inNormal, 0.0)).xyz);
    vec3 T = normalize((pc.model * vec4(inTangent.xyz, 0.0)).xyz);
    // Re-orthogonalize T with respect to N (Gram-Schmidt)
    T = normalize(T - dot(T, N) * N);
    vec3 B = cross(N, T) * inTangent.w;

    fragT = T;
    fragB = B;
    fragN = N;
}
