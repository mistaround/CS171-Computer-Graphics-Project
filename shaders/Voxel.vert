#version 430

layout(location = 0) in vec3 vertexPosition_model;
layout(location = 1) in vec2 vertexUV;
layout(location = 2) in vec3 vertexNormal_model;

uniform mat4 DepthModelViewProjectionMatrix;
uniform mat4 ModelMatrix;

out vData {
    vec2 UV;
    vec4 position_depth;
	vec3 normal; 
} vert;

void main() {
    vert.UV = vertexUV;
    vert.position_depth = DepthModelViewProjectionMatrix * vec4(vertexPosition_model, 1);
	vert.position_depth.xyz = vert.position_depth.xyz * 0.5f + 0.5f;

    vert.normal = normalize(vec3(ModelMatrix * vec4(vertexNormal_model,1)));
	gl_Position = ModelMatrix * vec4(vertexPosition_model,1);
}