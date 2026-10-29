// GPU postprocessing boundary. The original Lua model glow amount controls
// this pass; UI and cursor are composited afterwards and stay sharp.
// The portable Gaussian kernel is not a claim of native postprocess parity.
export function createSceneGlow(gl) {
  const textures = [],
    framebuffers = [],
    programs = [];
  let depth,
    width = 0,
    height = 0,
    active = false;
  const vao = gl.createVertexArray();
  function program(fragment) {
    const result = gl.createProgram(),
      shaders = [];
    if (!result) throw Error('Scene glow program allocation failed');
    try {
      for (const [type, source] of [
        [
          gl.VERTEX_SHADER,
          `#version 300 es
precision highp float;out vec2 uv;
void main(){vec2 p=vec2(float((gl_VertexID<<1)&2),float(gl_VertexID&2));uv=p;gl_Position=vec4(p*2.-1.,0.,1.);}`,
        ],
        [gl.FRAGMENT_SHADER, fragment],
      ]) {
        const shader = gl.createShader(type);
        if (!shader) throw Error('Scene glow shader allocation failed');
        shaders.push(shader);
        gl.shaderSource(shader, source);
        gl.compileShader(shader);
        if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS))
          throw Error(gl.getShaderInfoLog(shader));
        gl.attachShader(result, shader);
      }
      gl.linkProgram(result);
      if (!gl.getProgramParameter(result, gl.LINK_STATUS))
        throw Error(gl.getProgramInfoLog(result));
      programs.push(result);
      return result;
    } catch (error) {
      gl.deleteProgram(result);
      throw error;
    } finally {
      for (const shader of shaders) gl.deleteShader(shader);
    }
  }
  const blur = program(`#version 300 es
precision highp float;in vec2 uv;out vec4 pixel;
uniform sampler2D image;uniform vec2 stepSize;uniform bool extract;
vec3 sampleColor(vec2 p){vec3 c=texture(image,p).rgb;return extract?max(c-vec3(.65),vec3(0.))/.35:c;}
void main(){vec3 c=sampleColor(uv)*.227027027;
c+=(sampleColor(uv+stepSize)+sampleColor(uv-stepSize))*.194594595;
c+=(sampleColor(uv+stepSize*2.)+sampleColor(uv-stepSize*2.))*.121621622;
c+=(sampleColor(uv+stepSize*3.)+sampleColor(uv-stepSize*3.))*.054054054;
c+=(sampleColor(uv+stepSize*4.)+sampleColor(uv-stepSize*4.))*.016216216;
pixel=vec4(c,1.);}`);
  const composite = program(`#version 300 es
precision highp float;in vec2 uv;out vec4 pixel;
uniform sampler2D image;uniform sampler2D glow;uniform float amount;
void main(){vec4 base=texture(image,uv);pixel=vec4(base.rgb+texture(glow,uv).rgb*amount,base.a);}`);
  const blurImage = gl.getUniformLocation(blur, 'image'),
    blurStep = gl.getUniformLocation(blur, 'stepSize'),
    blurExtract = gl.getUniformLocation(blur, 'extract'),
    baseImage = gl.getUniformLocation(composite, 'image'),
    glowImage = gl.getUniformLocation(composite, 'glow'),
    glowAmount = gl.getUniformLocation(composite, 'amount');
  function releaseSurfaces() {
    for (const texture of textures) gl.deleteTexture(texture);
    for (const framebuffer of framebuffers) gl.deleteFramebuffer(framebuffer);
    textures.length = framebuffers.length = 0;
    if (depth) gl.deleteRenderbuffer(depth);
    depth = null;
  }
  function resize(w, h) {
    if (w === width && h === height) return;
    releaseSurfaces();
    width = w;
    height = h;
    for (let i = 0; i < 3; i++) {
      const texture = gl.createTexture(),
        framebuffer = gl.createFramebuffer();
      if (!texture || !framebuffer) throw Error('Scene glow surface allocation failed');
      textures.push(texture);
      framebuffers.push(framebuffer);
      gl.bindTexture(gl.TEXTURE_2D, texture);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
      gl.texImage2D(
        gl.TEXTURE_2D,
        0,
        gl.RGBA8,
        i ? Math.max(1, w >> 2) : w,
        i ? Math.max(1, h >> 2) : h,
        0,
        gl.RGBA,
        gl.UNSIGNED_BYTE,
        null,
      );
      gl.bindFramebuffer(gl.FRAMEBUFFER, framebuffer);
      gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, texture, 0);
      if (!i) {
        depth = gl.createRenderbuffer();
        gl.bindRenderbuffer(gl.RENDERBUFFER, depth);
        gl.renderbufferStorage(gl.RENDERBUFFER, gl.DEPTH_COMPONENT24, w, h);
        gl.framebufferRenderbuffer(gl.FRAMEBUFFER, gl.DEPTH_ATTACHMENT, gl.RENDERBUFFER, depth);
      }
      if (gl.checkFramebufferStatus(gl.FRAMEBUFFER) !== gl.FRAMEBUFFER_COMPLETE)
        throw Error('Scene glow framebuffer incomplete');
    }
  }
  return {
    begin(w, h, amount) {
      active = Number.isFinite(amount) && amount > 0;
      if (!active) return null;
      resize(w, h);
      return framebuffers[0];
    },
    end(amount) {
      if (!active) return;
      gl.bindVertexArray(vao);
      gl.disable(gl.SCISSOR_TEST);
      gl.disable(gl.DEPTH_TEST);
      gl.disable(gl.CULL_FACE);
      gl.disable(gl.BLEND);
      gl.depthMask(false);
      gl.useProgram(blur);
      gl.uniform1i(blurImage, 0);
      gl.activeTexture(gl.TEXTURE0);
      const w = Math.max(1, width >> 2),
        h = Math.max(1, height >> 2);
      gl.viewport(0, 0, w, h);
      for (let pass = 0; pass < 2; pass++) {
        gl.bindFramebuffer(gl.FRAMEBUFFER, framebuffers[pass + 1]);
        gl.bindTexture(gl.TEXTURE_2D, textures[pass]);
        gl.uniform2f(blurStep, pass ? 0 : 1 / w, pass ? 1 / h : 0);
        gl.uniform1i(blurExtract, pass === 0);
        gl.drawArrays(gl.TRIANGLES, 0, 3);
      }
      gl.bindFramebuffer(gl.FRAMEBUFFER, null);
      gl.viewport(0, 0, width, height);
      gl.useProgram(composite);
      gl.uniform1i(baseImage, 0);
      gl.uniform1i(glowImage, 1);
      gl.uniform1f(glowAmount, Math.max(0, Math.min(1, amount)));
      gl.activeTexture(gl.TEXTURE0);
      gl.bindTexture(gl.TEXTURE_2D, textures[0]);
      gl.activeTexture(gl.TEXTURE1);
      gl.bindTexture(gl.TEXTURE_2D, textures[2]);
      gl.drawArrays(gl.TRIANGLES, 0, 3);
      gl.activeTexture(gl.TEXTURE0);
      gl.depthMask(true);
    },
    dispose() {
      releaseSurfaces();
      for (const p of programs) gl.deleteProgram(p);
      gl.deleteVertexArray(vao);
    },
  };
}
