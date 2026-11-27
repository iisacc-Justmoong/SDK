#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Status values returned by C operations; failures do not imply rollback. */
typedef enum iiagent_application_interface_status {
    IIAGENTAPPLICATIONINTERFACE_OK = 0,
    IIAGENTAPPLICATIONINTERFACE_INVALID_ARGUMENT,
    IIAGENTAPPLICATIONINTERFACE_NOT_FOUND,
    IIAGENTAPPLICATIONINTERFACE_ALREADY_EXISTS,
    IIAGENTAPPLICATIONINTERFACE_DENIED,
    IIAGENTAPPLICATIONINTERFACE_VALIDATION_FAILED,
    IIAGENTAPPLICATIONINTERFACE_HANDLER_FAILED,
    IIAGENTAPPLICATIONINTERFACE_INTERNAL_ERROR,
    IIAGENTAPPLICATIONINTERFACE_CANCELLED
} iiagent_application_interface_status;

/* Independently authorized actions, matching the C++ AccessRequest contract. */
typedef enum iiagent_application_interface_action {
    IIAGENTAPPLICATIONINTERFACE_READ_OBJECT = 0,
    IIAGENTAPPLICATIONINTERFACE_DISCOVER_CAPABILITY,
    IIAGENTAPPLICATIONINTERFACE_EXECUTE,
    IIAGENTAPPLICATIONINTERFACE_OBSERVE,
    IIAGENTAPPLICATIONINTERFACE_READ_NATIVE_OBJECT,
    IIAGENTAPPLICATIONINTERFACE_WRITE_NATIVE_OBJECT,
    IIAGENTAPPLICATIONINTERFACE_EXECUTE_PROCEDURE,
    IIAGENTAPPLICATIONINTERFACE_COMPILE_PROCEDURE,
    IIAGENTAPPLICATIONINTERFACE_LOAD_PROCEDURE
} iiagent_application_interface_action;

/* Event kinds delivered by an authorized subscription. */
typedef enum iiagent_application_interface_event_kind {
    IIAGENTAPPLICATIONINTERFACE_OBJECT_ADDED = 0,
    IIAGENTAPPLICATIONINTERFACE_OBJECT_REMOVED,
    IIAGENTAPPLICATIONINTERFACE_CAPABILITY_ADDED,
    IIAGENTAPPLICATIONINTERFACE_CAPABILITY_REMOVED,
    IIAGENTAPPLICATIONINTERFACE_STATE_CHANGED,
    IIAGENTAPPLICATIONINTERFACE_COMMAND_COMPLETED,
    IIAGENTAPPLICATIONINTERFACE_COMMAND_FAILED,
    IIAGENTAPPLICATIONINTERFACE_PROCEDURE_COMPLETED,
    IIAGENTAPPLICATIONINTERFACE_PROCEDURE_FAILED
} iiagent_application_interface_event_kind;

/* Opaque trusted-host runtime; create and destroy on the same serialized thread. */
typedef struct iiagent_application_interface_runtime iiagent_application_interface_runtime;
/* Opaque compiled artifact. Its backend callbacks and user data must outlive it. */
typedef struct iiagent_application_interface_compiled_procedure iiagent_application_interface_compiled_procedure;

/* Called for each access decision; nonzero permits it. Strings are borrowed UTF-8. */
typedef int (*iiagent_application_interface_authorizer)(
    void *user, const char *principal, int action, const char *object_id,
    const char *capability_id, const char *const *permissions, size_t permission_count);
/* Called before execution; a borrowed reason must stay valid until the enclosing API call returns. */
typedef int (*iiagent_application_interface_validator)(void *user, const char *input, const char **reason);
/* Called at most once; output must stay valid until the enclosing API call returns. */
typedef int (*iiagent_application_interface_handler)(
    void *user, const char *principal, const char *input, const char **output);
/* Called synchronously for each authorized event; string arguments are borrowed. */
typedef void (*iiagent_application_interface_observer)(
    void *user, int kind, const char *object_id, const char *capability_id,
    uint64_t instance, uint64_t revision);

/* Compile/load callbacks use opaque nonzero handles and diagnostics valid through the call. */
typedef int (*iiagent_application_interface_source_compile)(void *user, const char *identifier,
    const char *source, uint64_t *artifact, const char **diagnostic);
typedef int (*iiagent_application_interface_source_load)(void *user, uint64_t artifact,
    uint64_t *module, const void **descriptor, const char **diagnostic);
typedef void (*iiagent_application_interface_source_release)(void *user, uint64_t handle);
/* Host-owned source backend; callbacks are used synchronously during execute_source. */
typedef struct iiagent_application_interface_source_backend {
    size_t size;
    iiagent_application_interface_source_compile compile;
    iiagent_application_interface_source_load load;
    iiagent_application_interface_source_release release_artifact;
    iiagent_application_interface_source_release release_module;
    void *user;
} iiagent_application_interface_source_backend;

