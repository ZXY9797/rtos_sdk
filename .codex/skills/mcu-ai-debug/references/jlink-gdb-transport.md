# J-Link / GDB transport and session lifecycle

Read for a new authorized GDB session, Windows/WSL connectivity, RTT, or serial shell
validation. It does not authorize replacing an existing debugger owner or disturbing a target.

## Discover the endpoint before connecting

Record the physical USB owner, probe serial, exact device, server executable/version,
command line, listener address, GDB port and RTT port. A GDB client and an RTT socket client
may share one J-Link GDB Server; another direct probe client may not share its ownership.
Prefer passive process/listener inspection before making connections:

```powershell
Get-CimInstance Win32_Process |
  Where-Object { $_.Name -match 'JLink|Ozone|gdb' } |
  Select-Object ProcessId, Name, CommandLine
Get-NetTCPConnection -State Listen |
  Select-Object LocalAddress, LocalPort, OwningProcess
Get-Command arm-none-eabi-gdb, gdb-multiarch -ErrorAction SilentlyContinue
```

In Linux/WSL, `command -v`, `ss -ltnp` and `ip route show default` help discover local
tools, listeners and routing. WSL `ss` is not a listing of Windows-owned server processes.
Use an installed ARM-capable GDB compatible with the ELF/DWARF; `gdb-multiarch` is a fallback
when the project's `arm-none-eabi-gdb` is unavailable. Verify its version and ELF architecture.

Do not infer protocol from port 2331/3331 or from `monitor` support. GDB forwards monitor
commands to the server; command support and reset syntax are server-specific. An unsupported
command alone proves neither server identity nor broken Flash programming.

### WSL to Windows

Resolve addresses from the actual WSL networking mode and Windows listener. Mirrored
networking supports Windows access through `127.0.0.1`; NAT commonly needs the Windows
host gateway address. These are distinct cases, as described in
[Microsoft's WSL networking documentation](https://learn.microsoft.com/en-us/windows/wsl/networking).
DNS nameservers alone are not sufficient proof of the GDB host address.

If a server is bound only to Windows loopback, a NAT gateway connection will not reach that
listener. Prefer a controlled host-side client or existing SSH tunnel. Do not scan a subnet,
disable a firewall, change WSL networking, or expose debug/control ports just to find a target.
For Ozone, keep both Automation Socket and any PC agent control port on localhost.

Only probe a known unused GDB endpoint when connection/disconnection effects are acceptable.
Even a test TCP connection may occupy the single client slot or trigger a single-run server's
lifecycle. Never send test text such as `echo` to a debug-protocol port.

## Connect within the established authorization

Inspect server startup settings, device scripts and GDB init files before launch/attach.
`-noreset` alone is not an attach guarantee: SEGGER exposes separate halt and register
initialization settings. Use the installed version's help and logs, not a copied flag set.
See [SEGGER's server options](https://kb.segger.com/J-Link_GDB_Server#Command_line_options).

Prepare a command file or structured MI2 request stream with actual paths/endpoints.
On Windows launch authorized background helpers hidden. Keep stdout, stderr, process IDs
and session ownership. Example MI2 shape, after attachment has been authorized:

```text
-gdb-set mi-async on
-gdb-set pagination off
-file-exec-and-symbols "<absolute_elf_path>"
-target-select remote <verified_host>:<verified_port>
```

Use `-nx` when launching GDB if project/user init files must be bypassed for a controlled
session, and inspect any retained auto-load scripts. Host-only symbol inspection is separate
from connecting to hardware. Do not append reset, download, interrupt or breakpoints to an
otherwise observational script. Apply the requested operations individually within scope.

MI automation must correlate result tokens and asynchronous running/stopped events. Do not
interpret `^running` as test completion. Keep a persistent controller for a running target;
do not put commands after a blocking CLI `continue` and expect them to execute immediately.

## Run, timeout and disconnect

Choose the requested final state before starting. `detach`, `disconnect`, GDB exit and server
termination are different operations; their effects depend on the remote stub/settings.
GDB documents this distinction in [remote connections](https://www.sourceware.org/gdb/current/onlinedocs/gdb.html/Connecting.html).
Do not assume that disconnect always halts or always resumes the MCU.

An external timeout limits host work, not hardware effects. Do not kill a debugger as the
default way to stop a wait: closing a server may resume the target. Preserve an already
stopped fault unless continuing was authorized; keep the owner alive if that is needed.
On timeout/lost connection, report the last observed state and uncertainty. Query through
the existing owner when possible; never attach a competing client just to check state.

If authorized cleanup requires stopping a helper, affect only the recorded process owned
by this task. Do not kill all J-Link/Ozone/GDB processes by name.

## Serial and RTT evidence

Discover the serial device from USB identity, configuration
and current owner. Verify the baud rate from the target configuration.
Check baud/parity/stop bits, flow control and DTR/RTS/reset wiring before opening it.
Keep one serial reader to avoid splitting evidence across consumers.

Prepare the configured reader and begin timestamped capture **before** an authorized reset
or run, so the first boot output is retained. Missing an already-passed boot banner does not
justify resetting a live target. Follow USB re-enumeration using device identity, not only
the old port name. UART writes and shell commands need to be within the requested test scope;
choose a benign query such as `help` only when its firmware behavior is known.

For an authorized shell test, keep the reader open, send the expected CR/LF terminator, then
verify a complete response/prompt within a bounded interval. A boot banner proves only boot
progress; an echo proves only that transaction. For intermittent stalls, exercise repeated
commands and recovery paths with sequence/count evidence. Keep raw bytes; strip ANSI only
in a derived view. Repeated shell testing does not imply permission to run arbitrary commands.

For RTT, connect to the existing server's configured Telnet port (19021 is only a common
default), verify channel/control-block initialization and buffer settings. No RTT output is
not proof of a stopped CPU. Do not start a direct RTT probe owner alongside the server.
