// Browser GPU boundary. Color bands, spatial lighting selection, game-time
// interpolation and cloud clock are supplied by C/WASM from owner data.
export function createSkyRenderer(gl, image) {
  const shaders = [];
  let program,
    vao,
    texture,
    disposed = false;
  const dispose = () => {
    if (disposed) return;
    disposed = true;
    for (const s of shaders) gl.deleteShader(s);
    if (program) gl.deleteProgram(program);
    if (vao) gl.deleteVertexArray(vao);
    if (texture) gl.deleteTexture(texture);
  };
  const compile = (kind, source) => {
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
      compile(
        gl.VERTEX_SHADER,
        `#version 300 es
precision highp float;out vec2 screen;void main(){vec2 p=vec2(float((gl_VertexID<<1)&2),float(gl_VertexID&2));screen=p*2.-1.;gl_Position=vec4(screen,1.,1.);}`,
      ),
    );
    gl.attachShader(
      program,
      compile(
        gl.FRAGMENT_SHADER,
        `#version 300 es
precision highp float;in vec2 screen;out vec4 pixel;uniform mat4 view;uniform vec2 projectionScale;uniform vec3 bands[18];uniform sampler2D clouds;uniform bool hasClouds;uniform float clock;
void main(){vec3 ray=normalize(transpose(mat3(view))*vec3(screen/projectionScale,-1.));float h=max(0.,ray.z);vec3 c;
if(h<.08)c=mix(bands[6],bands[5],h/.08);else if(h<.25)c=mix(bands[5],bands[4],(h-.08)/.17);else if(h<.6)c=mix(bands[4],bands[3],(h-.25)/.35);else c=mix(bands[3],bands[2],(h-.6)/.4);
if(hasClouds&&ray.z>0.){vec2 uv=ray.xy/(.35+ray.z)*.2+vec2(clock,clock*.3);vec4 source=texture(clouds,uv);float cover=clamp(source.r*source.a,0.,1.)*smoothstep(0.,.15,h);c=mix(c,bands[11],cover*.65);}
pixel=vec4(c,1.);}`,
      ),
    );
    gl.linkProgram(program);
    if (!gl.getProgramParameter(program, gl.LINK_STATUS))
      throw Error(gl.getProgramInfoLog(program));
    vao = gl.createVertexArray();
    const loc = (n) => gl.getUniformLocation(program, n),
      view = loc('view'),
      projection = loc('projectionScale'),
      bands = loc('bands[0]'),
      clock = loc('clock');
    gl.useProgram(program);
    gl.uniform1i(loc('clouds'), 0);
    gl.uniform1i(loc('hasClouds'), !!image);
    if (image) {
      texture = gl.createTexture();
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
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.REPEAT);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.REPEAT);
    }
    return {
      dispose,
      draw(state) {
        if (disposed || !state.sky) return;
        gl.useProgram(program);
        gl.bindVertexArray(vao);
        gl.disable(gl.DEPTH_TEST);
        gl.depthMask(false);
        gl.disable(gl.BLEND);
        gl.disable(gl.CULL_FACE);
        gl.uniformMatrix4fv(view, false, state.view.subarray(0, 16));
        gl.uniform2f(projection, state.view[16], state.view[21]);
        gl.uniform3fv(bands, state.sky.subarray(0, 54));
        gl.uniform1f(clock, state.sky[57]);
        gl.activeTexture(gl.TEXTURE0);
        gl.bindTexture(gl.TEXTURE_2D, texture);
        gl.drawArrays(gl.TRIANGLES, 0, 3);
        gl.depthMask(true);
      },
    };
  } catch (error) {
    dispose();
    throw error;
  }
}
