import { createParticleRenderer } from './particle_renderer.mjs';
import { uvVertexExpression, stageTranslation } from './uv_state.mjs';
import { materialState, applyMaterialState } from './material_state.mjs';
import { sceneFragmentSource as materialFragmentSource } from './scene_shader.mjs';
import { planDraws } from './draw_plan.mjs';
export function createSceneRenderer(gl, canvas, payload, cameraValues, pool) {
  let disposed = false,
    particleRenderer;
  if (!gl || gl.isContextLost()) throw Error('WebGL2 unavailable');
  const shaders = [];
  let p;
  function shader(type, source) {
    const object = gl.createShader(type);
    if (!object) throw Error('scene shader allocation failed');
    shaders.push(object);
    gl.shaderSource(object, source);
    gl.compileShader(object);
    if (!gl.getShaderParameter(object, gl.COMPILE_STATUS))
      throw Error(gl.getShaderInfoLog(object) || 'scene shader compilation failed');
    return object;
  }
  try {
    p = gl.createProgram();
    if (!p) throw Error('scene program allocation failed');
    gl.attachShader(
      p,
      shader(
        gl.VERTEX_SHADER,
        `#version 300 es
 precision highp float;layout(location=0)in vec3 position;layout(location=1)in vec4 coords;uniform mat4 view;uniform mat4 projection;uniform int uvset;uniform vec2 uvTranslation;uniform int uvset1;uniform vec2 uvTranslation1;out vec2 texcoord;out vec2 texcoord1;void main(){${uvVertexExpression}texcoord1=(uvset1==0?coords.xy:coords.zw)+uvTranslation1;gl_Position=projection*view*vec4(position,1.);}`,
      ),
    );
    gl.attachShader(p, shader(gl.FRAGMENT_SHADER, materialFragmentSource));
    gl.linkProgram(p);
    if (!gl.getProgramParameter(p, gl.LINK_STATUS))
      throw Error(gl.getProgramInfoLog(p) || 'scene program link failed');
  } catch (error) {
    if (p) gl.deleteProgram(p);
    throw error;
  } finally {
    for (const object of shaders) gl.deleteShader(object);
  }
  gl.useProgram(p);

  let vao;
  try {
    const positionBuffer = pool.buffer('posed-positions', 'vertex'),
      records = pool.buffer('prepared-records', 'vertex'),
      indicesBuffer = pool.buffer('indices-u32', 'index');
    if (!positionBuffer || !records || !indicesBuffer) throw Error('GPU scene buffers unavailable');
    vao = gl.createVertexArray();
    if (!vao) throw Error('scene VAO allocation failed');
    gl.bindVertexArray(vao);
    gl.bindBuffer(gl.ARRAY_BUFFER, positionBuffer);
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 12, 0);
    gl.bindBuffer(gl.ARRAY_BUFFER, records);
    gl.enableVertexAttribArray(1);
    gl.vertexAttribPointer(1, 4, gl.FLOAT, false, 48, 32);
    gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, indicesBuffer);
    const textures = payload.textures.map((row) => {
      const texture = pool.texture(row.name);
      if (!texture) throw Error('GPU scene texture unavailable');
      return texture;
    });

    const dot = (a, b) => a.reduce((s, x, i) => s + x * b[i], 0),
      cross = (a, b) => [
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
      ],
      norm = (a) => {
        const n = Math.hypot(...a);
        return a.map((x) => x / n);
      };
    let yaw = 0.7,
      pitch = 0.15;
    const rows = payload.rows,
      stages = payload.stages,
      ranges = payload.ranges,
      viewloc = gl.getUniformLocation(p, 'view'),
      ploc = gl.getUniformLocation(p, 'projection'),
      uvloc = gl.getUniformLocation(p, 'uvset'),
      uvTranslationLoc = gl.getUniformLocation(p, 'uvTranslation'),
      cutloc = gl.getUniformLocation(p, 'alphaCutoff'),
      opacityloc = gl.getUniformLocation(p, 'opacity'),
      tintloc = gl.getUniformLocation(p, 'tint'),
      uv1loc = gl.getUniformLocation(p, 'uvset1'),
      translation1loc = gl.getUniformLocation(p, 'uvTranslation1'),
      combinerloc = gl.getUniformLocation(p, 'combiner'),
      resolvedPasses = new Uint32Array(payload.resolvedPasses);
    let last = { batch: -1, fileCamera: true, materialMode: 'scene', frontFace: 'ccw' },
      current = {
        positions: payload.positions,
        appearance: payload.appearance,
        cameraOffset: payload.cameraOffset,
        distances: payload.distances,
        uvMatrices: payload.uvMatrices,
        sequenceMs: 0,
        globalMs: 0,
      };

    let positions = new Float32Array(payload.positions);
    const indices = new Uint32Array(payload.indices);
    function selectedBounds(batch) {
      if (batch < 0) return payload.bounds;
      const range = ranges[rows[batch * 9]];
      if (!range.index_count) return payload.bounds;
      const min = [Infinity, Infinity, Infinity],
        max = [-Infinity, -Infinity, -Infinity];
      for (let i = range.index_start; i < range.index_start + range.index_count; i++)
        for (let j = 0; j < 3; j++) {
          const v = positions[indices[i] * 3 + j];
          min[j] = Math.min(min[j], v);
          max[j] = Math.max(max[j], v);
        }
      return { min, max };
    }
    function draw(options = last) {
      if (disposed) throw Error('scene renderer disposed');
      gl.useProgram(p);
      gl.bindVertexArray(vao);
      gl.activeTexture(gl.TEXTURE0);
      gl.uniform1i(gl.getUniformLocation(p, 'image0'), 0);
      gl.uniform1i(gl.getUniformLocation(p, 'image1'), 1);
      last = options;
      const {
          batch,
          fileCamera = true,
          materialMode = 'scene',
          frontFace = 'ccw',
          passPolicy = 'baseline',
          measurePixels = true,
        } = options,
        aspect = canvas.width / canvas.height,
        bounds = selectedBounds(batch),
        center = bounds.min.map((v, i) => (v + bounds.max[i]) / 2),
        radius = Math.max(0.01, Math.hypot(...bounds.min.map((v, i) => (bounds.max[i] - v) / 2)));
      let eye,
        target,
        roll = 0,
        fov = 0.85,
        near = 0.1,
        far = radius * 20;
      if (!fileCamera) near = Math.min(0.1, radius / 100);
      if (fileCamera) {
        const offset = current.cameraOffset;
        eye = cameraValues.slice(3, 6).map((v, i) => v + offset[i]);
        target = cameraValues.slice(6, 9);
        roll = cameraValues[9];
        fov = cameraValues[0] / Math.sqrt(1 + aspect * aspect);
        near = cameraValues[2];
        far = cameraValues[1];
      } else {
        target = center;
        eye = center.map(
          (v, i) =>
            v +
            [Math.cos(yaw) * Math.cos(pitch), Math.sin(yaw) * Math.cos(pitch), Math.sin(pitch)][i] *
              radius *
              2.8,
        );
      }
      const f = norm(target.map((x, i) => x - eye[i])),
        r0 = norm(cross(f, [0, 0, 1])),
        u0 = cross(r0, f),
        r = r0.map((x, i) => x * Math.cos(roll) + u0[i] * Math.sin(roll)),
        u = u0.map((x, i) => x * Math.cos(roll) - r0[i] * Math.sin(roll)),
        view = new Float32Array([
          r[0],
          u[0],
          -f[0],
          0,
          r[1],
          u[1],
          -f[1],
          0,
          r[2],
          u[2],
          -f[2],
          0,
          -dot(r, eye),
          -dot(u, eye),
          dot(f, eye),
          1,
        ]),
        cot = 1 / Math.tan(fov / 2),
        projection = new Float32Array([
          cot / aspect,
          0,
          0,
          0,
          0,
          cot,
          0,
          0,
          0,
          0,
          (far + near) / (near - far),
          -1,
          0,
          0,
          (2 * far * near) / (near - far),
          0,
        ]);
      if (!['baseline', 'first'].includes(passPolicy)) throw Error('invalid scene pass policy');
      gl.viewport(0, 0, canvas.width, canvas.height);
      gl.clearColor(0.063, 0.082, 0.122, 1);
      gl.depthMask(true);
      gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
      gl.enable(gl.DEPTH_TEST);
      gl.disable(gl.CULL_FACE);
      gl.disable(gl.BLEND);
      gl.uniformMatrix4fv(viewloc, false, view);
      gl.uniformMatrix4fv(ploc, false, projection);
      let drawn = 0,
        triangles = 0;
      const plan = planDraws(rows, stages, ranges, new Float32Array(current.appearance), {
        batch,
        mode: materialMode,
        priorities: payload.priorities,
        distances: new Float64Array(current.distances),
        resolvedPasses,
        firstPassOnly: passPolicy === 'first',
      });
      for (const item of plan.chosen) {
        const state = materialState(item.flags, item.materialMode, item.opacity);
        applyMaterialState(gl, state, frontFace);
        if (!textures[item.texture] || !textures[item.texture1])
          throw Error('selected pass texture unavailable');
        gl.uniform1i(combinerloc, item.combiner);
        gl.activeTexture(gl.TEXTURE0);
        gl.bindTexture(gl.TEXTURE_2D, textures[item.texture]);
        gl.activeTexture(gl.TEXTURE1);
        gl.bindTexture(gl.TEXTURE_2D, textures[item.texture1]);
        gl.uniform1i(uv1loc, item.coord1);
        gl.uniform2fv(
          translation1loc,
          stageTranslation(
            rows,
            stages,
            item.batch,
            new Float32Array(current.uvMatrices),
            item.secondStage,
          ),
        );
        gl.uniform1i(uvloc, item.coord);
        gl.uniform2fv(
          uvTranslationLoc,
          stageTranslation(rows, stages, item.batch, new Float32Array(current.uvMatrices)),
        );
        gl.uniform1f(cutloc, state.alphaCutoff);
        gl.uniform1f(opacityloc, state.opacity);
        gl.uniform3fv(tintloc, item.tint);
        gl.drawElements(
          gl.TRIANGLES,
          item.range.index_count,
          gl.UNSIGNED_INT,
          item.range.index_start * 4,
        );
        drawn++;
        triangles += item.range.index_count / 3;
      }
      const particleStats = current.particles
        ? (particleRenderer ??= createParticleRenderer(gl, textures)).draw(
            current.particles,
            projection,
          )
        : { particles: 0, particle_batches: 0 };
      gl.activeTexture(gl.TEXTURE0);
      gl.depthMask(true);
      const error = gl.getError();
      if (error !== gl.NO_ERROR) throw Error('WebGL error ' + error);
      let plotted = null;
      if (measurePixels) {
        const pixels = new Uint8Array(canvas.width * canvas.height * 4);
        gl.readPixels(0, 0, canvas.width, canvas.height, gl.RGBA, gl.UNSIGNED_BYTE, pixels);
        plotted = 0;
        for (let i = 0; i < pixels.length; i += 4)
          if (pixels[i] !== 16 || pixels[i + 1] !== 21 || pixels[i + 2] !== 31) plotted++;
      }
      return {
        ...particleStats,
        drawn_batches: drawn,
        two_stage_batches: plan.chosen.filter((item) => item.stageCount === 2).length,
        environment_fallback_batches: plan.environmentFallback,
        pass_policy: passPolicy,
        drawn_triangles: triangles,
        plotted_pixels: plotted,
        camera_ms: current.sequenceMs,
        global_ms: current.globalMs,
        skipped: plan.skipped,
        eye,
        material_policy: materialMode,
        front_face: frontFace,
        webgl_version: gl.getParameter(gl.VERSION),
      };
    }
    function update(sample) {
      if (disposed) throw Error('scene renderer disposed');
      const p = new Float32Array(sample.positions);
      if (p.length !== positions.length || p.some((x) => !Number.isFinite(x)))
        throw Error('invalid animated position buffer');
      const m = new Float32Array(sample.uvMatrices);
      if (
        m.length !== new Float32Array(payload.uvMatrices).length ||
        m.some((x) => !Number.isFinite(x))
      )
        throw Error('invalid UV translation palette');
      const a = new Float32Array(sample.appearance);
      if (a.length !== (rows.length / 9) * 6 || a.some((x) => !Number.isFinite(x)))
        throw Error('invalid animated appearance');
      current = sample;
      positions = p;
      gl.bindBuffer(gl.ARRAY_BUFFER, positionBuffer);
      gl.bufferSubData(gl.ARRAY_BUFFER, 0, p);
    }
    return {
      draw,
      update,
      dispose() {
        if (disposed) return;
        disposed = true;
        particleRenderer?.dispose();
        gl.deleteVertexArray(vao);
        gl.deleteProgram(p);
      },
    };
  } catch (error) {
    if (vao) gl.deleteVertexArray(vao);
    gl.deleteProgram(p);
    throw error;
  }
}
