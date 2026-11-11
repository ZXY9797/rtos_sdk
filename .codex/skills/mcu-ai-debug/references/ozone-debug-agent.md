# Ozone Advanced Debug Agent

Use this reference when Codex must directly operate a hidden Ozone session for memory,
breakpoints, watchpoints, registers, stopped-target context, snapshots, or disassembly.
Use `ozone_live_attach.py` instead when preserving a running现场 is the only requirement.

## Contents

- Architecture
- Capability Modes
- Configuration
- Start And Connect
- Read And Disassemble
- Checked Memory And Register Writes
- Breakpoints And Watchpoints
- Execution And Context
- Shutdown And Evidence
- Multiple J-Links
- Performance Rules

## Architecture

```text
Codex server
  -> controlled SSH/PC broker stdin+stdout
  -> ozone_debug_agent.py (typed JSONL, policy checks, audit)
  -> hidden persistent Ozone
  -> localhost Automation Socket
  -> exact J-Link serial
  -> MCU
```

Keep the agent and Ozone in the Windows interactive user session that owns USB. Never
publish Ozone's Automation Socket outside localhost. The agent is the sole J-Link owner.

## Capability Modes

| Mode | Operations | Effect |
|---|---|---|
| `observe` | block reads, status, target bytes, ELF disassembly | no intentional halt/write |
| `live_modify` | checked allowlisted scalar RAM writes | changes live target state |
| `stopped_debug` | halt/continue/step, code/data breakpoints, registers, context, snapshot | changes timing or execution state |

The agent intentionally has no raw console, reset, download, flash erase, arbitrary
expression, or `Target.LoadMemory` operation. Use a separate explicitly authorized
programming workflow for those actions.

Enabling a mode in the PC-owned configuration defines the maximum capability. It does not
replace per-request user authorization. Every disturbing request also requires a non-empty
reason and this exact acknowledgement:

```text
DISTURB:<J-Link-serial>:<operation>
```

Codex must not synthesize that acknowledgement without explicit authorization for the named
operation and its effect on the named target. Existing authorization remains valid within
its accepted limits; a per-request acknowledgement is not a requirement to ask the user again.
For active PWM, charging, motor, or BMS power stages, require an externally safe state before
enabling `stopped_debug`. This fresh-session agent cannot adopt an already connected Ozone
owner; do not terminate that owner to make room without an authorized handoff.

## Configuration

```json
{
  "schema_version": 1,
  "ozone_exe": "C:/Program Files/SEGGER/Ozone/Ozone.exe",
  "jlink_commander": "C:/Program Files/SEGGER/JLink/JLink.exe",
  "device": "STM32F407VG",
  "probe_serial": "123456789",
  "target_interface": "SWD",
  "speed": "4 MHz",
  "elf": "D:/firmware/build/firmware.elf",
  "objdump_tool": "C:/ArmGNU/bin/arm-none-eabi-objdump.exe",
  "automation_port": 19200,
  "control_token": "replace-with-a-random-secret-at-least-16-characters",
  "timeout_s": 20,
  "output_dir": "D:/mcu-debug-sessions",
  "max_block_bytes": 1048576,
  "enabled_modes": ["observe", "live_modify", "stopped_debug"],
  "allowed_read_ranges": [
    {"name": "sram", "start": "0x20000000", "end": "0x20020000"}
  ],
  "allowed_write_ranges": [
    {"name": "debug_controls", "start": "0x20001000", "end": "0x20001040"}
  ],
  "allowed_code_ranges": [
    {"name": "app_flash", "start": "0x08000000", "end": "0x08100000"}
  ],
  "allowed_registers": [
    "R0", "R1", "R2", "R3", "R4", "R5", "R6", "R7",
    "R8", "R9", "R10", "R11", "R12", "SP", "LR", "PC", "xPSR",
    "MSP", "PSP", "PRIMASK", "BASEPRI", "FAULTMASK", "CONTROL"
  ],
  "allowed_source_files": ["bms_control.c", "fault_handlers.c"],
  "allowed_source_functions": ["HardFault_Handler"],
  "allow_register_write": false,
  "allow_software_breakpoints": false,
  "fault_registers": [
    {"name": "CFSR", "address": "0xE000ED28", "width": 32},
    {"name": "HFSR", "address": "0xE000ED2C", "width": 32}
  ]
}
```

Treat `end` as exclusive. Derive every range and fault-register address from the linker
files, MCU headers, and reference manual; do not copy the example addresses blindly.
Keep write ranges much narrower than read ranges. Leave software breakpoints and register
writes disabled unless the user specifically needs them.

## Start And Connect

Validate offline first:

```powershell
python scripts/ozone_debug_agent.py --config debug_agent.json --dry-run
python scripts/ozone_debug_agent.py --self-test
```

After reviewing the exact probe serial, device, ELF hash, modes, and ranges, start one
persistent JSONL session:

```powershell
python scripts/ozone_debug_agent.py --config debug_agent.json --serve-stdio
```

The first line is `session.ready`. Send one JSON object per line and read one response per
line. Keep this process alive across operations for native-like performance. A controlled
SSH or PC broker may carry the same stdin/stdout stream; do not proxy port 19200.

For repeated calls from a server-side Codex, keep the agent listening on a separate local
control port:

```powershell
python scripts/ozone_debug_agent.py --config debug_agent.json --serve-tcp 19300
```

Both the Ozone Automation port and agent control port bind to `127.0.0.1`. Submit a request
from an SSH command or PC broker running on that same PC:

```powershell
Get-Content request.json | python scripts/ozone_debug_agent.py `
  --config debug_agent.json --send-stdin --control-port 19300
