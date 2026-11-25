#pragma once

#include "Agent.h"

namespace iiAgentApplicationInterface {

/// Opaque compiler output; the backend owns its storage and cleanup policy.
class SourceArtifact {
public:
    /// Releases temporary compiler output after its loaded module is gone.
    virtual ~SourceArtifact() = default;
};

/// An application-loaded native module kept alive for the duration of execution.
class LoadedSourceProcedure {
public:
    /// Releases the module according to the host's lifetime policy.
    virtual ~LoadedSourceProcedure() = default;
    /// Returns its borrowed descriptor while this loaded module is alive.
    [[nodiscard]] virtual const NativeProcedureDescriptor* descriptor() const noexcept = 0;
};

/// Outcome of compiling a source procedure through an application backend.
struct SourceCompilation {
    Status status;
    std::string diagnostic;
    std::unique_ptr<SourceArtifact> artifact;
};

/// Outcome of loading a compiled procedure through an application backend.
struct SourceLoad {
    Status status;
    std::string diagnostic;
    std::unique_ptr<LoadedSourceProcedure> module;
};

/// Host-supplied compiler, artifact storage and module loader; the SDK owns no file I/O.
class SourceProcedureBackend {
public:
    /// Releases backend state after all artifacts and loaded modules are gone.
    virtual ~SourceProcedureBackend() = default;
    /// Compile trusted source for the named procedure using the application's build profile.
    [[nodiscard]] virtual SourceCompilation compile(std::string_view identifier, std::string_view source) = 0;
    /// Load a successful artifact. The caller authorizes loading before invoking this method.
    [[nodiscard]] virtual SourceLoad load(const SourceArtifact& artifact) = 0;
};

/// One compiler artifact whose identifier and originating backend cannot be replaced.
class CompiledProcedure final {
public:
    /// Releases the artifact while the application-provided backend is still alive.
    ~CompiledProcedure() = default;
    CompiledProcedure(const CompiledProcedure&) = delete;
    CompiledProcedure& operator=(const CompiledProcedure&) = delete;
    CompiledProcedure(CompiledProcedure&&) = delete;
    CompiledProcedure& operator=(CompiledProcedure&&) = delete;
    /// Returns the immutable identifier used for load and execution authorization.
    [[nodiscard]] std::string_view identifier() const noexcept { return identifier_; }
private:
    friend class Runtime;
    /// Binds an artifact to the exact host backend that created it.
    CompiledProcedure(std::string identifier, SourceProcedureBackend& backend, std::unique_ptr<SourceArtifact> artifact)
        : identifier_(std::move(identifier)), backend_(&backend), artifact_(std::move(artifact)) {}
    std::string identifier_;
    SourceProcedureBackend* backend_;
    std::unique_ptr<SourceArtifact> artifact_;
};

/// A compilation diagnostic and, on success, a reusable owned procedure.
struct ProcedureBuild {
    Status status;
    std::string diagnostic;
    std::unique_ptr<CompiledProcedure> procedure;
};

} // namespace iiAgentApplicationInterface
