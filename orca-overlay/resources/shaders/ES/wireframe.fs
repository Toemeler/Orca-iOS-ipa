#version 300 es

precision highp float;
precision highp int;

uniform vec4 uniform_color;

out vec4 out_color;

void main()
{
    out_color = uniform_color;
}
