---
name: Bug report
about: Report a reproducible brWoWser problem with clear steps and evidence.
title: '[Bug]: '
labels: ''
assignees: ''
---

## Summary

Describe one problem and where it happens (startup, login, character selection,
creation, introduction, or world).

## Environment

- brWoWser version / commit:
- Operating system:
- Browser and version:
- GPU (for rendering issues):
- Node.js version:
- Client build and locale (for example, 12340 / enUS):
- Server implementation and version (if relevant):
- Startup command:
- Fresh configuration or existing saved settings:

## Preconditions

Describe the starting state: current screen, existing/new character, race, class,
gender, and any previous actions or open dialogs needed to reproduce the problem.

## Steps to reproduce

1. Start with ...
2. Click ...
3. Type ...
4. Press ...
5. Observe ...

Include exact button labels, keys, and wait times. For confirmation dialogs,
state whether you clicked the button or pressed Enter, and whether it was enabled.

## Expected behavior

Describe what should happen. If comparing with the original client, describe the
same action there and include a labeled comparison image when useful.

## Actual behavior

Describe what happened, how long it took, and whether the UI remained responsive.
Include frequency (every time / intermittent) and whether reconnecting or
restarting changes the result.

## Evidence

Paste the complete error and relevant nearby browser/terminal lines as text.
For a connection failure, include the full `Connection detail:` line.
For a visual issue, attach labeled screenshots of brWoWser and the original client
with the same character, scene, and viewport when possible.

```text
Paste relevant logs here.
```

## Additional context

- Last known working commit, if known:
- Workaround, if any:
- Other relevant details:

Do not attach passwords, account tokens, session keys, private server addresses,
client assets, executables, MPQs, or database dumps. Redact sensitive values
without removing error names, phases, opcodes, or timing information.
