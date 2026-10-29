export function chooseStartupConfig({ saved, imported, fresh = false }) {
  if (fresh) return 'SET gxResolution "1024x768"\nSET movie "1"\nSET expansionMovie "1"\n';
  if (typeof saved === 'string') return saved;
  if (typeof imported === 'string') return imported;
  return 'SET gxResolution "1024x768"\n';
}
