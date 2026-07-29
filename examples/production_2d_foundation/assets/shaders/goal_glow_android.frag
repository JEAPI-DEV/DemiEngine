#version 100

precision mediump float;

varying vec2 fragTexCoord;
varying vec4 fragColor;

uniform sampler2D texture0;
uniform vec4 effect_color;
uniform float strength;

void main()
{
    vec4 base = texture2D(texture0, fragTexCoord)*fragColor;
    gl_FragColor = vec4(mix(base.rgb, effect_color.rgb, strength), base.a);
}
