# MCU AI debug reference entry

The maintained operating workflow is [SKILL.md](../SKILL.md). Select a mode from the user's
task and existing authorization; discover paths and configuration before asking for missing
information. This compatibility entry replaces the older monolithic command collection.

Read only the reference relevant to the operation:

- [jlink-gdb-transport.md](jlink-gdb-transport.md): Windows/WSL endpoints, GDB/MI2 lifecycle,
  RTT and serial capture.
- [firmware-evidence.md](firmware-evidence.md): ELF/image identity, load ranges, readback,
  bounded programming recovery and Cortex-M fault analysis.
- [ozone-live-attach.md](ozone-live-attach.md): fresh hidden Ozone session for allowlisted
  RAM observation, only when no existing probe owner must be preserved.
- [ozone-debug-agent.md](ozone-debug-agent.md): typed, policy-gated advanced Ozone operations.

Do not run reset/download/interrupt as part of a generic “minimal connection” script.
Do not replace an existing live Ozone owner, kill a server on timeout, or infer run state
from a disconnected socket. Use the entrypoint's authorization and evidence rules.
