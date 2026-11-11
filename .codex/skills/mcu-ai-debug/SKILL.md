---
name: mcu-ai-debug
description: Debug MCU firmware through J-Link, GDB/MI2, RTT, serial or hidden Ozone; preserve live targets, verify programmed images, and correlate faults with ELF/source evidence. Use for board bring-up, HardFault/assert/reset-loop analysis, Windows/WSL debug connections, or authorized memory and execution control, especially real-time power/PWM/motor/BMS systems.
---

# MCU AI Debug

Use this skill to debug MCU firmware with J-Link while preserving evidence and avoiding accidental target disturbance. Prefer evidence over guesses, and treat live control loops as fragile.

For KP protocol requests, ACKs, subscriptions or USB/UART/CAN telemetry, automatically use
the global `kptools-debug` skill and `kptools` MCP. Reuse its existing device owner; do not
open a competing serial/protocol client. Pure ELF/RAM analysis does not require KPtools.

Use repository debug notes for current device/build/protocol facts; reconcile old command
examples with these operating rules before execution. Run bundled `scripts/` relative to
this skill directory or by absolute path, not relative to the firmware checkout.

Read only the needed reference:

- [jlink-gdb-transport.md](references/jlink-gdb-transport.md): actual Windows/WSL endpoints,
  server capabilities, GDB lifecycle, serial startup capture and RTT.
- [firmware-evidence.md](references/firmware-evidence.md): build/ELF selection, package layout,
  Flash readback, bounded recovery and fault interpretation.
- Ozone controller references below: read the selected one completely before using its script.

## First Decision

Before choosing tools, classify the task:

1. **Verification mode**: user wants PASS/FAIL, smoke test, regression, or a fixed probe value. Use `JLink.exe` only when flashing/running/halt/readback is acceptable and the firmware exposes structured results through globals, fixed memory, serial, GPIO, or instruments.
2. **Debug mode**: user needs call stacks, registers, breakpoints, HardFault/assert analysis, variables, watchpoints, or RTT logs. Reuse the existing authorized debugger owner; for a new GDB session use `JLinkGDBServer` plus an installed ARM-capable GDB in MI2 mode (`arm-none-eabi-gdb`, or `gdb-multiarch` when appropriate), optionally with RTT Telnet.
3. **Live real-time现场**: power electronics, PWM, motor, charging, BMS current loop, communication timing, or the user says not to reset, flash, halt, or destroy the现场. Default to non-invasive observation. Prefer the hidden Ozone `CM_ATTACH` controller below when the Windows PC locally owns the USB J-Link. Do not halt, reset, flash, continue, or open a second J-Link client unless explicitly allowed.

When the user explicitly requests Ozone memory writes, breakpoints, watchpoints, registers,
stepping, stopped context, or target disassembly, use the policy-gated persistent Ozone debug
agent. Do not weaken the read-only live controller to add those operations.

For live power/PWM/BMS debugging, classify the task as **Live real-time现场** even if the user says "use J-Link" or "look at RAM". Treat preserving the field condition as the primary requirement.

Discover device/probe, owner, tools/endpoints, source/build configuration and ELF/image from
the current session, process command lines and project artifacts first. Infer the mode from
the task instead of asking the user to choose tools. Ask only for missing facts that affect
target selection, behavior or authorization.

Existing explicit authorization remains valid for the named target and operations within
its stated limits; do not request it again for the same step. Before any new disturbance,
prepare the concrete target/image/command, reason, expected effect and final state. Ask only
if that action is outside the authorization already given. Observation does not authorize
reset, flash, halt or a competing debugger attach.

## Live Target Safety

For running power/PWM/motor/charging systems:

1. Treat `halt`, `interrupt`, GDB `Ctrl-C`, `-exec-interrupt`, `monitor halt`, breakpoints, and stepping as target-disturbing operations.
2. Treat `JLink.exe` / J-Link Commander `connect` as potentially invasive. It may apply a reset strategy, halt the core, run target initialization, or touch work RAM depending on device/configuration.
3. Treat `JLinkGDBServer -noreset` as "no explicit reset" only. It does not make later GDB interrupt/halt commands non-invasive.
4. Do not start a second J-Link client when the user already has Ozone connected to a live现场. Use that existing session through its authorized CLI/localhost Automation Socket when available; otherwise request the minimal watch values. The bundled fresh-session controllers do not adopt an existing Ozone process.
5. Prefer existing telemetry: Ozone watches, RTT/serial/CAN logs, fixed memory probes read by an already attached debugger, external instruments, LEDs/GPIO, oscilloscope, electronic load, or power-supply readings.
6. If RAM reads return all zero/old values, do not conclude ELF mismatch or firmware state from that alone. First verify whether the tool reset/halted, whether running-target memory read is supported, whether the target has reached app code, and whether the symbol/address is correct.
7. If a command may disturb现场, match it to the existing authorization; if it is not covered, state the concrete effect and obtain the missing authorization before execution.
8. If you disturbed现场, immediately say what command was run, what it may have changed, and stop further J-Link actions unless the user explicitly asks to recover or continue.

