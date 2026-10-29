// Browser graphics boundary. Owns uploaded resources, not scene draw policy or
// reconstructed D3D state/shaders. Asset identity must be checked before entry.
export async function uploadWebGLResources(
  gl,
  { textures, buffers },
  { budgetBytes = 134217728, timeoutMs = 10000 } = {},
) {
  if (
    !gl ||
    gl.isContextLost() ||
    !Array.isArray(textures) ||
    !Array.isArray(buffers) ||
    !Number.isSafeInteger(budgetBytes) ||
    budgetBytes < 1 ||
    !Number.isFinite(timeoutMs) ||
    timeoutMs < 1 ||
    timeoutMs > 30000
  )
    throw Error('invalid GPU resource request');
  const names = new Set();
  let bytes = 0;
  for (const row of textures) {
    if (
      typeof row.name !== 'string' ||
      !row.name ||
      names.has(row.name) ||
      !Number.isInteger(row.width) ||
      !Number.isInteger(row.height) ||
      row.width < 1 ||
      row.height < 1 ||
      row.width > gl.getParameter(gl.MAX_TEXTURE_SIZE) ||
      row.height > gl.getParameter(gl.MAX_TEXTURE_SIZE) ||
      !(row.rgba instanceof Uint8Array) ||
      row.rgba.byteLength !== row.width * row.height * 4
    )
      throw Error('invalid GPU texture');
    names.add(row.name);
    bytes += row.rgba.byteLength;
  }
  for (const row of buffers) {
    if (
      typeof row.name !== 'string' ||
      !row.name ||
      names.has(row.name) ||
      !(row.bytes instanceof Uint8Array) ||
      !row.bytes.byteLength ||
      !['vertex', 'index'].includes(row.role)
    )
      throw Error('invalid GPU buffer');
    names.add(row.name);
    bytes += row.bytes.byteLength;
  }
  if (bytes > budgetBytes) throw Error('GPU resource budget exceeded');
  if (gl.getError() !== gl.NO_ERROR) throw Error('preexisting GPU error');
  const textureObjects = new Map(),
    bufferObjects = new Map(),
    bufferRoles = new Map();
  let sync,
    disposed = false,
    restored = false;
  const previousTexture = gl.getParameter(gl.TEXTURE_BINDING_2D),
    previousVertexBuffer = gl.getParameter(gl.ARRAY_BUFFER_BINDING),
    previousIndexBuffer = gl.getParameter(gl.ELEMENT_ARRAY_BUFFER_BINDING),
    alignment = gl.getParameter(gl.UNPACK_ALIGNMENT),
    flip = gl.getParameter(gl.UNPACK_FLIP_Y_WEBGL),
    premultiply = gl.getParameter(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL),
    colorspace = gl.getParameter(gl.UNPACK_COLORSPACE_CONVERSION_WEBGL);
  // Upload is synchronous until bindings/pixel-store are restored. Do not keep
  // caller VAO/array/texture state changed while awaiting the fence.
  const restore = () => {
    if (restored) return;
    gl.bindTexture(gl.TEXTURE_2D, previousTexture);
    gl.bindBuffer(gl.ARRAY_BUFFER, previousVertexBuffer);
    gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, previousIndexBuffer);
    gl.pixelStorei(gl.UNPACK_ALIGNMENT, alignment);
    gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, flip);
    gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, premultiply);
    gl.pixelStorei(gl.UNPACK_COLORSPACE_CONVERSION_WEBGL, colorspace);
    restored = true;
  };
  const cleanup = () => {
    if (sync) {
      gl.deleteSync(sync);
      sync = null;
    }
    for (const object of textureObjects.values()) gl.deleteTexture(object);
    for (const object of bufferObjects.values()) gl.deleteBuffer(object);
    textureObjects.clear();
    bufferObjects.clear();
    bufferRoles.clear();
    disposed = true;
  };
  try {
    gl.pixelStorei(gl.UNPACK_ALIGNMENT, 1);
    gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, false);
    gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, false);
    gl.pixelStorei(gl.UNPACK_COLORSPACE_CONVERSION_WEBGL, gl.NONE);
    for (const row of textures) {
      const object = gl.createTexture();
      if (!object) throw Error('GPU texture allocation failed');
      textureObjects.set(row.name, object);
      gl.bindTexture(gl.TEXTURE_2D, object);
      gl.texImage2D(
        gl.TEXTURE_2D,
        0,
        gl.RGBA8,
        row.width,
        row.height,
        0,
        gl.RGBA,
        gl.UNSIGNED_BYTE,
        row.rgba,
      );
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.REPEAT);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.REPEAT);
      if (gl.getError() !== gl.NO_ERROR) throw Error('GPU texture upload failed');
    }
    for (const row of buffers) {
      const object = gl.createBuffer();
      if (!object) throw Error('GPU buffer allocation failed');
      bufferObjects.set(row.name, object);
      bufferRoles.set(row.name, row.role);
      const target = row.role === 'index' ? gl.ELEMENT_ARRAY_BUFFER : gl.ARRAY_BUFFER;
      gl.bindBuffer(target, object);
      gl.bufferData(target, row.bytes, gl.STATIC_DRAW);
      if (gl.getError() !== gl.NO_ERROR) throw Error('GPU buffer upload failed');
    }
    restore();
    sync = gl.fenceSync(gl.SYNC_GPU_COMMANDS_COMPLETE, 0);
    if (!sync) throw Error('GPU fence allocation failed');
    gl.flush();
    const deadline = performance.now() + timeoutMs;
    for (;;) {
      if (gl.isContextLost()) throw Error('GPU context lost');
      const status = gl.clientWaitSync(sync, 0, 0);
      if (status === gl.ALREADY_SIGNALED || status === gl.CONDITION_SATISFIED) break;
      if (status !== gl.TIMEOUT_EXPIRED) throw Error('GPU completion failed');
      if (performance.now() >= deadline) throw Error('GPU completion timeout');
      await new Promise((resolve) => setTimeout(resolve, 1));
    }
    gl.deleteSync(sync);
    sync = null;
    return {
      bytes,
      textureCount: textureObjects.size,
      bufferCount: bufferObjects.size,
      texture(name) {
        if (disposed) throw Error('disposed GPU resources');
        return textureObjects.get(name);
      },
      buffer(name, role) {
        if (disposed) throw Error('disposed GPU resources');
        if (role !== undefined && bufferRoles.get(name) !== role)
          throw Error('GPU buffer role mismatch: ' + name);
        return bufferObjects.get(name);
      },
      dispose: cleanup,
    };
  } catch (error) {
    cleanup();
    throw error;
  } finally {
    restore();
  }
}
