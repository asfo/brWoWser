// Original static mappings: 0x81ff08, 0x81ff74, 0x6a8cf8, 0x6a8dc5.
// Browser front-face orientation remains an explicit compatibility choice.
export function materialState(flags, mode, opacity = 1) {
  if (
    !Number.isInteger(flags) ||
    flags < 0 ||
    flags > 65535 ||
    ![0, 1, 2, 4].includes(mode) ||
    !Number.isFinite(opacity) ||
    opacity < 0 ||
    opacity > 1
  )
    throw Error('unsupported material state');
  return {
    mode,
    cull: !(flags & 4),
    depthWrite: !(flags & 16),
    depthCompare: flags & 8 ? 'always' : 'lequal',
    blend: mode === 2 ? 'alpha' : mode === 4 ? 'additive' : 'off',
    alphaCutoff: mode === 0 ? 0 : mode === 1 ? opacity * (224 / 255) : 1 / 255,
    opacity,
  };
}
export function applyMaterialState(gl, state, frontFace = 'ccw') {
  if (!['ccw', 'cw'].includes(frontFace)) throw Error('invalid front-face orientation');
  if (state.cull) {
    gl.enable(gl.CULL_FACE);
    gl.cullFace(gl.BACK);
    gl.frontFace(frontFace === 'ccw' ? gl.CCW : gl.CW);
  } else gl.disable(gl.CULL_FACE);
  gl.enable(gl.DEPTH_TEST);
  gl.depthFunc(state.depthCompare === 'always' ? gl.ALWAYS : gl.LEQUAL);
  gl.depthMask(state.depthWrite);
  if (state.blend === 'off') gl.disable(gl.BLEND);
  else {
    gl.enable(gl.BLEND);
    gl.blendEquation(gl.FUNC_ADD);
    gl.blendFunc(gl.SRC_ALPHA, state.blend === 'alpha' ? gl.ONE_MINUS_SRC_ALPHA : gl.ONE);
  }
}
export const materialFragmentSource = `#version 300 es
precision highp float;
in vec2 texcoord;
uniform sampler2D image;
uniform float alphaCutoff;
uniform float opacity;
out vec4 color;
void main(){vec4 c=texture(image,texcoord);c.a*=opacity;if(c.a<alphaCutoff)discard;color=c;}`;
