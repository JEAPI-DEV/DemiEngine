#version 100
attribute vec3 vertexPosition;
attribute vec2 vertexTexCoord;
attribute vec3 vertexNormal;
uniform mat4 mvp;
uniform mat4 matModel;
varying vec2 fragTexCoord;
varying vec3 fragPosition;
varying vec3 fragNormal;
void main() {
  fragTexCoord = vertexTexCoord;
  fragPosition = vec3(matModel*vec4(vertexPosition, 1.0));
  fragNormal = normalize(mat3(matModel)*vertexNormal);
  gl_Position = mvp*vec4(vertexPosition, 1.0);
}
