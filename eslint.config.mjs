import js from '@eslint/js';
import globals from 'globals';

export default [
  {
    ignores: [
      '**/build/**',
      '**/cache/**',
      '**/node_modules/**',
      'engine/lua-runtime/tools/**',
      '.local/**',
      'validation/**',
      'research-tools/**',
      'reverse-engineering-notes/**',
      'Data/**',
      'Interface/**',
      'WTF/**',
      'azerothcore-wotlk/**',
    ],
  },
  {
    files: ['**/*.js', '**/*.mjs'],
    ...js.configs.recommended,
    languageOptions: {
      ecmaVersion: 'latest',
      sourceType: 'module',
      globals: { ...globals.browser, ...globals.node, ...globals.worker },
    },
    rules: {
      // Platform boundaries retain explicit bit patterns and callback arguments.
      'no-unused-vars': ['warn', { args: 'none', caughtErrors: 'none', varsIgnorePattern: '^_' }],
      'no-empty': ['error', { allowEmptyCatch: true }],
      'no-constant-condition': ['error', { checkLoops: false }],
      'no-bitwise': 'off',
    },
  },
];
