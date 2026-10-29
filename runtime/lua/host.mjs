// Thin VM memory/exports adapter. Game logic stays in compiled Lua/C.
export function luaHost(instance) {
  const e = instance.exports,
    encode = new TextEncoder(),
    decode = new TextDecoder();
  const text = (pointer) => {
    if (!pointer) return '';
    const bytes = new Uint8Array(e.memory.buffer, pointer);
    const end = bytes.indexOf(0);
    if (end < 0) throw Error('unterminated VM text');
    return decode.decode(bytes.subarray(0, end));
  };
  const input = (value) => {
    const bytes = typeof value === 'string' ? encode.encode(value) : value;
    if (bytes.length > e.bw_lua_input_capacity()) throw Error('Lua chunk capacity');
    new Uint8Array(e.memory.buffer, e.bw_lua_input_ptr(), bytes.length).set(bytes);
    return bytes.length;
  };
  const value = (type) =>
    type === 5
      ? text(e.bw_lua_text_ptr())
      : type === 4
        ? e.bw_lua_number()
        : type === 2
          ? !!e.bw_lua_number()
          : type === 1
            ? null
            : { lua_type: type - 1 };
  return {
    result: (index = 1) => value(e.bw_lua_result(index)),
    resultCount: () => e.bw_lua_result_count(),
    init() {
      if (e.bw_lua_init()) throw Error('Lua initialization failed');
    },
    run(source, budget = 1000000) {
      const status = e.bw_lua_run(input(source), budget);
      return { status, error: status ? text(e.bw_lua_error_ptr()) : null };
    },
    get(name) {
      const type = e.bw_lua_global(input(name));
      if (type === 5) return text(e.bw_lua_text_ptr());
      if (type === 4) return e.bw_lua_number();
      if (type === 2) return !!e.bw_lua_number();
      if (type === 1) return null;
      return { lua_type: type - 1 };
    },
    heapBytes: () => e.bw_lua_heap_bytes(),
    close: () => e.bw_lua_close(),
  };
}
