#version 300 es

precision highp float;
precision highp int;

// OpenGL ES replacement for the desktop dashed_thick_lines pass, which expands
// each line into a screen-space quad in a geometry shader. ES 3.0 has no
// geometry stage, so lines are drawn as plain GL_LINES and only the dash
// pattern is reproduced. Line thickness and edge antialiasing are therefore not
// applied here; the "width" and "viewport_size" uniforms the callers set go
// unused, which set_uniform tolerates (it no-ops on an inactive location).
//
// v_position.w carries the coordinate along the line, exactly as the desktop
// path supplies it, so the dash phase is computed the same way.

uniform mat4 view_model_matrix;
uniform mat4 projection_matrix;

in vec4 v_position;

out float coord_s;

void main()
{
    coord_s = v_position.w;
    gl_Position = projection_matrix * view_model_matrix * vec4(v_position.xyz, 1.0);
}
