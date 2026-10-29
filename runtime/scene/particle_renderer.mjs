// Device boundary: vertices, colors, atlas coordinates and ordering come from
// the C particle runtime. Native view space points forward along positive Z.
export function particleDrawPacket(sample, textureCount) {
  if (!sample || !(sample.vertices instanceof ArrayBuffer) || !Array.isArray(sample.batches))
    throw Error('invalid particle packet');
  const vertices = new Float32Array(sample.vertices);
  if (
    vertices.length % 9 ||
    vertices.length > 65536 * 6 * 9 ||
    vertices.some((v) => !Number.isFinite(v))
  )
    throw Error('invalid particle vertices');
  let end = 0;
  for (const b of sample.batches) {
    if (
      !Number.isInteger(b.texture) ||
      b.texture < 0 ||
      b.texture >= textureCount ||
      ![0, 1, 2, 4].includes(b.blend) ||
      !Number.isInteger(b.start) ||
      b.start !== end ||
      !Number.isInteger(b.count) ||
      b.count < 0 ||
      b.count % 6
    )
      throw Error('invalid particle batch');
    end += b.count;
  }
  if (end !== vertices.length / 9) throw Error('particle batch coverage');
  return vertices;
}
export function createParticleRenderer(gl, textures) {
  let program,
    vao,
    buffer,
    disposed = false;
  const shaders = [];
  const dispose = () => {
    if (disposed) return;
    disposed = true;
    if (buffer) gl.deleteBuffer(buffer);
    if (vao) gl.deleteVertexArray(vao);
    if (program) gl.deleteProgram(program);
  };
  try {
    program = gl.createProgram();
    if (!program) throw Error('particle program allocation failed');
    for (const [type, source] of [
      [
        gl.VERTEX_SHADER,
        `#version 300 es
precision highp float;layout(location=0)in vec3 position;layout(location=1)in vec2 coords;layout(location=2)in vec4 tint;uniform mat4 projection;out vec2 uv;out vec4 color;void main(){uv=coords;color=tint;gl_Position=projection*vec4(position.xy,-position.z,1.);}`,
      ],
      [
        gl.FRAGMENT_SHADER,
        `#version 300 es
precision highp float;in vec2 uv;in vec4 color;uniform sampler2D image;uniform float alphaCutoff;out vec4 pixel;void main(){pixel=texture(image,uv)*color;if(pixel.a<alphaCutoff)discard;}`,
      ],
    ]) {
      const shader = gl.createShader(type);
      if (!shader) throw Error('particle shader allocation failed');
      shaders.push(shader);
      gl.shaderSource(shader, source);
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
    if (!vao || !buffer) throw Error('particle buffer allocation failed');
    gl.bindVertexArray(vao);
    gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
    for (const [index, size, offset] of [
      [0, 3, 0],
      [1, 2, 12],
      [2, 4, 20],
    ]) {
      gl.enableVertexAttribArray(index);
      gl.vertexAttribPointer(index, size, gl.FLOAT, false, 36, offset);
    }
  } catch (error) {
    dispose();
    throw error;
  } finally {
    for (const shader of shaders) gl.deleteShader(shader);
  }
  const projectionLocation = gl.getUniformLocation(program, 'projection'),
    imageLocation = gl.getUniformLocation(program, 'image'),
    cutoffLocation = gl.getUniformLocation(program, 'alphaCutoff');
  return {
    draw(sample, projection) {
      if (disposed) throw Error('particle renderer disposed');
      const vertices = particleDrawPacket(sample, textures.length);
      if (!vertices.length) return { particles: 0, particle_batches: 0 };
      gl.useProgram(program);
      gl.bindVertexArray(vao);
      gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
      gl.bufferData(gl.ARRAY_BUFFER, vertices, gl.DYNAMIC_DRAW);
      gl.uniformMatrix4fv(projectionLocation, false, projection);
      gl.uniform1i(imageLocation, 0);
      gl.activeTexture(gl.TEXTURE0);
      gl.disable(gl.CULL_FACE);
      gl.enable(gl.DEPTH_TEST);
      gl.depthFunc(gl.LEQUAL);
      gl.depthMask(false);
      gl.enable(gl.BLEND);
      gl.blendEquation(gl.FUNC_ADD);
      try {
        for (const b of sample.batches) {
          if (b.blend < 2) gl.disable(gl.BLEND);
          else gl.enable(gl.BLEND);
          gl.uniform1f(cutoffLocation, b.blend === 1 ? 224 / 255 : b.blend === 0 ? 0 : 1 / 255);
          gl.blendFunc(gl.SRC_ALPHA, b.blend === 2 ? gl.ONE_MINUS_SRC_ALPHA : gl.ONE);
          gl.bindTexture(gl.TEXTURE_2D, textures[b.texture]);
          gl.drawArrays(gl.TRIANGLES, b.start, b.count);
        }
      } finally {
        gl.depthMask(true);
      }
      if (gl.getError() !== gl.NO_ERROR) throw Error('particle GPU draw failed');
      return { particles: vertices.length / 54, particle_batches: sample.batches.length };
    },
    dispose,
  };
}