```

The lightweight client reads `control_token` from the PC-local configuration and adds it to
the local request. Do not place the token in prompts, logs, source control, or command-line
arguments. Rotate it if it may have been exposed.

For a single diagnostic request, use:

```powershell
python scripts/ozone_debug_agent.py --config debug_agent.json --request request.json
```

This cold-starts Ozone and is slower than `--serve-stdio`.

## Read And Disassemble

Read a contiguous block using one native `Target.SaveMemory` operation:

```json
{"id":"r1","operation":"memory.read","address":"0x20001000","size":256}
```

Return blocks up to 4096 bytes inline as hex and always preserve a binary artifact with
SHA-256. Larger blocks are artifact-only.

Disassemble the ELF without changing target execution:

```json
{"id":"d1","operation":"disassemble.elf","start":"0x08001000","stop":"0x08001100"}
```

Read target instruction bytes and provide matching ELF disassembly:

```json
{"id":"d2","operation":"disassemble.target","address":"0x08001000","size":256}
```

Static ELF disassembly does not reveal the running PC. `context.capture` requires an
already halted target.

## Checked Memory And Register Writes

Live RAM writes use compare-before-write and readback verification:

```json
{
  "id":"w1",
  "operation":"memory.write_checked",
  "address":"0x20001004",
  "width":32,
  "expected_old":"0x00000002",
  "value":"0x00000003",
  "reason":"Temporarily select diagnostic state approved by user",
  "acknowledge":"DISTURB:123456789:memory.write_checked"
}
```

The compare is not an atomic transaction with running firmware. For racing variables,
prefer a firmware diagnostic mailbox or halt the target after the hardware is safe.

Register writes additionally require `allow_register_write: true`, an allowlisted register,
an already halted target, compare-before-write, and acknowledgement.

## Breakpoints And Watchpoints

Prefer hardware instruction breakpoints:

```json
{
  "operation":"breakpoint.add_instruction",
  "address":"0x08001234",
  "implementation":"hardware",
  "reason":"Capture the approved fault path",
  "acknowledge":"DISTURB:123456789:breakpoint.add_instruction"
}
```

Source breakpoint:

```json
{
  "operation":"breakpoint.add_source",
  "location":"bms_control.c:317",
  "implementation":"hardware",
  "reason":"Stop at approved source line",
  "acknowledge":"DISTURB:123456789:breakpoint.add_source"
}
```

The source file suffix or function must also appear in `allowed_source_files` or
`allowed_source_functions`. Use instruction-address breakpoints when an exact code-range
policy is required. Version 1 requires a zero data-watchpoint address mask so the monitored
address cannot expand beyond the allowlisted object.

Data watchpoint:

```json
{
  "operation":"breakpoint.add_data",
  "address":"0x20001020",
  "size":4,
  "access":"write",
  "reason":"Find the writer of the corrupted state word",
  "acknowledge":"DISTURB:123456789:breakpoint.add_data"
}
```

Data watchpoints halt when triggered and consume limited MCU debug resources. Conditional
breakpoint editing is intentionally not exposed in version 1 because expression evaluation
and repeated halt/resume can heavily disturb real-time behavior. Use software breakpoints
only after enabling them and accepting that Ozone/J-Link may modify executable storage.

Clear all session breakpoints before handing the target back:

```json
{
  "operation":"breakpoint.clear_all",
  "reason":"Remove all agent-owned breakpoints",
  "acknowledge":"DISTURB:123456789:breakpoint.clear_all"
}
```

## Execution And Context

Halt only after explicit authorization and hardware-safe confirmation:

```json
{
  "operation":"execution.halt",
  "reason":"Capture approved stopped fault context",
  "acknowledge":"DISTURB:123456789:execution.halt"
}
```

Once halted, capture registers, stack bytes, target code bytes, and ELF disassembly locally:

```json
{
  "operation":"context.capture",
  "stack_bytes":512,
  "code_before":64,
  "code_after":128
}
```

Read configured fault registers without inventing bit interpretations:

```json
{"operation":"fault.capture"}
```

Save an Ozone snapshot only when already halted:

```json
{"operation":"snapshot.save","name":"fault_17.jsnap"}
```

Continue and step operations each require their own explicit acknowledgement. The agent
never automatically resumes on disconnect or timeout; automatic resume could be more
dangerous than remaining halted. Decide the final target state explicitly before closing.

## Shutdown And Evidence

Query status, remove breakpoints, choose whether to continue, then close:

```json
{"operation":"session.status"}
{"operation":"session.close"}
```

Preserve the generated `.jdebug`, ELF hash, Ozone/J-Link logs, binary captures, snapshots,
and `audit.jsonl`. If startup logs show reset, download, flash activity, SYSRESETREQ, or work
RAM initialization, the agent rejects the session. If the target state is unexpected, stop
and report it; never issue a speculative continue/reset.

## Multiple J-Links

Select probes only by exact non-zero serial. Version 1 permits only one local hidden Ozone
agent in a Windows user session because it refuses competing J-Link/Ozone processes. For
simultaneous probes, use isolated interactive Windows sessions/VMs/PC agents with separate
Automation ports verified against the installed Ozone version. Do not rely on USB order.

## Performance Rules

1. Keep `--serve-stdio` alive; do not cold-start per request.
2. Read contiguous blocks, not scalar loops.
3. Execute stopped-context capture on the PC before returning data to the server.
4. Cache ELF/DWARF/objdump output by ELF SHA-256.
5. Return large data as local artifacts plus hashes.
6. Measure `duration_ms` from each response and compare against manual Ozone on the same
   probe, target, SWD speed, and capture size.