### J-Link live attach guardrail

Do not use J-Link Commander `connect` to read RAM/registers on a running power electronics, PWM, charging, motor, or BMS board unless the user explicitly accepts possible reset/halt. Some J-Link/device combinations reset and halt during `connect`, even if the script only contains memory reads. Prefer one of these paths:

1. Reuse the already attached Ozone/J-Link session through its authorized interface, or request a minimal watch list if no supported interface is available.
2. Use existing firmware telemetry over RTT/serial/CAN/KP protocol without attaching a debugger.
3. Use external instruments or GPIO/LED observations.
4. If debugger attachment is required, prepare the exact command and reset/halt/work-RAM effects. Obtain explicit authorization if the existing scope does not already cover that attachment.

After any J-Link command, inspect the log for `Reset`, `Halt`, `SYSRESETREQ`, work RAM initialization, or target initialization messages. If any appear, treat the live state as disturbed, stop further debugger actions, and report that the captured RAM/register values are not valid evidence for the original field condition.

## PC Hidden Ozone Live Attach

Use this path when Codex must run a PC-side non-invasive observation flow without showing
the Ozone window, after confirming no existing probe owner needs to be preserved:

```text
Codex
  -> hidden Ozone process on the Windows PC
  -> localhost Automation Socket (default 19200)
  -> CM_ATTACH
  -> allowlisted RAM reads only
  -> JSON evidence
  -> offline ELF/source analysis
```

Before using it, read [references/ozone-live-attach.md](references/ozone-live-attach.md)
completely. Use the bundled controller instead of rewriting socket or Ozone commands:

```powershell
python scripts/ozone_live_attach.py --list-probes --jlink-commander <JLink.exe>
python scripts/ozone_live_attach.py --config <live_attach.json> --dry-run
python scripts/ozone_live_attach.py --config <live_attach.json>
```

Rules:

1. Run `--dry-run` first. Review the exact MCU, probe serial, ELF SHA-256, resolved symbol
   addresses, read widths/counts, and exclusive-end RAM ranges.
2. When one or more J-Links are present, enumerate them and select by the non-zero numeric
   serial number. Never use USB discovery order. Require the selected serial to appear
   exactly once immediately before launching Ozone.
3. A successful dry run does not authorize hardware attachment. Run without `--dry-run`
   only after the user authorizes attaching to that named target.
4. Do not add a halt/reset/flash/write escape hatch to the live controller. Use a separate,
   explicitly authorized workflow for those actions.
5. Keep Ozone Automation Socket bound to localhost. Remote Codex systems must call a
   controlled PC agent or SSH wrapper, not expose port 19200 directly.
6. Run Ozone in an interactive Windows user session. A service may broker requests, but it
   must not launch Ozone directly from Session 0.
7. Use a fresh generated `.jdebug` and no reused `.user` file. Clear pending instruction and
   data breakpoints before `Debug.Start()`.
8. Treat any detected reset, halt, download, flash, SYSRESETREQ, or work-RAM initialization
   as disturbance. Stop reading and report `disturbed=true`.
9. Never fall back to GDB interrupt or J-Link Commander when background RAM access fails.
10. Running-core CPU registers and call stacks are outside this mode. Use telemetry, a
   firmware crash dump, or request permission for a stopped-fault capture.

## PC Hidden Ozone Advanced Debug Agent

Use this path only when the user asks Codex to directly operate Ozone for memory writes,
breakpoints/watchpoints, execution control, stopped registers/context, snapshots, or target
assembly. Read [references/ozone-debug-agent.md](references/ozone-debug-agent.md) completely
before starting it. Use the bundled persistent controller:

```powershell
python scripts/ozone_debug_agent.py --config <debug_agent.json> --dry-run
python scripts/ozone_debug_agent.py --config <debug_agent.json> --serve-stdio
python scripts/ozone_debug_agent.py --config <debug_agent.json> --serve-tcp 19300
```

Rules:

