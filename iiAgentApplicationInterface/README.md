# iiAgentApplicationInterface

iiAgentApplicationInterface is a small in-process foundation for agent-accessible application objects, semantic commands, and state events. A trusted application registers its own objects, validators, handlers, authorization policy, and subscriptions. UI and agent adapters can invoke the same application command.

The initial local prototype was named `iiAgent`. Its public names have been replaced: update CMake package and target names, C++ includes and namespace, C symbols, Python imports, and configuration variables, then rebuild consumers. No compatibility aliases are provided.

The existing runtime is serialized on a host thread; tracked object destruction can be handed back from other threads. Authorization defaults to deny. The host authenticates principals and owns side effects, transaction and undo semantics, and approval. Runtime capability schemas remain metadata and each capability still requires a host validator. The command API adds explicit JSON Schema validation, command discovery and resolution, progress and cancellation, and MCP and CLI adapters. Host-composed Python transports serve MCP over standard input/output or Streamable HTTP. See [the command contract](docs/Commands.md) for validation coverage and transport limits.

## Build and test

```sh
iisdk-builder build --mode library --project . --project-build-directory --platform host
ctest --test-dir build --output-on-failure
./install.sh                          # stages to build/install
# ./install.sh /chosen/install/prefix # optional explicit prefix
# INSTALL_PREFIX=/chosen/prefix ./install.sh # also supported by the SDK batch installer
```

The direct build option reuses `build/` for one host kit; other builder modes place their configured build in a kit directory beneath `build/`. The installer uses the cached CMake build type, or Release if unset; set `IIAGENTAPPLICATIONINTERFACE_BUILD_CONFIG` to override it. CMake options include `BUILD_SHARED_LIBS`, `BUILD_TESTING`, `IIAGENTAPPLICATIONINTERFACE_BUILD_PYTHON`, and `IIAGENTAPPLICATIONINTERFACE_BUILD_EXAMPLES`. Python requires a shared build. iiSDKCore Contracts and Runtime are required; Qt is not. JSON Schema builds also require Rust and Cargo for the native exact-number backend. The pinned Corvus schema engine uses platform packages, or a target-native package supplied with `IIAGENTAPPLICATIONINTERFACE_CORVUS_PREFIX`. Cross-builds also need a matching `IIAGENTAPPLICATIONINTERFACE_RUST_TARGET`. See the command contract for platform and JSON Schema coverage.

## Commands and MCP

Include `<iiAgentApplicationInterface>` for the command values, `CommandSchemaValidator`, cancellation handle, resolver, scope policy and adapters. The Python extension is imported as `iiagent_application_commands`. The host supplies grants and schema reference documents explicitly; a schema or published tool does not authorize execution. `Runtime::add_cancellable_capability` supplies a stop token and progress callback to a host-thread handler. An isolated process job can be terminated on cancellation; its prior side effects are not rolled back.

The installed `iiagent-command` calls an application-owned local gateway. `iiagent-mcp` can expose it over stdio or Streamable HTTP, for example after the host supplies an endpoint and sets `IIAGENT_APPLICATION_CREDENTIAL`:

```sh
iiagent-command --endpoint /app/owned/agent.sock --target document --command rename --arguments '{"name":"New"}'
iiagent-mcp --endpoint /app/owned/agent.sock --transport stdio
```

For HTTP use `--transport http`, an explicit `--host` and `--port`, and `--allow-origin` for trusted browser origins. TLS and the credential verifier belong to the host. `McpCommandServer` also accepts a host backend for discovery, execution, progress, and cooperative cancellation. The existing synchronous gateway proxy cannot forward progress or cancellation to a running application command; applications that need them must supply a backend that integrates those callbacks. The transports implement the 2025-11-25 protocol version. They do not automatically attach to a running application.

## Native application procedures

A trusted host can associate an existing registry object with a `std::shared_ptr<T>` using `Runtime::bind_live_object`. `Runtime::execute_procedure` passes an authorized `ProcedureContext` to a C++ procedure. The procedure can query application objects, invoke the same command bus, or obtain a type-checked shared reference to the live object and use its ordinary C++ API. Native reads, native writes, and procedure execution have separate host-policy actions. The runtime retains only a weak reference to the application object.

A procedure exports `iiagent_application_procedure_v1`, returning a `NativeProcedureDescriptor`. `Runtime::execute_source` authorizes compilation, loading and execution separately, coordinates the host's `SourceProcedureBackend`, checks the descriptor, and keeps the loaded module and its artifact alive until the procedure returns. A C host can provide equivalent backend callbacks. The native C++ runtime defines the backend port; the application supplies compiler, storage and loader implementations. `tests/source_runtime_native.cpp` demonstrates an actual host adapter and compiles a source string at runtime to modify a tracked C++ application object. The descriptor version checks layout; it does not guarantee C++ ABI compatibility.

