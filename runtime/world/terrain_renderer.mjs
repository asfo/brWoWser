// WebGL boundary: uploads WASM-owned meshes and applies texture bindings.
export function createTerrainRenderer(gl, tile, images) {
  const resources = [],
    shaders = [];
  let program,
    vao,
    disposed = false;
  function shader(kind, source) {
    const s = gl.createShader(kind);
    shaders.push(s);
    gl.shaderSource(s, source);
    gl.compileShader(s);
    if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) throw Error(gl.getShaderInfoLog(s));
    return s;
  }
  function dispose() {
    if (disposed) return;
    disposed = true;
    for (const [kind, value] of resources) gl[kind](value);
    for (const s of shaders) gl.deleteShader(s);
    if (vao) gl.deleteVertexArray(vao);
    if (program) gl.deleteProgram(program);
  }
  try {
    program = gl.createProgram();
    gl.attachShader(
      program,
      shader(
        gl.VERTEX_SHADER,
        `#version 300 es
precision highp float;
layout(location=0) in vec3 position;layout(location=1) in vec3 normal;
layout(location=2) in vec2 uv;layout(location=3) in vec3 tint;
uniform mat4 transform;uniform vec3 eye;
out vec2 coords;out vec3 shade;out float distanceToEye;
void main(){coords=uv;shade=tint*(.6+.4*max(dot(normalize(normal),normalize(vec3(-.4,-.3,.8))),0.));distanceToEye=length(position-eye);gl_Position=transform*vec4(position,1.);}`,
      ),
    );
    gl.attachShader(
      program,
      shader(
        gl.FRAGMENT_SHADER,
        `#version 300 es
precision highp float;precision highp sampler2DArray;
in vec2 coords;in vec3 shade;in float distanceToEye;out vec4 color;
uniform sampler2D layer0;uniform sampler2D layer1;uniform sampler2D layer2;uniform sampler2D layer3;
uniform sampler2DArray alphaMap;uniform int chunk;uniform int layers;
void main(){vec3 a=texture(alphaMap,vec3(coords,float(chunk))).rgb;
vec3 c=texture(layer0,coords*8.).rgb;
if(layers>1)c=mix(c,texture(layer1,coords*8.).rgb,a.r);
if(layers>2)c=mix(c,texture(layer2,coords*8.).rgb,a.g);
if(layers>3)c=mix(c,texture(layer3,coords*8.).rgb,a.b);
color=vec4(mix(c*shade,vec3(.53,.67,.78),clamp((distanceToEye-650.)/450.,0.,1.)),1.);}`,
      ),
    );
    gl.linkProgram(program);
    if (!gl.getProgramParameter(program, gl.LINK_STATUS))
      throw Error(gl.getProgramInfoLog(program));
    const location = (name) => gl.getUniformLocation(program, name);
    const transform = location('transform'),
      eye = location('eye'),
      chunk = location('chunk'),
      layers = location('layers');
    gl.useProgram(program);
    for (let i = 0; i < 4; i++) gl.uniform1i(location('layer' + i), i);
    gl.uniform1i(location('alphaMap'), 4);
    vao = gl.createVertexArray();
    gl.bindVertexArray(vao);
    const vertex = gl.createBuffer(),
      index = gl.createBuffer();
    resources.push(['deleteBuffer', vertex], ['deleteBuffer', index]);
    gl.bindBuffer(gl.ARRAY_BUFFER, vertex);
    gl.bufferData(gl.ARRAY_BUFFER, tile.vertices, gl.STATIC_DRAW);
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
    gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, tile.indices, gl.STATIC_DRAW);
    const textures = images.map((image) => {
      const texture = gl.createTexture();
      resources.push(['deleteTexture', texture]);
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
      gl.generateMipmap(gl.TEXTURE_2D);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR_MIPMAP_LINEAR);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.REPEAT);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.REPEAT);
      return texture;
    });
    const alpha = gl.createTexture();
    resources.push(['deleteTexture', alpha]);
    gl.bindTexture(gl.TEXTURE_2D_ARRAY, alpha);
    gl.texImage3D(
      gl.TEXTURE_2D_ARRAY,
      0,
      gl.RGBA8,
      64,
      64,
      tile.chunks.length / 8,
      0,
      gl.RGBA,
      gl.UNSIGNED_BYTE,
      tile.alpha,
    );
    gl.texParameteri(gl.TEXTURE_2D_ARRAY, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D_ARRAY, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D_ARRAY, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D_ARRAY, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    return {
      dispose,
      draw(view, visibility) {
        if (disposed) return 0;
        gl.useProgram(program);
        gl.bindVertexArray(vao);
        gl.uniformMatrix4fv(transform, false, view.transform);
        gl.uniform3fv(eye, view.eye);
        gl.enable(gl.DEPTH_TEST);
        gl.depthFunc(gl.LEQUAL);
        gl.depthMask(true);
        gl.disable(gl.BLEND);
        gl.disable(gl.CULL_FACE);
        gl.disable(gl.SCISSOR_TEST);
        gl.activeTexture(gl.TEXTURE4);
        gl.bindTexture(gl.TEXTURE_2D_ARRAY, alpha);
        let triangles = 0;
        for (let i = 0; i < tile.chunks.length; i += 8) {
          const cell = tile.chunks[i + 7];
          if (visibility && !(visibility[cell >>> 5] & (1 << (cell & 31)))) continue;
          if (!tile.chunks[i + 1] || !tile.chunks[i + 2]) continue;
          for (let layer = 0; layer < 4; layer++) {
            const image = textures[tile.chunks[i + 3 + Math.min(layer, tile.chunks[i + 2] - 1)]];
            if (!image) throw Error('Terrain texture index outside source table');
            gl.activeTexture(gl.TEXTURE0 + layer);
            gl.bindTexture(gl.TEXTURE_2D, image);
          }
          gl.uniform1i(chunk, i / 8);
          gl.uniform1i(layers, tile.chunks[i + 2]);
          gl.drawElements(gl.TRIANGLES, tile.chunks[i + 1], gl.UNSIGNED_INT, tile.chunks[i] * 4);
          triangles += tile.chunks[i + 1] / 3;
        }
        return triangles;
      },
    };
  } catch (error) {
    dispose();
    throw error;
  }
}