1. Keep `ozone_live_attach.py` read-only. Never add halt/write/breakpoint escape hatches to it.
2. Use only typed JSON operations from `ozone_debug_agent.py`; never expose raw Ozone Console,
   arbitrary expressions, reset, download, flash erase, or `Target.LoadMemory`.
3. Require the PC-owned configuration to enable the needed mode: `observe`, `live_modify`, or
   `stopped_debug`. Run `--dry-run` and inspect the exact serial, device, ELF hash, ranges, and
   enabled modes before hardware access.
4. Before every disturbing request, verify that explicit user authorization covers that
   operation on that J-Link/target. Do not re-ask within its accepted scope. Each typed request
   still needs a reason and `DISTURB:<serial>:<operation>` derived from that authorization;
   an enabled capability or a successful dry run is not user authorization.
5. For PWM, motor, charging, power conversion, or BMS hardware, require an externally safe
   power-stage state before halt, breakpoint/watchpoint, step, register write, or software
   breakpoint operations.
6. Prefer persistent `--serve-stdio`, native block reads, local stopped-context capture, and
   offline cached ELF disassembly. For repeated remote calls, bind `--serve-tcp` to localhost
   and reach it only through a PC-local client invoked by SSH/broker. Never expose either
   control port directly. Do not round-trip individual words through the Codex server.
7. Prefer hardware instruction breakpoints. Treat data breakpoints as halting operations and
   software/flash breakpoints as executable-storage modification.
8. Use compare-before-write and readback verification for RAM/register writes. A live RAM write
   is not atomic with firmware and always changes the现场.
9. Do not automatically resume during cleanup, timeout, or lost server connection. Query the
   state and make the final continue/close decision explicitly.
10. Preserve `audit.jsonl`, generated project, ELF hash, memory artifacts, snapshots, and all
    Ozone/J-Link logs. Stop on unexpected reset/download/flash/SYSRESETREQ/work-RAM evidence.
11. Pin the session to the exact non-zero J-Link serial and allow one local Ozone owner. Use
    isolated interactive Windows sessions/VMs/PC agents for simultaneous probes unless the
    installed Ozone version's independent port behavior has been verified.

## Verification Mode

Use `JLink.exe` for scripted validation only when the user accepts the run/halt/readback model.

Typical sequence:

```text
device <MCU>
si SWD
speed <verified_speed_khz>
connect
mem32 <verified_probe_addr> <word_count>
```

Rules:

1. Keep one direct J-Link owner. GDB and RTT socket clients may use that owner's server;
   do not launch another direct probe client. The example `connect` is itself potentially
   invasive. Add load/run/halt/exit only when the accepted verification sequence covers them,
   derive image addresses from the actual layout, and verify programmed bytes before run.
2. `JLink.exe` cannot simultaneously provide RTT Telnet logs through `JLinkGDBServer`.
3. Prefer structured probe data over free-form logs:

```c
typedef struct {
    uint32_t magic;
    uint32_t phase;
    uint32_t fail_code;
    int32_t last_error;
    uint32_t perf_ms;
} probe_status_t;

volatile probe_status_t g_probe_status;
```

4. Decode `mem32` values using `.map`, ELF symbols, or verified fixed addresses. If data is missing, check reset/run timing, probe initialization, wait duration, address mapping, and endianness before drawing conclusions.

## Debug Mode

Read [jlink-gdb-transport.md](references/jlink-gdb-transport.md) before a new session.
After checking owner, startup effects and attachment authorization, prefer MI2:

```text
-gdb-set mi-async on
-gdb-set pagination off
-gdb-set confirm off
-file-exec-and-symbols "<absolute_elf_path>"
-target-select remote <verified_host>:<verified_port>
```

Only when explicitly allowed:

```text
-interpreter-exec console "<verified_server_reset_halt_command>"
-target-download
-exec-interrupt
```

Use `-exec-continue` only when running is authorized. A normal main loop or RTOS scheduler
can make synchronous `continue` block forever; use MI asynchronous events and bounded host
observation. Breakpoints/watchpoints and interrupt need their own covered disturbance scope.
Do not automatically terminate/detach a debugger on timeout: that may change execution state.

For RTT:

1. Use `JLinkGDBServer` as the J-Link owner.
2. Connect RTT through its Telnet port, commonly `localhost:19021`.
3. Do not treat "no RTT log" as proof that firmware is not running. Check RTT initialization, channel, buffer, port, and whether the selected mode supports RTT.

## Common Flows

### Flash And Stop At Main

