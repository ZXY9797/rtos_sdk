# Runtime contracts

`sdk::runtime` is a header-only C++17 component. Its headers are owned by
`component/runtime/include/runtime`; it does not export or depend on platform
headers. Firmware platform targets explicitly link this component when needed.

The core API contains:

- `execution.h`: execution descriptions, supported option validation and timing metrics.
- `output_latch.h`: bounded output-stop registration and irreversible fault latching.
- `wire.h`: bounded, explicit wire encoding.

Executors configure scheduling and hardware separately. `ExecutionSpec` does not
create threads or configure timer frequencies. Its supported overflow policies
are `RejectNewest` and `Fault`; unsupported or invalid options must be rejected
even when timing collection is disabled.

`experimental/async_pool.h` is a reference request registry used by the control
pipeline sample and host tests. Its types live in `runtime::experimental` and
are not a stable product API. The IPC and Link components follow the same rule
for their experimental timed-topic and typed-command examples. Promote these
interfaces only when product use establishes the required ownership contracts.
