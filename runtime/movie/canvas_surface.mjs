// Decoder stays at the browser boundary; decoded frames share the game canvas.
export function createMovieSurface(gl, canvas, video) {
  const program = gl.createProgram(),
    vao = gl.createVertexArray(),
    texture = gl.createTexture(),
    shaders = [];
  let running = false,
    frame = 0,
    disposed = false,
    drawn = 0;
  try {
    for (const [type, source] of [
      [
        gl.VERTEX_SHADER,
        '#version 300 es\nprecision highp float;out vec2 uv;void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);uv=vec2(p.x,1.-p.y);gl_Position=vec4(p*2.-1.,0,1);}',
      ],
      [
        gl.FRAGMENT_SHADER,
        '#version 300 es\nprecision highp float;in vec2 uv;uniform sampler2D movie;out vec4 color;void main(){color=texture(movie,uv);}',
      ],
    ]) {
      const shader = gl.createShader(type);
      shaders.push(shader);
      gl.shaderSource(shader, source);
      gl.compileShader(shader);
      if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS))
        throw Error('Movie canvas shader failed');
      gl.attachShader(program, shader);
    }
    gl.linkProgram(program);
    if (!gl.getProgramParameter(program, gl.LINK_STATUS)) throw Error('Movie canvas link failed');
    gl.bindTexture(gl.TEXTURE_2D, texture);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
  } catch (error) {
    gl.deleteProgram(program);
    gl.deleteVertexArray(vao);
    gl.deleteTexture(texture);
    throw error;
  } finally {
    for (const shader of shaders) gl.deleteShader(shader);
  }
  function draw() {
    if (disposed || video.readyState < 2 || !video.videoWidth) return;
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    gl.disable(gl.SCISSOR_TEST);
    gl.disable(gl.DEPTH_TEST);
    gl.disable(gl.CULL_FACE);
    gl.disable(gl.BLEND);
    gl.depthMask(true);
    gl.viewport(0, 0, canvas.width, canvas.height);
    gl.clearColor(0, 0, 0, 1);
    gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
    const scale = Math.min(canvas.width / video.videoWidth, canvas.height / video.videoHeight),
      w = Math.round(video.videoWidth * scale),
      h = Math.round(video.videoHeight * scale);
    gl.viewport(Math.floor((canvas.width - w) / 2), Math.floor((canvas.height - h) / 2), w, h);
    gl.useProgram(program);
    gl.bindVertexArray(vao);
    gl.activeTexture(gl.TEXTURE0);
    gl.bindTexture(gl.TEXTURE_2D, texture);
    gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, false);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, video);
    gl.uniform1i(gl.getUniformLocation(program, 'movie'), 0);
    gl.drawArrays(gl.TRIANGLES, 0, 3);
    gl.viewport(0, 0, canvas.width, canvas.height);
    drawn++;
  }
  const tick = () => {
    if (!running) return;
    try {
      draw();
    } catch (error) {
      running = false;
      window.dispatchEvent(new CustomEvent('brwowser-render-error', { detail: error.message }));
      return;
    }
    frame = requestAnimationFrame(tick);
  };
  return {
    start() {
      if (disposed) throw Error('Movie surface disposed');
      running = true;
      cancelAnimationFrame(frame);
      tick();
    },
    stop() {
      running = false;
      cancelAnimationFrame(frame);
      gl.viewport(0, 0, canvas.width, canvas.height);
    },
    metrics: () => ({ drawn_frames: drawn, running }),
    dispose() {
      if (disposed) return;
      running = false;
      cancelAnimationFrame(frame);
      disposed = true;
      gl.deleteTexture(texture);
      gl.deleteVertexArray(vao);
      gl.deleteProgram(program);
    },
  };
}
