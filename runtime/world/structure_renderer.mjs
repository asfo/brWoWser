// Static WMO drawing boundary. Geometry, batches and instance transforms are
// decoded in C/WASM; this module owns only WebGL handles and submissions.
export function createStructureRenderer(gl, mesh, materials, images) {
  const buffers = [],
    textures = [],
    shaders = [];
  let program,
    vao,
    disposed = false;
  const dispose = () => {
    if (disposed) return;
    disposed = true;
    for (const b of buffers) gl.deleteBuffer(b);
    for (const t of textures) gl.deleteTexture(t);
    for (const s of shaders) gl.deleteShader(s);
    if (program) gl.deleteProgram(program);
    if (vao) gl.deleteVertexArray(vao);
  };
  const shader = (kind, source) => {
    const s = gl.createShader(kind);
    shaders.push(s);
    gl.shaderSource(s, source);
    gl.compileShader(s);
    if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) throw Error(gl.getShaderInfoLog(s));
    return s;
  };
  try {
    program = gl.createProgram();
    gl.attachShader(
      program,
      shader(
        gl.VERTEX_SHADER,
        `#version 300 es
precision highp float;layout(location=0) in vec3 position;layout(location=1) in vec3 normal;layout(location=2) in vec2 uv;layout(location=3) in vec3 tint;
uniform mat4 transform;uniform mat4 placement;uniform vec3 eye;out vec2 coords;out vec3 shade;out float distanceToEye;
void main(){vec4 p=placement*vec4(position,1.);coords=uv;shade=tint*(.7+.3*max(dot(normalize(mat3(placement)*normal),normalize(vec3(-.4,-.3,.8))),0.));distanceToEye=length(p.xyz-eye);gl_Position=transform*vec4(p.xyz-eye,1.);}`,
      ),
    );
    gl.attachShader(
      program,
      shader(
        gl.FRAGMENT_SHADER,
        `#version 300 es
precision highp float;in vec2 coords;in vec3 shade;in float distanceToEye;out vec4 color;uniform sampler2D image;uniform int mode;
void main(){vec4 c=texture(image,coords);if(mode==1&&c.a<.5)discard;c.rgb*=shade;c.rgb=mix(c.rgb,vec3(.53,.67,.78),clamp((distanceToEye-650.)/450.,0.,1.));color=vec4(c.rgb,mode==0?1.:c.a);}`,
      ),
    );
    gl.linkProgram(program);
    if (!gl.getProgramParameter(program, gl.LINK_STATUS))
      throw Error(gl.getProgramInfoLog(program));
    const loc = (name) => gl.getUniformLocation(program, name),
      transform = loc('transform'),
      placement = loc('placement'),
      eye = loc('eye'),
      mode = loc('mode');
    gl.useProgram(program);
    gl.uniform1i(loc('image'), 0);
    vao = gl.createVertexArray();
    gl.bindVertexArray(vao);
    const vertex = gl.createBuffer(),
      index = gl.createBuffer();
    buffers.push(vertex, index);
    gl.bindBuffer(gl.ARRAY_BUFFER, vertex);
    gl.bufferData(gl.ARRAY_BUFFER, mesh.vertices, gl.STATIC_DRAW);
    for (const [slot, size, offset] of [
      [0, 3, 0],
      [1, 3, 12],
      [2, 2, 24],
      [3, 3, 32],
    ]) {
      gl.enableVertexAttribArray(slot);
      gl.vertexAttribPointer(slot, size, gl.FLOAT, false, 44, offset);
    }
    gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, index);
    gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, mesh.indices, gl.STATIC_DRAW);
    for (const image of images) {
      if (!image) {
        textures.push(null);
        continue;
      }
      const t = gl.createTexture();
      textures.push(t);
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
      gl.generateMipmap(gl.TEXTURE_2D);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR_MIPMAP_LINEAR);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.REPEAT);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.REPEAT);
    }
    return {
      dispose,
      draw(view, matrix) {
        if (disposed) return 0;
        gl.useProgram(program);
        gl.bindVertexArray(vao);
        gl.enable(gl.DEPTH_TEST);
        gl.depthFunc(gl.LEQUAL);
        gl.depthMask(true);
        gl.disable(gl.CULL_FACE);
        gl.uniformMatrix4fv(transform, false, view.relativeTransform);
        gl.uniformMatrix4fv(placement, false, matrix);
        gl.uniform3fv(eye, view.eye);
        gl.activeTexture(gl.TEXTURE0);
        let triangles = 0;
        for (let i = 0; i < mesh.batches.length; i += 8) {
          const id = mesh.batches[i + 2],
            material = materials[id];
          if (!material || !textures[id])
            throw Error('World structure material outside source table');
          gl.bindTexture(gl.TEXTURE_2D, textures[id]);
          gl.uniform1i(mode, material.mode);
          if (material.mode < 2) {
            gl.disable(gl.BLEND);
            gl.depthMask(true);
          } else {
            gl.enable(gl.BLEND);
            gl.depthMask(false);
            gl.blendEquation(gl.FUNC_ADD);
            gl.blendFunc(
              gl.SRC_ALPHA,
              material.mode === 3 || material.mode === 4 ? gl.ONE : gl.ONE_MINUS_SRC_ALPHA,
            );
          }
          gl.drawElements(gl.TRIANGLES, mesh.batches[i + 1], gl.UNSIGNED_INT, mesh.batches[i] * 4);
          triangles += mesh.batches[i + 1] / 3;
        }
        gl.depthMask(true);
        return triangles;
      },
    };
  } catch (error) {
    dispose();
    throw error;
  }
}
