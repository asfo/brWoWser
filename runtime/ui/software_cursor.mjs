// Original MPQ pixels, composited last into the game framebuffer. No DOM image
// or CSS cursor bitmap. Coordinates are CSS pixels, independent of DPR.
export function createSoftwareCursor(
  gl,
  canvas,
  image,
  { hidden = () => false, redraw = () => {} } = {},
) {
  const program = gl.createProgram(),
    vao = gl.createVertexArray(),
    buffer = gl.createBuffer(),
    texture = gl.createTexture();
  for (const [type, source] of [
    [
      gl.VERTEX_SHADER,
      '#version 300 es\nlayout(location=0)in vec2 p;uniform vec4 rect;uniform vec2 size;out vec2 uv;void main(){uv=p;vec2 pos=rect.xy+p*rect.zw;gl_Position=vec4(pos.x/size.x*2.-1.,1.-pos.y/size.y*2.,0,1);}',
    ],
    [
      gl.FRAGMENT_SHADER,
      '#version 300 es\nprecision mediump float;in vec2 uv;uniform sampler2D image;out vec4 color;void main(){color=texture(image,uv);}',
    ],
  ]) {
    const shader = gl.createShader(type);
    gl.shaderSource(shader, source);
    gl.compileShader(shader);
    if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) throw Error(gl.getShaderInfoLog(shader));
    gl.attachShader(program, shader);
    gl.deleteShader(shader);
  }
  gl.linkProgram(program);
  if (!gl.getProgramParameter(program, gl.LINK_STATUS)) throw Error(gl.getProgramInfoLog(program));
  gl.bindVertexArray(vao);
  gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
  gl.bufferData(
    gl.ARRAY_BUFFER,
    new Float32Array([0, 0, 0, 1, 1, 0, 1, 0, 0, 1, 1, 1]),
    gl.STATIC_DRAW,
  );
  gl.enableVertexAttribArray(0);
  gl.vertexAttribPointer(0, 2, gl.FLOAT, false, 0, 0);
  gl.bindTexture(gl.TEXTURE_2D, texture);
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
  for (const p of [gl.TEXTURE_MIN_FILTER, gl.TEXTURE_MAG_FILTER])
    gl.texParameteri(gl.TEXTURE_2D, p, gl.NEAREST);
  for (const p of [gl.TEXTURE_WRAP_S, gl.TEXTURE_WRAP_T])
    gl.texParameteri(gl.TEXTURE_2D, p, gl.CLAMP_TO_EDGE);
  const rect = gl.getUniformLocation(program, 'rect'),
    size = gl.getUniformLocation(program, 'size'),
    sampler = gl.getUniformLocation(program, 'image');
  let inside = false,
    x = 0,
    y = 0,
    queued = false;
  const schedule = () => {
    if (queued) return;
    queued = true;
    requestAnimationFrame(() => {
      queued = false;
      redraw();
    });
  };
  const move = (e) => {
    const b = canvas.getBoundingClientRect();
    x = e.clientX - b.left;
    y = e.clientY - b.top;
    inside = x >= 0 && y >= 0 && x < b.width && y < b.height;
    schedule();
  };
  const leave = () => {
    inside = false;
    schedule();
  };
  canvas.dataset.softwareCursor = 'true';
  canvas.style.cursor = 'none';
  canvas.addEventListener('pointermove', move);
  canvas.addEventListener('pointerenter', move);
  canvas.addEventListener('pointerleave', leave);
  window.addEventListener('blur', leave);
  return {
    draw() {
      if (!inside || hidden()) return;
      const b = canvas.getBoundingClientRect();
      gl.useProgram(program);
      gl.bindVertexArray(vao);
      gl.activeTexture(gl.TEXTURE0);
      gl.bindTexture(gl.TEXTURE_2D, texture);
      gl.uniform1i(sampler, 0);
      gl.uniform2f(size, b.width, b.height);
      gl.uniform4f(rect, x, y, image.width, image.height);
      gl.disable(gl.DEPTH_TEST);
      gl.disable(gl.CULL_FACE);
      gl.disable(gl.SCISSOR_TEST);
      gl.depthMask(false);
      gl.enable(gl.BLEND);
      gl.blendEquation(gl.FUNC_ADD);
      gl.blendFuncSeparate(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA, gl.ONE, gl.ONE_MINUS_SRC_ALPHA);
      gl.drawArrays(gl.TRIANGLES, 0, 6);
      gl.depthMask(true);
    },
    dispose() {
      canvas.removeEventListener('pointermove', move);
      canvas.removeEventListener('pointerenter', move);
      canvas.removeEventListener('pointerleave', leave);
      window.removeEventListener('blur', leave);
      delete canvas.dataset.softwareCursor;
      canvas.style.cursor = 'auto';
      gl.deleteTexture(texture);
      gl.deleteBuffer(buffer);
      gl.deleteVertexArray(vao);
      gl.deleteProgram(program);
    },
  };
}
