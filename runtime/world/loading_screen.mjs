// WebGL submission only. Source selection, progress and quad geometry come
// from the world WASM decoder. No DOM loading overlay is created.
export async function createLoadingScreen(gl, source, map, width, height) {
  const selected = await source.world('loading-screen', { map });
  const base = selected.path;
  let background;
  if (selected.wide && width / height > 4 / 3)
    try {
      background = await source.decodeTexture(base.replace(/\.blp$/i, 'Wide.blp'));
    } catch {
      // Some original map records do not ship a separate wide variant.
    }
  background ??= await source.decodeTexture(base);
  const images = [background];
  for (const name of ['Background', 'Fill', 'Border'])
    images.push(
      await source.decodeTexture('Interface\\Glues\\LoadingBar\\Loading-Bar' + name + '.blp'),
    );
  const textures = [],
    shaders = [];
  let program,
    vao,
    buffer,
    quads,
    closed = false,
    progress = 0;
  const dispose = () => {
    if (closed) return;
    closed = true;
    textures.forEach((texture) => gl.deleteTexture(texture));
    shaders.forEach((shader) => gl.deleteShader(shader));
    if (program) gl.deleteProgram(program);
    if (vao) gl.deleteVertexArray(vao);
    if (buffer) gl.deleteBuffer(buffer);
  };
  try {
    program = gl.createProgram();
    for (const [kind, text] of [
      [
        gl.VERTEX_SHADER,
        '#version 300 es\nlayout(location=0) in vec2 position;layout(location=1) in vec2 texcoord;out vec2 uv;void main(){uv=texcoord;gl_Position=vec4(position,0.,1.);}',
      ],
      [
        gl.FRAGMENT_SHADER,
        '#version 300 es\nprecision highp float;in vec2 uv;uniform sampler2D image;out vec4 color;void main(){color=texture(image,uv);}',
      ],
    ]) {
      const shader = gl.createShader(kind);
      shaders.push(shader);
      gl.shaderSource(shader, text);
      gl.compileShader(shader);
      if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS))
        throw Error(gl.getShaderInfoLog(shader));
      gl.attachShader(program, shader);
    }
    gl.linkProgram(program);
    if (!gl.getProgramParameter(program, gl.LINK_STATUS))
      throw Error(gl.getProgramInfoLog(program));
    vao = gl.createVertexArray();
    buffer = gl.createBuffer();
    gl.bindVertexArray(vao);
    gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
    gl.bufferData(gl.ARRAY_BUFFER, 96, gl.DYNAMIC_DRAW);
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 2, gl.FLOAT, false, 16, 0);
    gl.enableVertexAttribArray(1);
    gl.vertexAttribPointer(1, 2, gl.FLOAT, false, 16, 8);
    gl.useProgram(program);
    gl.uniform1i(gl.getUniformLocation(program, 'image'), 0);
    for (const image of images) {
      const texture = gl.createTexture();
      textures.push(texture);
      gl.bindTexture(gl.TEXTURE_2D, texture);
      gl.pixelStorei(gl.UNPACK_ALIGNMENT, 1);
      gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, false);
      gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, false);
      gl.texImage2D(
        gl.TEXTURE_2D,
        0,
        gl.RGBA8,
        image.width,
        image.height,
        0,
        gl.RGBA,
        gl.UNSIGNED_BYTE,
        image.rgba,
      );
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    }
    return {
      path: base,
      dispose,
      async update(value, w = width, h = height) {
        if (closed) return;
        progress = Math.max(progress, value);
        width = w;
        height = h;
        const result = await source.world('loading-layout', { width, height, progress });
        if (!closed) quads = result.quads;
      },
      resize(w, h) {
        return this.update(progress, w, h);
      },
      draw() {
        if (closed || !quads) return;
        gl.bindFramebuffer(gl.FRAMEBUFFER, null);
        gl.viewport(0, 0, gl.canvas.width, gl.canvas.height);
        gl.disable(gl.SCISSOR_TEST);
        gl.disable(gl.DEPTH_TEST);
        gl.disable(gl.CULL_FACE);
        gl.depthMask(false);
        gl.useProgram(program);
        gl.bindVertexArray(vao);
        gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
        gl.activeTexture(gl.TEXTURE0);
        gl.enable(gl.BLEND);
        gl.blendEquation(gl.FUNC_ADD);
        gl.blendFuncSeparate(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA, gl.ONE, gl.ONE_MINUS_SRC_ALPHA);
        for (let i = 0; i < 4; i++) {
          gl.bindTexture(gl.TEXTURE_2D, textures[i]);
          gl.bufferSubData(gl.ARRAY_BUFFER, 0, quads.subarray(i * 24, (i + 1) * 24));
          gl.drawArrays(gl.TRIANGLES, 0, 6);
        }
        gl.depthMask(true);
      },
    };
  } catch (error) {
    dispose();
    throw error;
  }
}