Use only after the user authorizes this flash/reset/breakpoint/run sequence. First resolve
the correct image and prepare readback as described in
[firmware-evidence.md](references/firmware-evidence.md). Prepare logging before run.
The comparison below covers loadable read-only sections when the target and mapping support it;
use documented verification or byte comparison at load addresses for remaining ranges.
Submit each MI operation separately and inspect its result. Stop before breakpoint/run if
download or verification fails; a comparison command completing does not mean its bytes match:

```text
-gdb-set mi-async on
-gdb-set pagination off
-gdb-set confirm off
-file-exec-and-symbols "<absolute_elf_path>"
-target-select remote <verified_host>:<verified_port>
-interpreter-exec console "<verified_server_reset_halt_command>"
-target-download
-interpreter-exec console "compare-sections -r"
-break-insert -h main
-exec-continue
```

### Attach To A Stopped Fault

Prefer the existing debugger's stopped session. A new attachment can reset or initialize
registers even when the MCU was already stopped; authorize and check that separately.
After a verified stopped attachment, collect without a redundant interrupt:

```text
-file-exec-and-symbols "<absolute_elf_path>"
-target-select remote <verified_host>:<verified_port>
-stack-list-frames
-data-list-register-values x
```

Do not use this for a running live control loop unless the user explicitly accepts the disturbance.

### HardFault Or Assert

Read the fault section of [firmware-evidence.md](references/firmware-evidence.md).
Being in `HardFault_Handler` does not mean the core is stopped. Use a saved dump or an
already stopped authorized session; inspect the validated exception frame, not just handler SP:

```text
-stack-list-frames
-data-list-register-values x
-interpreter-exec console "bt"
-interpreter-exec console "info registers"
-interpreter-exec console "x/<validated_word_count>wx <validated_frame_address>"
```

Read Cortex-M fault registers such as CFSR/HFSR/MMFAR/BFAR only using the actual MCU reference manual or header files. Do not invent bit meanings.

### Existing Ozone Live现场

When the user says Ozone still shows the现场:

1. Do not open `JLink.exe` or `JLinkGDBServer` unless the user asks and accepts possible connection loss or halt.
2. Read through the existing authorized CLI/socket session where supported, or supply a minimal watch list for the user: mode/state, fault flags, ADC raw/converted values, loop target/feedback, PWM enable, GPIO stop/error and timestamps. Do not launch a fresh hidden controller against that probe.
3. Cross-check Ozone values with external instruments before concluding firmware state.
4. If an ELF/release mismatch is suspected, verify offline first; do not use one failed RAM read as proof.

## Release And ELF Checks

Use [firmware-evidence.md](references/firmware-evidence.md) to identify the actual build,
compare expected bytes, and distinguish transfer, spot-check and full-range verification.
Before saying "ELF 不匹配" or "烧录包不对":

1. Compare ELF path, release package path, file timestamp, size, and if available CRC/hash.
2. Inspect map/ELF symbol addresses for the variables being read.
3. Verify boot/app layout: app slot address, app payload address, package header size, vector table, `.text`, `.init`, `.data` VMA/LMA, and whether the release image includes a header before the ELF payload.
4. Confirm the board has actually booted into the app, not bootloader/low-power/fault/reset loop.
5. Confirm the memory read method did not reset/halt the target and supports reading the requested address while running.

## Evidence And Code Changes

Every conclusion must cite evidence: logs, stack frames, register values, memory values,
external instruments, Ozone values, map/ELF data, or source references. Correlate the same
image and boot/log segment. Report source/build, programming/readback and board/instrument
proof separately, plus final observed state, remaining owner and any disturbance/uncertainty.

Before modifying code:

1. Explain the suspected failure path and the evidence.
2. Propose the smallest change that tests or fixes that path.
3. Avoid broad refactors during hardware debug.
4. Preserve user changes and do not revert unrelated files.
5. Build or validate when possible; if not possible, state what was not run.

## Hard Rules

1. Do not fabricate paths, MCU models, register bit definitions, addresses, or tool outputs.
2. Do not flash without explicit authorization.
3. Do not reset without explicit authorization.
4. Do not halt/interrupt a running real-time control现场 without explicit authorization.
5. Do not start competing J-Link clients.
6. Do not assume attach is non-invasive.
7. Do not infer ELF mismatch from one all-zero RAM read.
8. Do not leave the user without knowing whether现场 may have been changed.
9. Do not use J-Link Commander `connect` as a live read method on power/PWM/BMS hardware without explicit approval for possible reset/halt.
