// Filesystem boundary only. C parses and composes the selected local documents.
// The directory inventory already rejects duplicate/case-ambiguous paths.
export async function readLegalFiles(
  inventory,
  locale,
  names = ['eula.html', 'tos.html', 'termination.html'],
) {
  if (!/^[a-z]{2}[A-Z]{2}$/.test(locale)) throw Error('Invalid legal document locale');
  const result = [];
  for (const name of names) {
    const file = inventory.data.get((locale + '/' + name).toLowerCase())?.file;
    if (!file || file.size < 1 || file.size > 1048576)
      throw Error('Missing or invalid local legal document: ' + name);
    const bytes = await file.arrayBuffer();
    if (bytes.byteLength !== file.size) throw Error('Legal document read size changed');
    result.push({ name, bytes });
  }
  return result;
}
