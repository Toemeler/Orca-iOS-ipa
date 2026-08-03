#version 300 es

precision highp float;
precision highp int;

// Dash rule kept identical to the desktop dashed_thick_lines fragment shader so
// the pattern matches; the antialiasing term there depends on the geometry
// shader's line_width varying and has no equivalent without that stage.

uniform float dash_size;
uniform float gap_size;
uniform vec4 uniform_color;

in float coord_s;

out vec4 out_color;

void main()
{
    float inv_stride = 1.0 / (dash_size + gap_size);
    if (gap_size > 0.0 && fract(coord_s * inv_stride) > dash_size * inv_stride)
        discard;

    out_color = uniform_color;
}