The Python host binding also provides `Runtime.execute_source`, and `iiagent_application_interface_native.NativeSourceBackend` supplies a clang++-style compiler/loader adapter. The host supplies a storage adapter, an existing `build/` directory, compiler, application-compatible compile and link flags, and the policy. `example/python_source_storage.py` shows an application wiring the storage adapter to iiFileProvider; the module does not import another SDK. Source is passed through standard input without a shell. By default loaded modules remain alive until the host explicitly closes the backend; unloading is safe only after the host has ensured that procedure code, callbacks, threads and references cannot outlive the module. Python procedures see object snapshots and commands, not typed C++ object references.

`Runtime::make_tracked_object<T>` constructs a shared object, registers its live binding, and removes its registry entry and commands after its last shared reference is released. For a last release on another thread, configure `set_lifecycle_dispatcher` with a thread-safe host event-loop post function; it schedules removal on the runtime's owner thread. Without a dispatcher, call `drain_lifecycle()` on that thread to process queued removals. Existing unmanaged objects, stack objects and objects created outside this factory are not automatically tracked: bind them explicitly or adapt the application's creation path. See `example/native_procedure.cpp` and `docs/Contract.md` for ownership and threading details.

Compiler and loaded native code execute with the application's process privileges; loading can execute module initializers before the procedure entry. Grant compilation and loading only to code trusted by the host. Direct object mutation does not automatically create transactions, undo records, or state events; the application must provide those behaviors.


## Agent-authored procedures

`Runtime::procedure_authoring_context` returns the procedure ABI, available `ProcedureContext` signatures and data types, plus a point-in-time view of objects, commands and native access allowed to the principal. When available and authorized, `application.build_catalog` appears in that view as source-declaration metadata. Discovery and native-access flags do not guarantee that a particular C++ member is public, callable, or ABI-compatible.

An agent can write a procedure body and optional C++ preamble itself. The host configures a `SourceProcedureBackend`, authorizes compilation, loading and execution, and supplies compatible application headers and build flags. Building does not run the procedure. Each later execution rechecks load and execution permissions, and the same compiled artifact can be reused. The following Python example assumes `runtime`, `backend` and the application's `document` command have already been configured by the host:

```python
contract = runtime.procedure_authoring_context("local-agent")
# The agent reads contract and authors the C++ body.
body = 'return context.invoke("document", "rename", input);'
source = runtime.compose_procedure_source("agent.rename", body)
with runtime.build_procedure("local-agent", "agent.rename", body, backend) as procedure:
    result = procedure.execute("local-agent", "New title")
```

C++ hosts use `ProcedureDraft`, `compose_procedure_source`, `Runtime::build_procedure` and `Runtime::execute_built`. The backend must outlive its compiled artifact. C hosts have equivalent JSON, assembly and opaque artifact APIs. Python retains backend callbacks with the compiled handle, which must be closed when no longer needed. The module exposes an interface for an agent; it does not supply a model that writes the code. Native procedures run with process privileges and require trusted code.


## External agents in a running application

An application can expose its existing runtime to an approved external agent without MCP. Construct `ExternalGateway` with the live `Runtime`, a host authenticator and an optional native source backend. The authenticator receives the local transport's peer attributes and the presented credential, and returns the authenticated `AgentContext`; an absent, failing or rejecting authenticator denies access. Each request is authenticated again and existing per-object, command and procedure policies are enforced. The gateway rejects calls on a thread other than the runtime owner.

The application-side adapter in `example/iiNetworkExternalBridge.h` and `.cpp` composes `ExternalGateway` with iiNetwork local sockets and iiJson. Compile those files into the application and link the independent SDK modules there; the iiAgentApplicationInterface library itself does not link them. Construct the runtime, gateway and bridge on the same iiSDKCore event loop thread, provision an app-owned endpoint, implement the real approval verifier, and keep them alive while serving. The reference adapter accepts one bounded newline-delimited JSON request per connection. It does not implement an approval store, an external event stream or persistent compiled-procedure handles.

After installing the packages, an application's CMake project can compile the installed adapter as application code:

```cmake
find_package(iiAgentApplicationInterface CONFIG REQUIRED)
find_package(iiNetwork CONFIG REQUIRED)
find_package(iiJson CONFIG REQUIRED)
find_package(iiSDKCore CONFIG REQUIRED COMPONENTS Runtime)
target_sources(MyApp PRIVATE "${iiAgentApplicationInterface_EXAMPLE_DIR}/iiNetworkExternalBridge.cpp")
target_include_directories(MyApp PRIVATE "${iiAgentApplicationInterface_EXAMPLE_DIR}")
target_link_libraries(MyApp PRIVATE iiAgentApplicationInterface::iiAgentApplicationInterface
    iiNetwork::iiNetwork iiJson::iiJson iiSDKCore::Runtime)
```

