#type vertex
#version 450

layout(location = 0) in vec3 a_Position;

layout(set = 0, binding = 0) uniform CameraBlock {
    mat4 ViewProj;
    vec4 Position;
} u_Camera;

void main()
{
    gl_Position = u_Camera.ViewProj * vec4(a_Position, 1.0);
}

#type fragment
#version 450
void main() {}