#version 300 es

precision highp float;
precision highp int;

// OpenGL ES replacement for the mm_contour pass. The desktop path draws the
// model twice, switching to glPolygonMode(GL_LINE) for the second pass; ES 3.0
// has no glPolygonMode, so the geometry arrives already as lines and this
// shader only needs the same depth bias mm_contour applies.

uniform mat4 view_model_matrix;
uniform mat4 projection_matrix;
uniform float offset;

in vec3 v_position;

void main()
{
    // Small epsilon on z to avoid z-fighting between the surface and its contour.
    vec4 clip_position = projection_matrix * view_model_matrix * vec4(v_position, 1.0);
    clip_position.z -= offset * abs(clip_position.w);
    gl_Position = clip_position;
}
