# Ozone Live Attach Controller

Use this reference only for a Windows PC that locally owns the USB J-Link. The controller
launches a fresh hidden Ozone process, uses the localhost Automation Socket, selects
`CM_ATTACH`, reads allowlisted RAM, and emits JSON. It has no reset, halt, continue,
download, breakpoint, register-write, or memory-write operation.

## Preconditions

- Run from an interactive Windows user session; do not launch Ozone directly from a
  Windows service Session 0.
- Require the selected probe to have no existing owner before starting this fresh-session
  controller. Do not close an active Ozone/IDE/J-Link session to satisfy this precondition.
  Reuse that owner's authorized interface, or perform an explicitly authorized handoff first.
- Provide the exact SEGGER device name, J-Link serial number, matching ELF, Ozone path,
  and explicit SRAM bounds from the MCU/linker configuration.
- Treat UART transmit, peripheral-register reads, halt, reset, flash, and writes as separate
  workflows requiring explicit authorization. This controller does none of them.

## Configuration

Create JSON on the PC:

```json
{
  "schema_version": 1,
  "ozone_exe": "C:/Program Files/SEGGER/Ozone/Ozone.exe",
  "jlink_commander": "C:/Program Files/SEGGER/JLink/JLink.exe",
  "device": "STM32F407VG",
  "probe_serial": "123456789",
  "target_interface": "SWD",
  "speed": "1 MHz",
  "elf": "D:/firmware/build/firmware.elf",
  "symbol_tool": "C:/ArmGNU/bin/arm-none-eabi-nm.exe",
  "automation_port": 19200,
  "timeout_s": 20,
  "output_dir": "D:/mcu-captures",
  "allowed_ram_ranges": [
    {"name": "sram", "kind": "ram", "start": "0x20000000", "end": "0x20020000"}
  ],
  "reads": [
    {"name": "bms_state", "symbol": "g_bms_state", "width": 32, "count": 1},
    {"name": "fault_words", "address": "0x20001000", "width": 32, "count": 8}
  ],
  "additional_disturbance_patterns": []
}
```

`end` is exclusive. Every byte of every read must be inside an allowlisted range. Width is
8, 16, or 32 bits. Symbol reads are resolved offline with the configured `nm`; Ozone never
evaluates a user-provided expression. Do not guess RAM bounds or symbol tools.

When multiple J-Links are connected, enumerate them before preparing the configuration:

```powershell
python scripts/ozone_live_attach.py --list-probes `
  --jlink-commander "C:/Program Files/SEGGER/JLink/JLink.exe"
```

This runs `ShowEmuList USB` without a target `Connect` command and returns JSON containing
each probe's serial number, product, and nickname. Put the selected numeric serial in
`probe_serial`. The live controller enumerates again immediately before Ozone starts and
refuses to attach unless that serial occurs exactly once. Ozone receives the same selection
through `Project.SetHostIF("USB", "<serial>")`.

## Commands

Validate without Ozone or hardware:

```powershell
python scripts/ozone_live_attach.py --config live_attach.json --dry-run
```

Run only after the user explicitly authorizes attachment to the named live target:

```powershell
python scripts/ozone_live_attach.py --config live_attach.json
```

Run offline controller tests:

```powershell
python scripts/ozone_live_attach.py --self-test
```

The process writes exactly one JSON document to stdout. Preserve its session directory,
ELF SHA-256, generated `.jdebug`, Ozone/J-Link logs, target state, RAM values, cleanup
status, and disturbance evidence.

## Decision Rules

1. Run `--dry-run` first and inspect every resolved address and range.
2. Never start the controller if another J-Link owner exists. The controller also checks
   common process names and refuses to proceed.
3. If `disturbed` is true, stop. Do not interpret RAM as the original field condition and do
   not issue continue/reset commands to recover it.
4. `halted_observed` does not prove Ozone halted the core; the original state is unknown.
   Use the logs as evidence and state that uncertainty.
5. If background RAM access fails, do not fall back to halt. Use UART/RTT/CAN telemetry or
   request explicit authorization for a separate stopped-fault workflow.
6. Running-core CPU registers and call stacks are unavailable in this non-invasive mode.