The app must still start the bridge and pass its own live runtime and verifier; CMake linkage does not do this automatically.

The installed Python client and CLI can then connect to that running application. The app or its trusted launcher supplies the endpoint and host-issued credential; the CLI reads the credential from `IIAGENT_APPLICATION_CREDENTIAL` by default rather than a command argument. For example, after setting `PYTHONPATH` to the installed Python directory and receiving an endpoint and credential from the app:

```sh
printf '%s' '{"operation":"describe"}' | python3 -m iiagent_application_interface_cli --endpoint /app/owned/agent.sock
printf '%s' '{"operation":"list_objects","limit":20}' | python3 -m iiagent_application_interface_cli --endpoint /app/owned/agent.sock
printf '%s' '{"operation":"invoke","object_id":"document","capability_id":"rename","input":"New title"}' | python3 -m iiagent_application_interface_cli --endpoint /app/owned/agent.sock
```

`describe` provides compact operation names and fields. Object pages omit state; `read_object` fetches one selected object's host-published snapshot and `list_capabilities` pages through its commands. The app must publish state changes; the snapshot is not an automatic serialization of arbitrary live memory. `execute_source` and `execute_draft` are available only if the host supplies a source backend; they run synchronously on the app owner thread. Code executes with the application's privileges. A same-user socket, peer UID, environment variable or caller-provided name alone is not agent approval: use a host-controlled verifier, provision a trusted endpoint and treat credentials as secrets. The local transport is not encrypted. If a connection ends before its response, the command may already have run; the protocol does not provide automatic deduplication. The supplied test host uses a fixture verifier, not a production approval system.

To exercise the optional multi-process composition test against installed iiNetwork, iiSDKCore and iiJson, configure `IIAGENTAPPLICATIONINTERFACE_TEST_IISDK_INTEGRATION=ON`, `IIAGENTAPPLICATIONINTERFACE_SDK_ROOT` and, if needed, `IIAGENTAPPLICATIONINTERFACE_OPENSSL_ROOT`, then build and run CTest. The test connects separate CLI processes to a live C++ app; it does not exercise an actual Codex or Claude Code session.

## Build-time application discovery

Install the package, then connect discovery after defining the application's local link dependencies:

```cmake
find_package(iiAgentApplicationInterface CONFIG REQUIRED)
add_executable(MyApp App/main.cpp)
# Add the application's local libraries before enabling discovery.
iiAgentApplicationInterface_enable_discovery(MyApp ROOTS "${CMAKE_SOURCE_DIR}")
```

Create the application runtime with its authorization policy; the generated catalog is published automatically:

```cpp
#include <iiAgentApplicationInterface>

auto runtime = iiAgentApplicationInterface::generated::create_application_runtime(policy);
```

For an existing runtime, call `generated::publish_build_catalog(runtime)` once. Authorized consumers can read `application.build_catalog` through `Runtime::object` or `objects`. Its state is JSON containing source declarations, their recognized documentation comments, and private member descriptions; it is not a live-object bridge. A Python host can use `iiagent_application_interface_discovery.discover_compilation_database` and `publish_catalog` with its native runtime. The build step needs Python and libclang and currently supports single-configuration CMake Ninja or Makefiles compilation databases. It scans the app and recognizable linked local source targets within the chosen roots. Prebuilt binaries, unrecognized conditional link expressions, uninstantiated template details, live objects, member values, and callable bindings are not comprehensively discovered. The host must still bind execution to actual objects and enforce authorization. See `docs/Contract.md` for the precise limits.

## C++ and C

```cmake
find_package(iiAgentApplicationInterface CONFIG REQUIRED)
target_link_libraries(MyApp PRIVATE iiAgentApplicationInterface::iiAgentApplicationInterface)
```

Include `<iiAgentApplicationInterface>` for C++23. The target also defines `IISDK_IMPORT_IIAGENTAPPLICATIONINTERFACE=1`. `<iiAgentApplicationInterface/AgentC.h>` supplies the C interface, whose allocated output is released with `iiagent_application_interface_free`. See `example/host.cpp` for UI and agent calls sharing a command and `docs/Contract.md` for the host's responsibilities.

## Python

The binding calls the same native runtime through its C ABI. Add `<prefix>/share/iiAgentApplicationInterface/python` to `PYTHONPATH`; its sibling library is located automatically. Set `IIAGENTAPPLICATIONINTERFACE_LIBRARY` to an explicit library path when needed.

```python
from iiagent_application_interface import Runtime

with Runtime(lambda principal, action, obj, cap, permissions: principal == "local") as runtime:
    runtime.add_object("document.1", "document", state="Draft")
    print(runtime.objects("local"))
```

The example policy is only a local demonstration, not authentication. Host code must keep callbacks and policy within its trust boundary. See [the contract](docs/Contract.md) for event ordering, object lifetimes, schema, and limitations.
