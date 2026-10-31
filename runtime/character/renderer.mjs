import { stageTranslation } from '../scene/uv_state.mjs';
import { loadCharacterModel } from './model.mjs';
import { createParticleRenderer } from '../scene/particle_renderer.mjs';
const identityPlacement = new Float32Array([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]);

// Canvas-only preview of the current client-owned player mesh. This is a
// separate scene from the login dragon; resources are released on replacement.
export async function createCharacterPreview({
  gl,
  canvas,
  readAsset,
  decodeTexture,
  appearance,
  catalog,
  modules,
  prepareWasm,
  backgroundPath,
  attachmentSpec,
  worldPropPath,
  isCurrent,
  onIssue = () => {},
}) {
  const model = await loadCharacterModel({
    readAsset,
    decodeTexture,
    appearance,
    catalog,
    modules,
    prepareWasm,
    backgroundPath,
    attachmentSpec,
    worldPropPath,
    isCurrent,
    onIssue,
  });
  let program,
    vao,
    particleRenderer,
    disposed = false;
  let lastPointLights = [];
  const attachments = [];
  const buffers = [],
    textures = [],
    shaders = [];
  function dispose() {
    if (disposed) return;
    disposed = true;
    particleRenderer?.dispose();
    for (const child of attachments) child.preview.dispose();
    for (const buffer of buffers) gl.deleteBuffer(buffer);
    for (const texture of textures) if (texture) gl.deleteTexture(texture);
    for (const shader of shaders) gl.deleteShader(shader);
    if (vao) gl.deleteVertexArray(vao);
    if (program) gl.deleteProgram(program);
  }
  const shader = (kind, source) => {
    const result = gl.createShader(kind);
    if (!result) throw Error('Character shader allocation failed');
    shaders.push(result);
    gl.shaderSource(result, source);
    gl.compileShader(result);
    if (!gl.getShaderParameter(result, gl.COMPILE_STATUS)) throw Error(gl.getShaderInfoLog(result));
    return result;
  };
  try {
    program = gl.createProgram();
    if (!program) throw Error('Character shader program allocation failed');
    gl.attachShader(
      program,
      shader(
        gl.VERTEX_SHADER,
        `#version 300 es
      precision highp float;
      layout(location=0) in vec3 position;layout(location=1) in vec3 normal;layout(location=2) in vec4 uv;
      uniform mat4 transform;uniform mat4 placement;uniform float facing;uniform vec3 eye;uniform int uvset;uniform vec2 uvTranslation;uniform int uvset1;uniform vec2 uvTranslation1;
      out vec2 coords;out vec2 coords1;out vec3 normalVector;out float eyeDistance;out vec3 worldPosition;
      void main(){float c=cos(facing),s=sin(facing);mat3 rotation=mat3(c,s,0.,-s,c,0.,0.,0.,1.);
      normalVector=normalize(mat3(placement)*rotation*normal);
      coords=(uvset==0?uv.xy:uv.zw)+uvTranslation;coords1=(uvset1==0?uv.xy:uv.zw)+uvTranslation1;vec3 point=(placement*vec4(rotation*position,1.)).xyz;worldPosition=point;eyeDistance=length(point-eye);gl_Position=transform*vec4(point,1.);}`,
      ),
    );
    gl.attachShader(
      program,
      shader(
        gl.FRAGMENT_SHADER,
        `#version 300 es
      precision highp float;in vec2 coords;in vec2 coords1;in vec3 normalVector;in float eyeDistance;in vec3 worldPosition;out vec4 color;
      uniform sampler2D image0;uniform sampler2D image1;uniform int layers;uniform float alphaCutoff;
      uniform vec3 ambient;uniform vec3 lightDirection[4];uniform vec3 lightColor[4];uniform int lightCount;
      uniform bool fogEnabled;uniform vec3 fogColor;uniform vec2 fogRange;uniform int blendMode;
      uniform vec4 materialColor;
      uniform int pointCount;uniform vec3 pointPosition[16];uniform vec3 pointAmbient[16];uniform vec3 pointDiffuse[16];uniform vec2 pointRange[16];
      uniform bool unlit;void main(){vec4 value=texture(image0,coords);
      if(layers==2)value*=texture(image1,coords1);value*=materialColor;
      // Opaque skin alpha is a material mask, not canvas transparency.
      if(blendMode==0)value.a=1.;if(value.a<alphaCutoff)discard;
      vec3 light=ambient;for(int i=0;i<4;i++){if(i>=lightCount)break;light+=lightColor[i]*max(dot(normalize(normalVector),normalize(-lightDirection[i])),0.);}
      for(int i=0;i<16;i++){if(i>=pointCount)break;vec3 delta=pointPosition[i]-worldPosition;float distanceToLight=length(delta);float attenuation=clamp((pointRange[i].y-distanceToLight)/max(.001,pointRange[i].y-pointRange[i].x),0.,1.);light+=attenuation*(pointAmbient[i]+pointDiffuse[i]*max(dot(normalize(normalVector),delta/max(.001,distanceToLight)),0.));}
      vec3 rgb=value.rgb*(unlit?vec3(1.):clamp(light,0.,1.));
      if(fogEnabled){vec3 target=fogColor;if(blendMode==3||blendMode==4)target=vec3(0.);else if(blendMode==5)target=vec3(1.);else if(blendMode==6)target=vec3(.5);
      rgb=mix(rgb,target,clamp((eyeDistance-fogRange.x)/max(.001,fogRange.y-fogRange.x),0.,1.));}
      color=vec4(rgb,value.a);}`,
      ),
    );
    gl.linkProgram(program);
    if (!gl.getProgramParameter(program, gl.LINK_STATUS))
      throw Error(gl.getProgramInfoLog(program));
    vao = gl.createVertexArray();
    if (!vao) throw Error('Character vertex array allocation failed');
    gl.bindVertexArray(vao);
    const attribute = (slot, count, bytes, usage) => {
      const buffer = gl.createBuffer();
      if (!buffer) throw Error('Character vertex buffer allocation failed');
      buffers.push(buffer);
      gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
      gl.bufferData(gl.ARRAY_BUFFER, bytes, usage);
      gl.enableVertexAttribArray(slot);
      gl.vertexAttribPointer(slot, count, gl.FLOAT, false, 0, 0);
      return buffer;
    };
    const positions = attribute(0, 3, model.positions, gl.DYNAMIC_DRAW);
    const normals = attribute(1, 3, model.normals, gl.DYNAMIC_DRAW);
    attribute(2, 4, model.uv, gl.STATIC_DRAW);
    const indices = gl.createBuffer();
    if (!indices) throw Error('Character index buffer allocation failed');
    buffers.push(indices);
    gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, indices);
    gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, model.indices, gl.STATIC_DRAW);
    for (const [index, source] of model.textures.entries()) {
      if (!source) {
        textures.push(null);
        continue;
      }
      if (
        !(source.rgba instanceof Uint8Array) ||
        source.rgba.length !== source.width * source.height * 4
      )
        throw Error('Character texture dimensions rejected');
      const texture = gl.createTexture();
      if (!texture) throw Error('Character texture allocation failed');
      textures.push(texture);
      gl.bindTexture(gl.TEXTURE_2D, texture);
      gl.pixelStorei(gl.UNPACK_ALIGNMENT, 1);
      gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, false);
      gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, false);
      gl.texImage2D(
        gl.TEXTURE_2D,
        0,
        gl.RGBA8,
        source.width,
        source.height,
        0,
        gl.RGBA,
        gl.UNSIGNED_BYTE,
        source.rgba,
      );
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
      gl.texParameteri(
        gl.TEXTURE_2D,
        gl.TEXTURE_WRAP_S,
        model.textureFlags[index] & 1 ? gl.REPEAT : gl.CLAMP_TO_EDGE,
      );
      gl.texParameteri(
        gl.TEXTURE_2D,
        gl.TEXTURE_WRAP_T,
        model.textureFlags[index] & 2 ? gl.REPEAT : gl.CLAMP_TO_EDGE,
      );
    }
    const locations = new Map();
    const location = (name) => {
      if (!locations.has(name)) locations.set(name, gl.getUniformLocation(program, name));
      return locations.get(name);
    };
    const transform = location('transform'),
      facingLocation = location('facing'),
      layerLocation = location('layers'),
      cutLocation = location('alphaCutoff'),
      unlitLocation = location('unlit');
    const preview = {
      cameraValues: model.camera,
      animated: model.animated,
      modelPath: model.metadata.modelPath,
      attachment: (id, elapsed) => model.attachment(id, elapsed),
      lights: () => lastPointLights,
      dispose,
      draw({
        elapsed = 0,
        facing = 0,
        viewport = { left: 0, bottom: 0, width: canvas.width, height: canvas.height },
        clear = true,
        preserveDepth = false,
        lighting,
        camera = model.camera,
        diagonalFraming = false,
        pointLights,
        placement = identityPlacement,
        viewState,
        moving = false,
      } = {}) {
        if (disposed || gl.isContextLost()) throw Error('Character preview is unavailable');
        if (
          ![elapsed, facing, viewport.left, viewport.bottom, viewport.width, viewport.height].every(
            Number.isFinite,
          ) ||
          viewport.width <= 0 ||
          viewport.height <= 0
        )
          throw Error('Invalid character preview viewport or clock');
        gl.bindVertexArray(vao);
        gl.useProgram(program);
        gl.viewport(viewport.left, viewport.bottom, viewport.width, viewport.height);
        gl.enable(gl.SCISSOR_TEST);
        gl.scissor(viewport.left, viewport.bottom, viewport.width, viewport.height);
        gl.depthMask(true);
        gl.clearDepth(1);
        if (clear) {
          gl.clearColor(0.025, 0.035, 0.05, 1);
          gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
        } else if (!preserveDepth) gl.clear(gl.DEPTH_BUFFER_BIT);
        gl.enable(gl.DEPTH_TEST);
        gl.depthFunc(gl.LEQUAL);
        gl.disable(gl.CULL_FACE);
        const {
          projection,
          transform: matrix,
          particleView,
          view,
          eye,
        } = viewState || model.view(viewport.width / viewport.height, camera, diagonalFraming);
        gl.uniformMatrix4fv(transform, false, matrix);
        gl.uniformMatrix4fv(location('placement'), false, placement);
        gl.uniform1f(facingLocation, facing);
        gl.uniform3fv(location('eye'), eye);
        gl.uniform1i(location('fogEnabled'), !!lighting?.fog?.enabled);
        gl.uniform3fv(location('fogColor'), lighting?.fog?.rgb || [0, 0, 0]);
        gl.uniform2fv(location('fogRange'), [
          lighting?.fog?.near || 0,
          lighting?.fog?.far || 100000,
        ]);
        gl.uniform1i(location('image0'), 0);
        gl.uniform1i(location('image1'), 1);
        const lights = (lighting?.lights || [])
          .filter((row) => row?.length === 13 && row[0])
          .slice(0, 4);
        const ambient = lights.length ? [0, 0, 0] : [0.55, 0.55, 0.55];
        for (const row of lights) for (let i = 0; i < 3; i++) ambient[i] += row[6 + i] * row[5];
        gl.uniform3fv(location('ambient'), ambient);
        gl.uniform1i(location('lightCount'), lights.length || 1);
        for (let i = 0; i < (lights.length || 1); i++) {
          const row = lights[i];
          gl.uniform3fv(location(`lightDirection[${i}]`), row ? row.slice(2, 5) : [-1, 0.5, -1]);
          gl.uniform3fv(
            location(`lightColor[${i}]`),
            row ? row.slice(10, 13).map((value) => value * row[9]) : [0.45, 0.45, 0.45],
          );
        }
        model.motion(moving, elapsed);
        if (model.animated) {
          gl.bindBuffer(gl.ARRAY_BUFFER, positions);
          gl.bufferSubData(
            gl.ARRAY_BUFFER,
            0,
            model.sample(elapsed, model.localView(view, placement)),
          );
          gl.bindBuffer(gl.ARRAY_BUFFER, normals);
          gl.bufferSubData(gl.ARRAY_BUFFER, 0, model.animatedNormals());
        }
        lastPointLights = pointLights || model.pointLights(elapsed);
        gl.uniform1i(location('pointCount'), lastPointLights.length);
        for (let i = 0; i < lastPointLights.length; i++) {
          const light = lastPointLights[i];
          gl.uniform3fv(location(`pointPosition[${i}]`), light.subarray(0, 3));
          gl.uniform3fv(location(`pointAmbient[${i}]`), light.subarray(3, 6));
          gl.uniform3fv(location(`pointDiffuse[${i}]`), light.subarray(6, 9));
          gl.uniform2fv(location(`pointRange[${i}]`), light.subarray(9, 11));
        }
        let uvMatrices = null;
        if (model.uvAnimated) {
          try {
            uvMatrices = model.uvMatrices(elapsed);
          } catch (error) {
            model.uvAnimated = false;
            onIssue('Background cloud scroll stopped: ' + error.message);
          }
        }
        let triangles = 0,
          eyeGlowTriangles = 0;
        const appearance = model.appearance(elapsed);
        for (const i of model.drawOrder(eye)) {
          if (appearance[i * 4 + 3] < 0.0001) continue;
          gl.uniform4fv(location('materialColor'), appearance.subarray(i * 4, i * 4 + 4));
          const at = i * 9,
            range = model.ranges[model.rows[at]];
          if (!model.visible(range.geoset)) continue;
          const flags = model.rows[at + 2],
            mode = model.rows[at + 3],
            start = model.rows[at + 6],
            count = model.rows[at + 7];
          const coord = model.stages[start * 3 + 1];
          const coord1 = count > 1 ? model.stages[(start + 1) * 3 + 1] : coord;
          // Environment and generated coordinates are not a mesh UV. Drawing
          // them with the first set stretches black strips and static smoke.
          if (model.metadata.isBackground && coord > 1) continue;
          const first = model.stages[start * 3],
            second = count > 1 ? model.stages[(start + 1) * 3] : first;
          if (!textures[first] || !textures[second])
            throw Error('Character batch texture unavailable');
          gl.activeTexture(gl.TEXTURE0);
          gl.bindTexture(gl.TEXTURE_2D, textures[first]);
          gl.activeTexture(gl.TEXTURE1);
          gl.bindTexture(gl.TEXTURE_2D, textures[second]);
          gl.uniform1i(location('uvset'), coord === 1 ? 1 : 0);
          gl.uniform1i(location('uvset1'), coord1 === 1 ? 1 : 0);
          gl.uniform2fv(
            location('uvTranslation'),
            uvMatrices ? stageTranslation(model.rows, model.stages, i, uvMatrices) : [0, 0],
          );
          gl.uniform2fv(
            location('uvTranslation1'),
            uvMatrices && count > 1
              ? stageTranslation(model.rows, model.stages, i, uvMatrices, 1)
              : [0, 0],
          );
          gl.uniform1i(layerLocation, count);
          gl.uniform1i(location('blendMode'), mode);
          gl.uniform1f(cutLocation, mode === 1 ? 224 / 255 : mode === 0 ? 0 : 1 / 255);
          gl.uniform1i(unlitLocation, !!(flags & 1));
          gl.uniform1i(location('fogEnabled'), !!lighting?.fog?.enabled && !(flags & 2));
          gl.depthFunc(flags & 8 ? gl.ALWAYS : gl.LEQUAL);
          gl.depthMask(!(flags & 16));
          if (flags & 4) gl.disable(gl.CULL_FACE);
          else {
            gl.enable(gl.CULL_FACE);
            gl.cullFace(gl.BACK);
            gl.frontFace(gl.CCW);
          }
          if (mode < 2) gl.disable(gl.BLEND);
          else {
            gl.enable(gl.BLEND);
            gl.blendEquation(gl.FUNC_ADD);
            if (mode === 3) gl.blendFunc(gl.ONE, gl.ONE);
            else if (mode === 4) gl.blendFunc(gl.SRC_ALPHA, gl.ONE);
            else if (mode === 5) gl.blendFunc(gl.DST_COLOR, gl.ZERO);
            else if (mode === 6) gl.blendFunc(gl.DST_COLOR, gl.SRC_COLOR);
            else gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
          }
          gl.drawElements(gl.TRIANGLES, range.count, gl.UNSIGNED_INT, range.start * 4);
          triangles += range.count / 3;
          if (!model.metadata.isBackground && Math.floor(range.geoset / 100) === 17)
            eyeGlowTriangles += range.count / 3;
        }
        const particleSample = model.sampleParticles(
          elapsed,
          model.localView(particleView, placement),
        );
        let particleStats = { particles: 0, particle_batches: 0 };
        if (particleSample)
          particleStats = (particleRenderer ??= createParticleRenderer(gl, textures)).draw(
            particleSample,
            projection,
          );
        gl.depthMask(true);
        gl.disable(gl.SCISSOR_TEST);
        gl.activeTexture(gl.TEXTURE0);
        for (const child of attachments)
          child.preview.draw({
            elapsed,
            viewport,
            clear: false,
            preserveDepth: true,
            lighting,
            camera,
            diagonalFraming,
            pointLights: lastPointLights,
            placement: model.placeAttachment(child.id, elapsed, placement, facing),
            viewState,
          });
        return {
          triangles,
          eye_glow_triangles: eyeGlowTriangles,
          animated: model.animated,
          ...particleStats,
          particleStatus: model.particleStatus,
        };
      },
    };
    for (const spec of model.metadata.attachments || []) {
      if (isCurrent && !isCurrent()) throw Error('Character load superseded');
      const child = await createCharacterPreview({
        gl,
        canvas,
        readAsset,
        decodeTexture,
        catalog,
        modules,
        prepareWasm,
        attachmentSpec: spec,
        isCurrent,
        onIssue,
      });
      attachments.push({ id: spec.attachmentId, preview: child });
    }
    return preview;
  } catch (error) {
    dispose();
    throw error;
  }
}
