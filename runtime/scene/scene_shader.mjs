// Original ps_2_0 record0 arithmetic; full fog/lighting/permutation selection remains outside this profile.
export const sceneFragmentSource = `#version 300 es
precision highp float;
in vec2 texcoord;in vec2 texcoord1;
uniform sampler2D image0;uniform sampler2D image1;uniform int combiner;
uniform float alphaCutoff;uniform float opacity;uniform vec3 tint;
out vec4 color;
void main(){vec4 a=texture(image0,texcoord),b=texture(image1,texcoord1);vec3 rgb=a.rgb;float alpha=opacity;
 if(combiner==1||combiner==3||combiner==4)alpha=a.a*alpha;
 if(combiner>=2){rgb=rgb*b.rgb;alpha=b.a*alpha;}
 if(combiner==2||combiner==3){rgb=rgb+rgb;alpha=alpha+alpha;}
 rgb=rgb*tint;if(alpha<alphaCutoff)discard;color=vec4(rgb,alpha);}`;
