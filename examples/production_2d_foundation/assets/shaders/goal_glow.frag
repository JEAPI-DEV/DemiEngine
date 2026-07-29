#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform vec4 effect_color;
uniform float strength;

out vec4 finalColor;

void main()
{
    vec4 base = texture(texture0, fragTexCoord)*fragColor;
    finalColor = vec4(mix(base.rgb, effect_color.rgb, strength), base.a);
}
