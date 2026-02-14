#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inTangent;
layout(location = 4) in vec4 inMvpRow0;
layout(location = 5) in vec4 inMvpRow1;
layout(location = 6) in vec4 inMvpRow2;
layout(location = 7) in vec4 inMvpRow3;
layout(location = 8) in vec4 inModelRow0;
layout(location = 9) in vec4 inModelRow1;
layout(location = 10) in vec4 inModelRow2;
layout(location = 11) in vec4 inModelRow3;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec3 fragWorldPos;
layout(location = 2) out vec3 fragT;
layout(location = 3) out vec3 fragB;
layout(location = 4) out vec3 fragN;

void main() {
    mat4 mvp = mat4(inMvpRow0, inMvpRow1, inMvpRow2, inMvpRow3);
    mat4 model = mat4(inModelRow0, inModelRow1, inModelRow2, inModelRow3);

    gl_Position = mvp * vec4(inPosition, 1.0);

    fragWorldPos = (model * vec4(inPosition, 1.0)).xyz;
    fragTexCoord = inTexCoord;

    // Build TBN basis vectors in world space
    vec3 N = normalize((model * vec4(inNormal, 0.0)).xyz);
    vec3 T = normalize((model * vec4(inTangent.xyz, 0.0)).xyz);
    // Re-orthogonalize T with respect to N (Gram-Schmidt)
    T = normalize(T - dot(T, N) * N);
    vec3 B = cross(N, T) * inTangent.w;

    fragT = T;
    fragB = B;
    fragN = N;
}
