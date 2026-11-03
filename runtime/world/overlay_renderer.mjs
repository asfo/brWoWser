// GPU submission and texture transport only. Minimap selection, coordinates,
// facing and native FrameXML rectangles are supplied by WASM.
export function createWorldOverlays(gl, source, onIssue) {
  const textures = new Map(),
    shaders = [];
  let program,
    vao,
    buffer,
    closed = false,
    state,
    arrowTexture;
  const arrowPath = 'Interface/Minimap/MinimapArrow.blp';
  const dispose = () => {
    closed = true;
    for (const t of textures.values()) if (t) gl.deleteTexture(t);
    for (const s of shaders) gl.deleteShader(s);
    if (program) gl.deleteProgram(program);
    if (vao) gl.deleteVertexArray(vao);
    if (buffer) gl.deleteBuffer(buffer);
  };
  try {
    program = gl.createProgram();
    for (const [kind, text] of [
      [
        gl.VERTEX_SHADER,
        '#version 300 es\nlayout(location=0)in vec2 position;layout(location=1)in vec2 texcoord;uniform bool full;uniform vec4 sampleRect;out vec2 uv;void main(){vec2 p=position;if(full){p=vec2(float((gl_VertexID<<1)&2),float(gl_VertexID&2))*2.-1.;uv=mix(sampleRect.xy,sampleRect.zw,(p+1.)*.5);}else uv=texcoord;gl_Position=vec4(p,0.,1.);}',
      ],
      [
        gl.FRAGMENT_SHADER,
        '#version 300 es\nprecision highp float;in vec2 uv;uniform sampler2D image;uniform vec3 circle;uniform bool marker;out vec4 color;void main(){if(distance(gl_FragCoord.xy,circle.xy)>circle.z)discard;color=texture(image,uv);}',
      ],
    ]) {
      const s = gl.createShader(kind);
      shaders.push(s);
      gl.shaderSource(s, text);
      gl.compileShader(s);
      if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) throw Error(gl.getShaderInfoLog(s));
      gl.attachShader(program, s);
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
    const loc = (n) => gl.getUniformLocation(program, n);
    const prepare = (rect, w, h) => {
      const scaleX = gl.canvas.width / w,
        scaleY = gl.canvas.height / h;
      const [x, y, rw, rh] = rect;
      gl.bindFramebuffer(gl.FRAMEBUFFER, null);
      gl.viewport(x * scaleX, y * scaleY, rw * scaleX, rh * scaleY);
      gl.disable(gl.SCISSOR_TEST);
      gl.disable(gl.DEPTH_TEST);
      gl.disable(gl.CULL_FACE);
      gl.depthMask(false);
      gl.enable(gl.BLEND);
      gl.blendEquation(gl.FUNC_ADD);
      gl.blendFuncSeparate(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA, gl.ONE, gl.ONE_MINUS_SRC_ALPHA);
      gl.useProgram(program);
      gl.bindVertexArray(vao);
      gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
      gl.activeTexture(gl.TEXTURE0);
      gl.uniform1i(loc('image'), 0);
      gl.uniform4f(loc('sampleRect'), 0, 0, 1, 1);
      gl.uniform3f(
        loc('circle'),
        (x + rw / 2) * scaleX,
        (y + rh / 2) * scaleY,
        Math.min(rw * scaleX, rh * scaleY) * 0.49,
      );
    };
    return {
      dispose,
      update(next) {
        state = next;
        const wanted = new Set([...next.paths.filter(Boolean), arrowPath]);
        for (const [path, t] of textures)
          if (t && !wanted.has(path)) {
            gl.deleteTexture(t);
            textures.delete(path);
          }
        for (const path of wanted)
          if (!textures.has(path)) {
            textures.set(path, null);
            source
              .decodeTexture(path)
              .then((image) => {
                if (closed || (path !== arrowPath && !state.paths.includes(path))) {
                  textures.delete(path);
                  return;
                }
                const t = gl.createTexture();
                gl.bindTexture(gl.TEXTURE_2D, t);
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
                textures.set(path, t);
                if (path === arrowPath) arrowTexture = t;
              })
              .catch((error) => onIssue('Minimap: ' + error.message));
          }
      },
      draw(rects, w, h, portrait) {
        if (closed) return;
        if (state && rects.Minimap) {
          prepare(rects.Minimap, w, h);
          gl.uniform1i(loc('full'), 0);
          gl.uniform1i(loc('marker'), 0);
          for (let i = 0; i < 9; i++) {
            const t = textures.get(state.paths[i]);
            if (!t) continue;
            gl.bindTexture(gl.TEXTURE_2D, t);
            gl.bufferSubData(gl.ARRAY_BUFFER, 0, state.vertices.subarray(i * 24, (i + 1) * 24));
            gl.drawArrays(gl.TRIANGLES, 0, 6);
          }
          if (arrowTexture) {
            gl.bindTexture(gl.TEXTURE_2D, arrowTexture);
            gl.bufferSubData(gl.ARRAY_BUFFER, 0, state.vertices.subarray(216, 240));
            gl.drawArrays(gl.TRIANGLES, 0, 6);
          }
        }
        if (portrait)
          for (const name of ['PlayerPortrait', 'MicroButtonPortrait']) {
            if (!rects[name]) continue;
            prepare(rects[name], w, h);
            if (name === 'MicroButtonPortrait') {
              gl.uniform3f(loc('circle'), 0, 0, 1e9);
              gl.uniform4f(loc('sampleRect'), 0.2, 0.1, 0.8, 0.94);
            }
            gl.uniform1i(loc('full'), 1);
            gl.uniform1i(loc('marker'), 0);
            gl.bindTexture(gl.TEXTURE_2D, portrait);
            gl.drawArrays(gl.TRIANGLES, 0, 3);
          }
        gl.viewport(0, 0, gl.drawingBufferWidth, gl.drawingBufferHeight);
        gl.depthMask(true);
      },
    };
  } catch (error) {
    dispose();
    throw error;
  }
}
