# Firmware identity, programming verification and fault evidence

Read when selecting an ELF/image, diagnosing suspected incomplete programming, or interpreting
a crash. Offline inspection comes first. Target reads still need a suitable existing owner
or authorized attach workflow; this reference never expands the live controller's RAM allowlist.

## Identify the build and address layout

Resolve app/board/toolchain/build directory from build scripts, presets/cache, generated
configuration and the most recent successful build log. Do not hardcode `out/` or select the
newest ELF among unrelated targets. In a confirmed Lark build the ELF may be
`<build_dir>/lark/lark.elf`; verify that path against its actual target configuration.

Record absolute paths, source revision/dirty changes, configuration, size, time and SHA-256
of the ELF and programmed package. Where files cross Windows/WSL or a remote PC, confirm
the owner's copy is the same artifact. Timestamps are a freshness clue, not image identity.

Use the toolchain's `readelf -h -l -S`, `objdump -h`, `nm -n`, linker script and map offline.
Derive architecture, vector-table location, entry point, reset-handler symbol and loadable
ranges. Check bootloader/app slot, package headers, aliases/remapping and VMA versus LMA,
including initialized data copied from Flash into RAM. A raw BIN needs an independently
verified load address. Preserve release optimization when diagnosing timing problems;
a new `-O0` build can alter the failure being investigated.

## Program only the requested image, then verify

Before an authorized programming operation, prepare the exact probe, device, image/hash,
load ranges, reset/run sequence, readback method and final state. Honor prior authorization
for that same target and operation; do not ask again unless the scope changes.
Select the Flash algorithm and commands from the actual server/device support.

Treat a successful `load`/`-target-download`/`loadfile` as a transfer result. Verify using the
server's documented download verification or target readback compared with expected bytes.
SEGGER documents its `-vd` option in [J-Link GDB Server](https://kb.segger.com/J-Link_GDB_Server#-vd).
Retain verification output and which address ranges it covers.

Where supported and address mappings match, GDB can compare loadable read-only sections:

```text
compare-sections -r
```

See [GDB section comparison](https://www.sourceware.org/gdb/current/onlinedocs/gdb.html/Memory.html).
For unsupported targets or differing LMA/VMA/package layouts, dump the relevant target ranges
and compare offline with the image's bytes at their actual load addresses. A read of `.data`
after execution cannot be compared blindly against its initial file contents; `.bss` has no
programmed payload. Account for installed software breakpoints, remapping and caches.

At minimum inspect the actual vector/reset entry and representative executable bytes against
the expected artifact, not merely whether they differ from `0xffffffff`. A few matching words
are a spot check, not proof the whole image was programmed. Report any unchecked ranges.

### A failed verification is evidence to preserve

Record expected/actual bytes, address and length, all download/server logs and target state
before attempting recovery. Check wrong slot/base, stale image, read protection, mapping,
connection/voltage, device/Flash algorithm and permitted transfer speed.
An all-zero or erased-looking read by itself does not identify the cause.

Retry only if evidence points to a recoverable cause and the existing authorization covers
the same write/reset sequence. Use at most one additional attempt for that diagnosed cause;
stop if the same mismatch remains. Do not repeatedly `load`, erase more memory, change option
bytes/protection, or reset to make a verification error disappear. Ask only if the next
necessary recovery action exceeds the accepted scope.

Start the already-prepared log reader before an authorized run/reset. Verify expected boot
phase, application behavior and requested final state separately from Flash byte verification.

## Cortex-M fault interpretation

Use a saved dump or already stopped, authorized debugger session. Being inside a fault handler
does not by itself mean the CPU is halted. Do not reset before collecting evidence.

Collect the original fault frame and code context where available: PC/LR/xPSR, R0-R3/R12,
MSP/PSP, EXC_RETURN, stack bounds, relevant instructions, fault-status registers and logs.
Derive the active stack and basic/extended frame from the actual core's exception rules;
handler SP, an RTOS task frame and the hardware-stacked fault PC are not interchangeable.
Validate stack pointers and unwind reliability before trusting a backtrace.

Decode CFSR/HFSR and other registers from the matching CMSIS headers or core/MCU manual,
including valid-address flags before interpreting BFAR/MMFAR. Preserve raw values first;
do not clear W1C fault evidence while collecting it. An escalation bit does not identify the
underlying cause by itself. Not every Cortex-M implements the same fault registers.

For UNDEFINSTR/invalid state, compare instruction bytes at the validated stacked PC with the
correct image, then inspect control flow, function pointers/returns, stack corruption,
instruction-set configuration and Flash mapping. Neither UNDEFINSTR nor a reset-handler
frame alone proves incomplete programming. Do not copy numeric bit meanings or historical
board addresses from another case.

## Deliver the evidence chain

Report symptom/time window → raw values/logs → field/bit interpretation → source condition
or instruction path → supported conclusion → smallest next check or fix.
Use only the selected log/boot segment for a selected-segment question; keep older events
separate. Correlate logs, target bytes and symbols with the same image and boot session.

Separate source/build proof, programming/readback proof and board/instrument proof.
State capture method, whether the target was disturbed, last observed running/stopped state,
who still owns the probe, and unresolved evidence. Do not call a reset flag proof of physical
power loss without the relevant electrical/sequence evidence.
