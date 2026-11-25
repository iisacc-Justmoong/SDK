#include <iiAgentApplicationInterface>
#include <iiSDKCore/Runtime/EventLoop.h>
#include "iiNetworkExternalBridge.h"
#include "Document.h"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
#include <iostream>
#include <memory>
#include <spawn.h>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

extern char** environ;
using namespace iiAgentApplicationInterface;

/// A compiler output owned by the application host until the agent call completes.
struct Artifact final : SourceArtifact {
    explicit Artifact(std::string value) : path(std::move(value)) {}
    ~Artifact() override { std::remove(path.c_str()); }
    std::string path;
};
/// A procedure module remains loaded while it accesses the live app object.
struct Module final : LoadedSourceProcedure {
    Module(void* handle, const NativeProcedureDescriptor* value) : library(handle), entry(value) {}
    ~Module() override { if (library) dlclose(library); }
    const NativeProcedureDescriptor* descriptor() const noexcept override { return entry; }
    void* library;
    const NativeProcedureDescriptor* entry;
};
/// Application-owned POSIX compiler/loader used only by this integration test host.
class Compiler final : public SourceProcedureBackend {
public:
    Compiler(std::string compiler, std::string output, std::string library, std::string module, std::string stage)
        : compiler_(std::move(compiler)), output_(std::move(output)), library_(std::move(library)),
          module_(std::move(module)), stage_(std::move(stage)) {}
    SourceCompilation compile(std::string_view, std::string_view source) override {
        int pipes[2];
        if (pipe(pipes) != 0) return {Status::handler_failed, "pipe failed", {}};
        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions, pipes[0], STDIN_FILENO);
        posix_spawn_file_actions_addclose(&actions, pipes[1]);
        std::vector<std::string> args{compiler_, "-std=c++23", "-shared", "-fPIC",
            "-I" + stage_ + "/include", "-I" + stage_ + "/include/iisdk", "-I" + module_ + "/resources/external_host",
            "-x", "c++", "-", "-x", "none", library_, "-Wl,-rpath," + stage_ + "/lib", "-o", output_};
        std::vector<char*> argv;
        for (auto& arg : args) argv.push_back(arg.data());
        argv.push_back(nullptr);
        pid_t child{};
        const int launched = posix_spawnp(&child, compiler_.c_str(), &actions, nullptr, argv.data(), environ);
        posix_spawn_file_actions_destroy(&actions); close(pipes[0]);
        if (launched != 0) { close(pipes[1]); return {Status::handler_failed, "compiler start failed", {}}; }
        std::size_t written = 0;
        while (written < source.size()) {
            const auto amount = write(pipes[1], source.data() + written, source.size() - written);
            if (amount < 0 && errno == EINTR) continue;
            if (amount <= 0) break;
            written += static_cast<std::size_t>(amount);
        }
        close(pipes[1]);
        int status = 0; pid_t waited;
        do { waited = waitpid(child, &status, 0); } while (waited < 0 && errno == EINTR);
        if (waited != child || written != source.size() || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            std::remove(output_.c_str()); return {Status::handler_failed, "compilation failed", {}};
        }
        return {Status::ok, "", std::make_unique<Artifact>(output_)};
    }
    SourceLoad load(const SourceArtifact& value) override {
        const auto* artifact = dynamic_cast<const Artifact*>(&value);
        if (!artifact) return {Status::invalid_argument, "wrong artifact", {}};
        void* library = dlopen(artifact->path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!library) return {Status::handler_failed, "module load failed", {}};
        const auto provider = reinterpret_cast<NativeProcedureDescriptorEntry>(dlsym(library, native_procedure_entry_symbol.data()));
        const auto* descriptor = provider ? provider() : nullptr;
        if (!descriptor) { dlclose(library); return {Status::handler_failed, "missing descriptor", {}}; }
        return {Status::ok, "", std::make_unique<Module>(library, descriptor)};
    }
private:
    std::string compiler_, output_, library_, module_, stage_;
};

/// Run a real iiNetwork application loop and expose only authenticated agent operations.
int main(int argc, char** argv) {
    if (argc != 6) return 2;
    const char* secret = std::getenv("IIAGENT_TEST_CREDENTIAL");
    if (!secret || !*secret) return 3;
    const std::string expected(secret);
    std::signal(SIGPIPE, SIG_IGN);
    ii::runtime::EventLoop loop;
    Runtime runtime([](const AccessRequest& request) {
        return request.context.principal == "approved-agent" && request.object_id != "hidden";
    });
    auto document = runtime.make_tracked_object<external_host_fixture::Document>(
        {"document", "external_host_fixture::Document", "Live document", "initial"});
    if (document.status != Status::ok || runtime.add_object({"hidden", "secret", "", "confidential"}) != Status::ok ||
        runtime.add_object({"control", "control", "test host", ""}) != Status::ok) return 4;
    auto validate = [](std::string_view, std::string&) { return true; };
    if (runtime.add_capability({"document", "rename", "Rename document", "text", "text", {"write"}}, validate,
        [&](const AgentContext&, std::string_view input) {
            document.object->rename(std::string(input));
            return Result{runtime.update_state("document", document.object->title()), document.object->title()};
        }) != Status::ok) return 5;
    if (runtime.add_capability({"document", "publish", "Publish current title", "text", "text", {"write"}}, validate,
        [&](const AgentContext&, std::string_view) {
            return Result{runtime.update_state("document", document.object->title()), document.object->title()};
        }) != Status::ok) return 6;
    if (runtime.add_capability({"control", "shutdown", "Stop test host", "", "", {}}, validate,
        [&](const AgentContext&, std::string_view) {
            const auto submitted = loop.after(std::chrono::milliseconds(300), [&] { loop.request_stop(); });
            return Result{submitted ? Status::ok : Status::handler_failed, "stopping"};
        }) != Status::ok) return 7;
    Compiler compiler(argv[2], argv[3], argv[4], argv[5], std::string(argv[5]) + "/build/external-package");
    ExternalGateway gateway(runtime, [&](const ExternalPeer& peer, std::string_view credential) -> std::optional<AgentContext> {
        // Fixture policy only: real applications must use their own approval store and verifier.
        if (!peer.user_id || *peer.user_id != static_cast<std::uint64_t>(geteuid()) || credential != expected) return std::nullopt;
        return AgentContext{"approved-agent"};
    }, &compiler);
    integration::LocalAgentBridge bridge(loop, gateway);
    std::string diagnostic;
    if (!bridge.listen(argv[1], diagnostic)) { std::cerr << diagnostic << '\n'; return 8; }
    std::cout << "READY" << std::endl;
    loop.run();
    bridge.close();
    return 0;
}
