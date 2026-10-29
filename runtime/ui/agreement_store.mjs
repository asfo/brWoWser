// Only browser-recorded, explicit button acceptance is restored. Imported WTF
// strings are deliberately not interpreted as consent. Bind it to document bytes.
export async function agreementIdentity(assets) {
  const rows = [];
  for (const asset of assets) {
    const hash = Array.from(
      new Uint8Array(await crypto.subtle.digest('SHA-256', asset.bytes)),
      (x) => x.toString(16).padStart(2, '0'),
    ).join('');
    rows.push([asset.name, hash]);
  }
  return JSON.stringify(rows.sort((a, b) => a[0].localeCompare(b[0])));
}
export function validateAgreementRecord(record, identity) {
  if (!record) return 0;
  if (record.version !== 1 || record.identity !== identity) return 0;
  if (
    !Number.isInteger(record.accepted_mask) ||
    record.accepted_mask < 0 ||
    record.accepted_mask > 31
  )
    throw Error('Invalid stored agreement mask');
  return record.accepted_mask;
}
export function agreementStore(operation, value) {
  if (!['read', 'write', 'clear'].includes(operation))
    return Promise.reject(Error('Invalid agreement storage operation'));
  return new Promise((resolve, reject) => {
    const request = indexedDB.open('brwowser-agreements', 1);
    request.onupgradeneeded = () => request.result.createObjectStore('consent');
    request.onerror = () => reject(request.error);
    request.onsuccess = () => {
      const db = request.result,
        tx = db.transaction('consent', operation === 'read' ? 'readonly' : 'readwrite'),
        store = tx.objectStore('consent'),
        query =
          operation === 'read'
            ? store.get('explicit-local-acceptance')
            : operation === 'clear'
              ? store.delete('explicit-local-acceptance')
              : store.put(value, 'explicit-local-acceptance');
      let result;
      query.onsuccess = () => {
        result = query.result;
      };
      tx.oncomplete = () => {
        db.close();
        resolve(result);
      };
      tx.onerror = tx.onabort = () => {
        db.close();
        reject(tx.error || Error('Agreement storage aborted'));
      };
    };
  });
}
