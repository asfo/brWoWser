// Session-owned coverage atlas. Exact byte comparison keeps hash collisions harmless.
export function createGlyphAtlas(gl, size = 1024, maxPages = 4) {
  const pages = [],
    entries = new Map();
  return {
    admit(width, height, coverage) {
      if (width + 2 > size || height + 2 > size) return null;
      let hash = 2166136261;
      for (const byte of coverage) hash = Math.imul(hash ^ byte, 16777619);
      const key = `${width}:${height}:${hash >>> 0}`;
      const bucket = entries.get(key) || [];
      const existing = bucket.find((row) => row.coverage.every((byte, i) => byte === coverage[i]));
      if (existing) return existing;
      let page = pages.at(-1);
      if (page && page.x + width + 1 > size) {
        page.x = 1;
        page.y += page.row;
        page.row = 0;
      }
      if (!page || page.y + height + 1 > size) {
        if (pages.length >= maxPages) return null;
        const texture = gl.createTexture();
        if (!texture) throw Error('Glyph atlas allocation failed');
        gl.bindTexture(gl.TEXTURE_2D, texture);
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, size, size, 0, gl.RGBA, gl.UNSIGNED_BYTE, null);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
        page = { texture, x: 1, y: 1, row: 0 };
        pages.push(page);
      }
      const { x, y, texture } = page;
      const rgba = new Uint8Array(width * height * 4);
      for (let i = 0; i < coverage.length; i++) {
        rgba.fill(255, i * 4, i * 4 + 3);
        rgba[i * 4 + 3] = coverage[i];
      }
      gl.bindTexture(gl.TEXTURE_2D, texture);
      const alignment = gl.getParameter(gl.UNPACK_ALIGNMENT);
      const flip = gl.getParameter(gl.UNPACK_FLIP_Y_WEBGL);
      const premultiply = gl.getParameter(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL);
      try {
        gl.pixelStorei(gl.UNPACK_ALIGNMENT, 1);
        gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, false);
        gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, false);
        gl.texSubImage2D(gl.TEXTURE_2D, 0, x, y, width, height, gl.RGBA, gl.UNSIGNED_BYTE, rgba);
      } finally {
        gl.pixelStorei(gl.UNPACK_ALIGNMENT, alignment);
        gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, flip);
        gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, premultiply);
      }

      const row = {
        texture,
        coverage: coverage.slice(),
        uv: [
          x / size,
          y / size,
          x / size,
          (y + height) / size,
          (x + width) / size,
          y / size,
          (x + width) / size,
          (y + height) / size,
        ],
      };
      bucket.push(row);
      entries.set(key, bucket);
      page.x += width + 2;
      page.row = Math.max(page.row, height + 2);
      return row;
    },
    dispose() {
      for (const page of pages) gl.deleteTexture(page.texture);
      pages.length = 0;
      entries.clear();
    },
  };
}
