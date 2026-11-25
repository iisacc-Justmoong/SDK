#include <iiAgentApplicationInterface/AgentC.h>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <string.h>

/* A policy sees the requested operation and the command's declared permission. */
static int policy(void *user, const char *principal, int action, const char *object,
                  const char *capability, const char *const *permissions, size_t count) {
    (void)user; (void)object; (void)capability;
    if (strcmp(principal, "alice") != 0) return 0;
    if (action == IIAGENTAPPLICATIONINTERFACE_EXECUTE) return count == 1 && strcmp(permissions[0], "write") == 0;
    return 1;
}
/* Reject an empty argument and leave an owned diagnostic for the caller to copy. */
static int validate(void *user, const char *input, const char **reason) {
    (void)user; *reason = "empty"; return input[0] != '\0';
}
/* The host's actual command returns static storage valid throughout the native call. */
static int execute(void *user, const char *principal, const char *input, const char **output) {
    int *calls = (int *)user; (void)principal; (void)input; ++*calls; *output = "done"; return 0;
}
/* Run a trusted source procedure once under its authorized principal. */
static int procedure(void *user, const char *principal, const char *input, const char **output) {
    int *calls = (int *)user;
    assert(strcmp(principal, "alice") == 0 && strcmp(input, "input") == 0);
    ++*calls;
    *output = "procedure done";
    return 0;
}
/* Record each event after the runtime checks observer access. */
static void observe(void *user, int kind, const char *object, const char *capability,
                    uint64_t instance, uint64_t revision) {
    int *count = (int *)user; (void)kind; (void)object; (void)capability; (void)instance; (void)revision; ++*count;
}
/* Count callback-backed artifacts independently of a compiler implementation. */
struct source_counts { int compiles, loads, artifact_releases, module_releases; };
static int compile_source(void *user, const char *identifier, const char *source, uint64_t *artifact, const char **diagnostic) {
    struct source_counts *counts = (struct source_counts *)user;
    assert(strcmp(identifier, "agent.procedure") == 0 && strstr(source, "return {"));
    ++counts->compiles; *artifact = 17; *diagnostic = "compiled"; return 0;
}
static int load_source(void *user, uint64_t artifact, uint64_t *module, const void **descriptor, const char **diagnostic) {
    struct source_counts *counts = (struct source_counts *)user;
    assert(artifact == 17); ++counts->loads; *module = 0; *descriptor = NULL; *diagnostic = "load failed"; return 1;
}
static void release_artifact(void *user, uint64_t handle) {
    struct source_counts *counts = (struct source_counts *)user; assert(handle == 17); ++counts->artifact_releases;
}
static void release_module(void *user, uint64_t handle) {
    struct source_counts *counts = (struct source_counts *)user; (void)handle; ++counts->module_releases;
}
int main(void) {
    iiagent_application_interface_runtime *runtime = iiagent_application_interface_create(policy, NULL);
    assert(runtime != NULL);
    int calls = 0, events = 0;
    assert(iiagent_application_interface_add_object(runtime, "doc", "document", "title", "old") == IIAGENTAPPLICATIONINTERFACE_OK);
    assert(iiagent_application_interface_subscribe(runtime, "alice", observe, &events) != 0);
    const char *permissions[] = {"write"};
    assert(iiagent_application_interface_add_capability(runtime, "doc", "rename", "rename", "{}", "{}", permissions, 1,
                                  validate, execute, &calls) == IIAGENTAPPLICATIONINTERFACE_OK);
    char *output = NULL;
    assert(iiagent_application_interface_invoke(runtime, "alice", "doc", "rename", "", &output) == IIAGENTAPPLICATIONINTERFACE_VALIDATION_FAILED);
    assert(strcmp(output, "empty") == 0); iiagent_application_interface_free(output); output = NULL;
    assert(iiagent_application_interface_invoke(runtime, "bob", "doc", "rename", "x", &output) == IIAGENTAPPLICATIONINTERFACE_DENIED);
    iiagent_application_interface_free(output); output = NULL;
    assert(calls == 0);
    assert(iiagent_application_interface_invoke(runtime, "alice", "doc", "rename", "x", &output) == IIAGENTAPPLICATIONINTERFACE_OK);
    assert(strcmp(output, "done") == 0 && calls == 1 && events == 2);
    iiagent_application_interface_free(output); output = NULL;
    int procedure_calls = 0;
    assert(iiagent_application_interface_execute_procedure(runtime, "bob", "example", "input",
        procedure, &procedure_calls, &output) == IIAGENTAPPLICATIONINTERFACE_DENIED);
    iiagent_application_interface_free(output); output = NULL;
    assert(procedure_calls == 0);
    assert(iiagent_application_interface_execute_procedure(runtime, "alice", "example", "input",
        procedure, &procedure_calls, &output) == IIAGENTAPPLICATIONINTERFACE_OK);
    assert(procedure_calls == 1 && strcmp(output, "procedure done") == 0);
    iiagent_application_interface_free(output); output = NULL;
    assert(iiagent_application_interface_object(runtime, "bob", "doc", &output) == IIAGENTAPPLICATIONINTERFACE_OK && strcmp(output, "null") == 0);
    iiagent_application_interface_free(output); output = NULL;
    assert(iiagent_application_interface_procedure_authoring_context(runtime, "alice", &output) == IIAGENTAPPLICATIONINTERFACE_OK);
    assert(strstr(output, "procedure-authoring.v1") && strstr(output, "rename") && strstr(output, "doc"));
    iiagent_application_interface_free(output); output = NULL;
    assert(iiagent_application_interface_compose_procedure_source(runtime, "agent.procedure", "", "return {};", &output) == IIAGENTAPPLICATIONINTERFACE_OK);
    assert(strstr(output, "iiagent_application_procedure_v1") && strstr(output, "return {};"));
    iiagent_application_interface_free(output); output = NULL;
    struct source_counts source = {0};
    iiagent_application_interface_source_backend backend = {sizeof(backend), compile_source, load_source,
                                                            release_artifact, release_module, &source};
    iiagent_application_interface_compiled_procedure *built = NULL;
    assert(iiagent_application_interface_build_procedure(runtime, "alice", "agent.procedure", "", "return {};",
        &backend, &built, &output) == IIAGENTAPPLICATIONINTERFACE_OK);
    assert(built && source.compiles == 1 && source.loads == 0 && strcmp(output, "compiled") == 0);
    iiagent_application_interface_free(output); output = NULL;
    assert(iiagent_application_interface_execute_built(runtime, "alice", built, "", &output) == IIAGENTAPPLICATIONINTERFACE_HANDLER_FAILED);
    assert(source.loads == 1 && source.artifact_releases == 0);
    iiagent_application_interface_free(output); output = NULL;
    iiagent_application_interface_destroy_compiled(built);
    assert(source.artifact_releases == 1 && source.module_releases == 0);
    iiagent_application_interface_destroy(runtime);
    return 0;
}