/* Return values correspond to iiAgentApplicationInterface::Status in Agent.h; zero indicates success. */
/* Creates a runtime. A null policy denies every consumer operation. Returns null on failure. */
iiagent_application_interface_runtime *iiagent_application_interface_create(
    iiagent_application_interface_authorizer policy, void *user);
/* Destroys a runtime and subscriptions. Never call while one of its calls or callbacks is active. */
void iiagent_application_interface_destroy(iiagent_application_interface_runtime *runtime);
/* Releases an allocated output returned by this C interface. Null is accepted. */
void iiagent_application_interface_free(void *memory);
/* Registers an object; text arguments must be non-null UTF-8 strings. */
int iiagent_application_interface_add_object(
    iiagent_application_interface_runtime *, const char *id, const char *type,
    const char *description, const char *state);
/* Removes an object and all of its commands. */
int iiagent_application_interface_remove_object(iiagent_application_interface_runtime *, const char *id);
/* Publishes the trusted host's current state. */
int iiagent_application_interface_update_state(
    iiagent_application_interface_runtime *, const char *id, const char *state);
/* Registers a command. Callbacks and user data must remain valid until runtime destruction. */
int iiagent_application_interface_add_capability(
    iiagent_application_interface_runtime *, const char *object_id, const char *id,
    const char *description, const char *input_schema, const char *output_schema,
    const char *const *permissions, size_t permission_count,
    iiagent_application_interface_validator validator, iiagent_application_interface_handler handler, void *user);
/* Removes one registered command. */
int iiagent_application_interface_remove_capability(
    iiagent_application_interface_runtime *, const char *object_id, const char *id);
/* Returns an allocated JSON array of authorized object snapshots in out_json. */
int iiagent_application_interface_objects(
    iiagent_application_interface_runtime *, const char *principal, char **out_json);
/* Returns one authorized object as JSON or JSON null for hidden/unknown objects. */
int iiagent_application_interface_object(
    iiagent_application_interface_runtime *, const char *principal, const char *id, char **out_json);
/* Returns an allocated JSON array of authorized command descriptions. */
int iiagent_application_interface_capabilities(
    iiagent_application_interface_runtime *, const char *principal, const char *object_id, char **out_json);
/* Returns a JSON authoring contract and only currently authorized objects and commands. */
int iiagent_application_interface_procedure_authoring_context(
    iiagent_application_interface_runtime *, const char *principal, char **out_json);
/* Assemble an agent-authored preamble and body into a complete native C++ source module. */
int iiagent_application_interface_compose_procedure_source(
    iiagent_application_interface_runtime *, const char *identifier, const char *preamble,
    const char *body, char **out_source);
/* Executes one command and allocates either its result or diagnostic in out_text. */
int iiagent_application_interface_invoke(
    iiagent_application_interface_runtime *, const char *principal, const char *object_id,
    const char *capability_id, const char *input, char **out_text);
/* Runs a trusted synchronous procedure under the caller's separate procedure permission. */
int iiagent_application_interface_execute_procedure(
    iiagent_application_interface_runtime *, const char *principal, const char *procedure_id,
    const char *input, iiagent_application_interface_handler procedure, void *user, char **out_text);
/* Independently authorizes source compilation, module loading and procedure execution. */
int iiagent_application_interface_execute_source(
    iiagent_application_interface_runtime *, const char *principal, const char *procedure_id,
    const char *source, const iiagent_application_interface_source_backend *, const char *input, char **out_text);
/* Compile a complete module without loading or executing it; out_text receives diagnostics. */
int iiagent_application_interface_build_source(
    iiagent_application_interface_runtime *, const char *principal, const char *identifier,
    const char *source, const iiagent_application_interface_source_backend *,
    iiagent_application_interface_compiled_procedure **out_procedure, char **out_text);
/* Assemble and compile an agent draft without loading or executing it. */
int iiagent_application_interface_build_procedure(
    iiagent_application_interface_runtime *, const char *principal, const char *identifier,
    const char *preamble, const char *body, const iiagent_application_interface_source_backend *,
    iiagent_application_interface_compiled_procedure **out_procedure, char **out_text);
/* Reauthorize loading and execution each time an existing artifact is used. */
int iiagent_application_interface_execute_built(
    iiagent_application_interface_runtime *, const char *principal,
    iiagent_application_interface_compiled_procedure *, const char *input, char **out_text);
/* Release a compiled artifact outside active calls and callbacks; null is accepted. */
void iiagent_application_interface_destroy_compiled(iiagent_application_interface_compiled_procedure *);
/* Subscribes a callback; zero indicates failure. Callback and user data must remain valid. */
uint64_t iiagent_application_interface_subscribe(
    iiagent_application_interface_runtime *, const char *principal,
    iiagent_application_interface_observer observer, void *user);
/* Unsubscribes idempotently. The host retains callback data until active callbacks finish. */
void iiagent_application_interface_unsubscribe(
    iiagent_application_interface_runtime *, uint64_t subscription);

#ifdef __cplusplus
}
#endif
