#include <iiAgentApplicationInterface>

#include <memory>
#include <string>
#include <utility>

/// A small application API whose existing object graph is used directly by a procedure.
class Application final {
public:
    /// Change the document title through an ordinary application method.
    void rename(std::string value) { title_ = std::move(value); }
    /// Read the current title for the procedure's result.
    [[nodiscard]] const std::string& title() const noexcept { return title_; }
private:
    std::string title_ = "Draft";
};

/// Show a host binding its root object and running one source-level native procedure.
int main() {
    using namespace iiAgentApplicationInterface;
    Runtime runtime([](const AccessRequest& request) { return request.context.principal == "local-agent"; });
    auto app = std::make_shared<Application>();
    if (runtime.add_object({"application", "Application", "Live application", ""}) != Status::ok ||
        runtime.bind_live_object("application", app) != Status::ok) return 1;
    const auto result = runtime.execute_procedure({"local-agent"}, "rename-document",
        [](ProcedureContext& context, std::string_view title) {
            auto application = context.write_object<Application>("application");
            if (!application) return Result{Status::denied, "application unavailable"};
            application->rename(std::string(title));
            return Result{Status::ok, application->title()};
        }, "Project notes");
    return result.status == Status::ok && app->title() == "Project notes" ? 0 : 1;
}
