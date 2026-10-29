// Source file stays read-only. C/WASM owns parsing and registered-CVar updates.
export function applyConfigBytes(e, bytes) {
  if (!(bytes instanceof Uint8Array) || bytes.length > 1048576) throw Error('Config.wtf capacity');
  new Uint8Array(e.memory.buffer, e.frame_input_ptr(), bytes.length).set(bytes);
  const status = e.glue_config_load(bytes.length),
    report = {
      entries: e.glue_config_metric(0),
      error_line: e.glue_config_metric(1),
      applied: e.glue_config_metric(2),
      unknown: e.glue_config_metric(3),
    };
  if (status) throw Error('Config.wtf rejected at line ' + report.error_line);
  return report;
}
export async function readConfigFile(e, file) {
  if (!file || file.size > 1048576) throw Error('Config.wtf capacity');
  return applyConfigBytes(e, new Uint8Array(await file.arrayBuffer()));
}

export function serializeConfig(e) {
  if (e.glue_config_save()) throw Error('CVar snapshot rejected');
  return new TextDecoder().decode(
    new Uint8Array(e.memory.buffer, e.frame_input_ptr(), e.glue_config_length()),
  );
}
// Browser-owned snapshot; never overwrites the selected source file.
export async function configStore(operation, value) {
  return new Promise((resolve, reject) => {
    const request = indexedDB.open('brwowser-config', 1);
    request.onupgradeneeded = () => request.result.createObjectStore('settings');
    request.onerror = () => reject(request.error);
    request.onsuccess = () => {
      const db = request.result,
        transaction = db.transaction('settings', operation === 'read' ? 'readonly' : 'readwrite'),
        store = transaction.objectStore('settings'),
        query =
          operation === 'read'
            ? store.get('registered-cvars')
            : operation === 'clear'
              ? store.delete('registered-cvars')
              : store.put(value, 'registered-cvars');
      let result;
      query.onsuccess = () => {
        result = query.result;
      };
      transaction.oncomplete = () => {
        db.close();
        resolve(result);
      };
      transaction.onerror = () => {
        db.close();
        reject(transaction.error);
      };
    };
  });
}
