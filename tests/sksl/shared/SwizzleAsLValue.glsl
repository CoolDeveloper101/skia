
out vec4 sk_FragColor;
uniform float testInput;
uniform vec4 colorGreen;
uniform vec4 colorRed;
vec4 main() {
    vec4 color;
    color = colorGreen * 0.5;
    color.w = 2.0;
    color.y /= 0.25;
    color.yzw *= vec3(0.5);
    color.zywx += vec4(0.25, 0.0, 0.0, 0.75);
    return color == vec4(0.75, 1.0, 0.25, 1.0) ? colorGreen : colorRed;
}
