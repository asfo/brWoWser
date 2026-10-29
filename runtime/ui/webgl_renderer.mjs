const coverageSignatures = new WeakMap();
function coverageKey(bytes, width, height) {
  let key = coverageSignatures.get(bytes);
  if (key) return key;
  let hash = 2166136261;
  for (const value of bytes) hash = Math.imul(hash ^ value, 16777619);
  key = width + ':' + height + ':' + (hash >>> 0);
  coverageSignatures.set(bytes, key);
  return key;
}
function sameCoverage(a, b) {
  if (a === b) return true;
  if (a.length !== b.length) return false;
  for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false;
  return true;
}
import { uploadWebGLResources } from '../graphics/webgl_resources.mjs';
// Device bridge only: C provides visibility, order, rectangles, UVs and glyph
// quads. No XML, Lua, text measurement, DOM controls or browser font rasterizer.
export async function createUIRenderer(
  gl,
  packet,
  images,
  { logicalWidth = 1024, logicalHeight = 768, textureCache, reuse, glyphAtlas } = {},
) {
  const textures = [],
    commands = [],
    issues = [...packet.issues],
    known = new Map(),
    glyphs = new Map();
  let glyphCount = 0;
  const admit = (name, image) => {
    if (known.has(name)) return;
    known.set(name, true);
    textures.push({ name, ...image });
  };
  for (const item of packet.commands) {
    if (item.scale !== 1) {
      issues.push({ name: item.name, kind: 'mixed-scale-not-implemented' });
      continue;
    }
    if (item.kind === 'images') {
      for (const q of item.quads) {
        const key = q.asset.toLowerCase().replaceAll('\\', '/'),
          image =
            key === '@solid'
              ? { width: 1, height: 1, rgba: new Uint8Array([255, 255, 255, 255]) }
              : images.get(key);
        if (!image) {
          issues.push({ name: item.name, kind: 'texture-not-supplied', path: q.asset });
          continue;
        }
        if (q.asset === item.edge && image.width !== image.height * 8) {
          issues.push({ name: item.name, kind: 'unsupported-edge-atlas' });
          continue;
        }
        admit(key, image);
        commands.push({
          name: key,
          rect: q.rect,
          uv: q.uv,
          color: q.color,
          vertexColors: q.vertexColors,
          desaturated: !!q.desaturated,
          alpha: item.alpha,
          clip: item.clip,
          additive: item.additive,
        });
      }
    } else
      for (let i = 0; i < item.quads.length; i++) {
        const q = item.quads[i],
          width = q.rect[2],
          height = q.rect[3];
        if (!width || !height) continue;
        if (item.clip) {
          const [x, y, w, h] = item.clip;
          if (
            q.rect[0] + width <= x ||
            q.rect[1] + height <= y ||
            q.rect[0] >= x + w ||
            q.rect[1] >= y + h
          )
            continue;
        }
        const atlas = glyphAtlas?.admit(width, height, q.coverage);
        if (atlas) {
          commands.push({
            texture: atlas.texture,
            rect: q.rect,
            uv: atlas.uv,
            color: q.color,
            alpha: item.alpha,
            clip: item.clip,
          });
          continue;
        }
        const signature = coverageKey(q.coverage, width, height),
          bucket = glyphs.get(signature) || [];
        let name = bucket.find((row) => sameCoverage(row.coverage, q.coverage))?.name;
        if (!name) {
          name = 'glyph:' + glyphCount++;
          bucket.push({ name, coverage: q.coverage });
          glyphs.set(signature, bucket);
          const rgba = new Uint8Array(width * height * 4);
          for (let k = 0; k < q.coverage.length; k++) {
            rgba[k * 4] = rgba[k * 4 + 1] = rgba[k * 4 + 2] = 255;
            rgba[k * 4 + 3] = q.coverage[k];
          }
          admit(name, { width, height, rgba });
        }
        commands.push({
          name,
          rect: q.rect,
          uv: [0, 0, 0, 1, 1, 0, 1, 1],
          color: q.color,
          alpha: item.alpha,
          clip: item.clip,
        });
      }
  }
  for (const q of commands) {
    const [x, y, w, h] = q.rect,
      points = [
        [x, y + h],
        [x, y],
        [x + w, y + h],
        [x + w, y],
      ],
      vertices = [];
    for (const i of [0, 1, 2, 2, 1, 3])
      vertices.push(
        ...points[i],
        q.uv[i * 2],
        q.uv[i * 2 + 1],
        (q.vertexColors?.[i * 4] ?? q.color[0]) / 255,
        (q.vertexColors?.[i * 4 + 1] ?? q.color[1]) / 255,
        (q.vertexColors?.[i * 4 + 2] ?? q.color[2]) / 255,
        ((q.vertexColors?.[i * 4 + 3] ?? q.color[3]) / 255) * q.alpha,
      );
    q.vertices = new Float32Array(vertices);
  }
  let pool,
    p,
    vao,
    buffer,
    cacheTexture,
    cacheFramebuffer,
    cacheWidth = 0,
    cacheHeight = 0;
  const shaders = [];
  let disposed = false,
    cacheDirty = true,
    surfaceAlpha = packet.surfaceAlpha ?? 1;
  const cleanup = () => {
    if (cacheFramebuffer) gl.deleteFramebuffer(cacheFramebuffer);
    if (cacheTexture) gl.deleteTexture(cacheTexture);
    if (buffer) gl.deleteBuffer(buffer);
    if (vao) gl.deleteVertexArray(vao);
    if (p) gl.deleteProgram(p);
    pool?.dispose();
  };
  try {
    pool = textureCache
      ? await textureCache.acquire(textures)
      : await uploadWebGLResources(gl, { textures, buffers: [] });
    const device = reuse?.takeDeviceState();
    if (device) {
      ({ p, vao, buffer, cacheTexture, cacheFramebuffer, cacheWidth, cacheHeight } = device);
    } else {
      p = gl.createProgram();
      if (!p) throw Error('UI program allocation failed');
      for (const [type, source] of [
        [
          gl.VERTEX_SHADER,
          '#version 300 es\nprecision highp float;layout(location=0)in vec2 position;layout(location=1)in vec2 texcoord;layout(location=2)in vec4 color;out vec4 vertexColor;uniform vec2 viewport;out vec2 uv;void main(){vertexColor=color;uv=texcoord;gl_Position=vec4(position/viewport*2.-1.,0.,1.);}',
        ],
        [
          gl.FRAGMENT_SHADER,
          '#version 300 es\nprecision highp float;in vec2 uv;in vec4 vertexColor;uniform sampler2D image;uniform vec4 tint;uniform bool desaturated;out vec4 pixel;void main(){vec4 value=texture(image,uv);if(desaturated)value.rgb=vec3(dot(value.rgb,vec3(.299,.587,.114)));pixel=value*tint*vertexColor;}',
        ],
      ]) {
        const s = gl.createShader(type);
        if (!s) throw Error('UI shader allocation failed');
        shaders.push(s);
        gl.shaderSource(s, source);
        gl.compileShader(s);
        if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) throw Error(gl.getShaderInfoLog(s));
        gl.attachShader(p, s);
      }
      gl.linkProgram(p);
      if (!gl.getProgramParameter(p, gl.LINK_STATUS)) throw Error(gl.getProgramInfoLog(p));
      vao = gl.createVertexArray();
      buffer = gl.createBuffer();
      if (!vao || !buffer) throw Error('UI vertex allocation failed');
      gl.bindVertexArray(vao);
      gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
      gl.bufferData(gl.ARRAY_BUFFER, 192, gl.DYNAMIC_DRAW);
      gl.enableVertexAttribArray(0);
      gl.vertexAttribPointer(0, 2, gl.FLOAT, false, 32, 0);
      gl.enableVertexAttribArray(1);
      gl.vertexAttribPointer(1, 2, gl.FLOAT, false, 32, 8);
      gl.enableVertexAttribArray(2);
      gl.vertexAttribPointer(2, 4, gl.FLOAT, false, 32, 16);
    }
  } catch (error) {
    cleanup();
    throw error;
  } finally {
    for (const s of shaders) gl.deleteShader(s);
  }
  // Merge only adjacent compatible commands: alpha layering remains unchanged.
  const batches = [];
  for (const q of commands) {
    const texture = q.texture || pool.texture(q.name);
    const clipKey = JSON.stringify(q.clip ?? null);
    let batch = batches.at(-1);
    if (
      !batch ||
      batch.texture !== texture ||
      batch.desaturated !== !!q.desaturated ||
      batch.additive !== q.additive ||
      batch.clipKey !== clipKey
    ) {
      batch = {
        texture,
        additive: q.additive,
        desaturated: !!q.desaturated,
        clip: q.clip,
        clipKey,
        values: [],
      };
      batches.push(batch);
    }
    for (const value of q.vertices) batch.values.push(value);
  }
  for (const batch of batches) {
    batch.vertices = new Float32Array(batch.values);
    delete batch.values;
  }
  const viewport = gl.getUniformLocation(p, 'viewport'),
    tint = gl.getUniformLocation(p, 'tint'),
    image = gl.getUniformLocation(p, 'image'),
    desaturated = gl.getUniformLocation(p, 'desaturated');
  const renderCommands = () => {
    if (disposed) throw Error('UI renderer disposed');
    gl.useProgram(p);
    gl.bindVertexArray(vao);
    gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
    gl.activeTexture(gl.TEXTURE0);
    gl.uniform1i(image, 0);
    gl.uniform2f(viewport, logicalWidth, logicalHeight);
    gl.disable(gl.DEPTH_TEST);
    gl.depthMask(false);
    gl.disable(gl.CULL_FACE);
    gl.enable(gl.BLEND);
    gl.blendEquation(gl.FUNC_ADD);
    gl.blendFuncSeparate(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA, gl.ONE, gl.ONE_MINUS_SRC_ALPHA);
    const scissorEnabled = gl.isEnabled(gl.SCISSOR_TEST),
      scissorBox = gl.getParameter(gl.SCISSOR_BOX);
    try {
      for (const q of batches) {
        if (q.additive) gl.blendFuncSeparate(gl.SRC_ALPHA, gl.ONE, gl.ZERO, gl.ONE);
        else
          gl.blendFuncSeparate(
            gl.SRC_ALPHA,
            gl.ONE_MINUS_SRC_ALPHA,
            gl.ONE,
            gl.ONE_MINUS_SRC_ALPHA,
          );
        if (q.clip) {
          const [x, y, w, h] = q.clip,
            sx = gl.drawingBufferWidth / logicalWidth,
            sy = gl.drawingBufferHeight / logicalHeight;
          gl.enable(gl.SCISSOR_TEST);
          const left = Math.ceil(x * sx),
            bottom = Math.ceil(y * sy);
          gl.scissor(
            left,
            bottom,
            Math.max(0, Math.floor((x + w) * sx) - left),
            Math.max(0, Math.floor((y + h) * sy) - bottom),
          );
        } else gl.disable(gl.SCISSOR_TEST);
        gl.bufferData(gl.ARRAY_BUFFER, q.vertices, gl.DYNAMIC_DRAW);
        gl.bindTexture(gl.TEXTURE_2D, q.texture);
        gl.uniform4f(tint, 1, 1, 1, 1);
        gl.uniform1i(desaturated, q.desaturated);
        gl.drawArrays(gl.TRIANGLES, 0, q.vertices.length / 8);
      }
    } finally {
      gl.scissor(...scissorBox);
      if (scissorEnabled) gl.enable(gl.SCISSOR_TEST);
      else gl.disable(gl.SCISSOR_TEST);
    }
    gl.depthMask(true);
    if (gl.getError() !== gl.NO_ERROR) throw Error('UI draw GL error');
    return { drawn_quads: commands.length, gpu_batches: batches.length, issues };
  };
  // Immutable C packet: compose once, then blend a single cached surface while
  // the scene animates. New packets rerender cached content while reusing GPU allocations.
  return {
    issues,
    takeDeviceState() {
      if (disposed) throw Error('UI renderer disposed');
      disposed = true;
      pool.dispose();
      return { p, vao, buffer, cacheTexture, cacheFramebuffer, cacheWidth, cacheHeight };
    },
    commandCount: commands.length,
    setOpacity(value) {
      if (!Number.isFinite(value) || value < 0 || value > 1) throw Error('invalid UI opacity');
      surfaceAlpha = value;
    },
    draw() {
      if (disposed) throw Error('UI renderer disposed');
      const width = gl.drawingBufferWidth,
        height = gl.drawingBufferHeight;
      if (cacheDirty || cacheWidth !== width || cacheHeight !== height) {
        const previousFramebuffer = gl.getParameter(gl.FRAMEBUFFER_BINDING),
          previousViewport = gl.getParameter(gl.VIEWPORT),
          previousClear = gl.getParameter(gl.COLOR_CLEAR_VALUE),
          scissor = gl.isEnabled(gl.SCISSOR_TEST),
          box = gl.getParameter(gl.SCISSOR_BOX);
        try {
          if (!cacheTexture) cacheTexture = gl.createTexture();
          if (!cacheFramebuffer) cacheFramebuffer = gl.createFramebuffer();
          if (!cacheTexture || !cacheFramebuffer) throw Error('UI cache allocation failed');
          gl.activeTexture(gl.TEXTURE0);
          gl.bindTexture(gl.TEXTURE_2D, cacheTexture);
          if (cacheWidth !== width || cacheHeight !== height) {
            gl.texImage2D(
              gl.TEXTURE_2D,
              0,
              gl.RGBA8,
              width,
              height,
              0,
              gl.RGBA,
              gl.UNSIGNED_BYTE,
              null,
            );
          }
          gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
          gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
          gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
          gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
          gl.bindFramebuffer(gl.FRAMEBUFFER, cacheFramebuffer);
          gl.framebufferTexture2D(
            gl.FRAMEBUFFER,
            gl.COLOR_ATTACHMENT0,
            gl.TEXTURE_2D,
            cacheTexture,
            0,
          );
          if (gl.checkFramebufferStatus(gl.FRAMEBUFFER) !== gl.FRAMEBUFFER_COMPLETE)
            throw Error('UI cache framebuffer incomplete');
          gl.viewport(0, 0, width, height);
          gl.disable(gl.SCISSOR_TEST);
          gl.colorMask(true, true, true, true);
          gl.clearColor(0, 0, 0, 0);
          gl.clear(gl.COLOR_BUFFER_BIT);
          renderCommands();
          cacheWidth = width;
          cacheHeight = height;
          cacheDirty = false;
        } finally {
          gl.bindFramebuffer(gl.FRAMEBUFFER, previousFramebuffer);
          gl.viewport(...previousViewport);
          gl.clearColor(...previousClear);
          gl.scissor(...box);
          if (scissor) gl.enable(gl.SCISSOR_TEST);
          else gl.disable(gl.SCISSOR_TEST);
        }
      }
      gl.useProgram(p);
      gl.bindVertexArray(vao);
      gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
      gl.activeTexture(gl.TEXTURE0);
      gl.bindTexture(gl.TEXTURE_2D, cacheTexture);
      gl.uniform1i(image, 0);
      gl.uniform2f(viewport, logicalWidth, logicalHeight);
      gl.uniform4f(tint, surfaceAlpha, surfaceAlpha, surfaceAlpha, surfaceAlpha);
      gl.uniform1i(desaturated, false);
      gl.disable(gl.DEPTH_TEST);
      gl.disable(gl.CULL_FACE);
      gl.depthMask(false);
      gl.enable(gl.BLEND);
      gl.blendEquation(gl.FUNC_ADD);
      gl.blendFuncSeparate(gl.ONE, gl.ONE_MINUS_SRC_ALPHA, gl.ONE, gl.ONE_MINUS_SRC_ALPHA);
      const scissor = gl.isEnabled(gl.SCISSOR_TEST);
      gl.disable(gl.SCISSOR_TEST);
      try {
        const composite = [];
        for (const vertex of [
          [0, logicalHeight, 0, 1],
          [0, 0, 0, 0],
          [logicalWidth, logicalHeight, 1, 1],
          [logicalWidth, logicalHeight, 1, 1],
          [0, 0, 0, 0],
          [logicalWidth, 0, 1, 0],
        ])
          composite.push(...vertex, 1, 1, 1, 1);
        gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(composite), gl.DYNAMIC_DRAW);
        gl.drawArrays(gl.TRIANGLES, 0, 6);
      } finally {
        if (scissor) gl.enable(gl.SCISSOR_TEST);
        gl.depthMask(true);
      }
      if (gl.getError() !== gl.NO_ERROR) throw Error('UI cache draw GL error');
      return {
        drawn_quads: commands.length,
        gpu_batches: batches.length,
        issues,
        cached_surface: true,
      };
    },
    dispose() {
      if (disposed) return;
      disposed = true;
      cleanup();
    },
  };
}
