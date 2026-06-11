---
name: mcu-ai-debug
description: MCU firmware debugging workflow for J-Link verification, JLinkGDBServer, arm-none-eabi-gdb/GDB-MI2, RTT logs, HardFault, assert, reset-loop, peripheral-init, and automated PASS/FAIL validation tasks. Use when debugging embedded MCU firmware, choosing between verification and debug mode, reading RTT, collecting registers/backtraces, or coordinating flash/reset permissions.
---

# MCU AI Debug

Before starting any MCU debug or validation workflow, read
`references/mcu_ai_debug_prompt.md` completely and follow it as the source of
truth.

Core rules:

- Ask whether the user wants verification mode or debug mode unless they
  already made that explicit.
- Do not guess tool paths, MCU device names, ELF/BIN paths, source roots, or
  build commands.
- Do not flash or reset the MCU without explicit authorization.
- In verification mode, use JLink.exe/JLinkExe for scripted run/halt/probe
  checks and do not claim RTT is available through that mode.
- In debug mode, use JLinkGDBServer plus arm-none-eabi-gdb/GDB-MI2 and RTT
  evidence where available.
- Base conclusions on concrete evidence: logs, GDB/MI results, registers,
  backtraces, memory reads, symbols, or external probes.
- Do not invent chip register meanings; require a reference manual, header, or
  user-provided register definition when needed.
