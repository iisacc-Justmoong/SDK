#include "Command.h"
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/stl/filesystem.h>

namespace py = pybind11;
using namespace iiAgentApplicationInterface;

PYBIND11_MODULE(iiagent_application_commands, m) {
    m.doc() = "Native application command contracts and transport adapters";
    py::enum_<Status>(m, "Status")
        .value("OK", Status::ok).value("INVALID_ARGUMENT", Status::invalid_argument)
        .value("NOT_FOUND", Status::not_found).value("ALREADY_EXISTS", Status::already_exists)
        .value("DENIED", Status::denied).value("VALIDATION_FAILED", Status::validation_failed)
        .value("HANDLER_FAILED", Status::handler_failed).value("INTERNAL_ERROR", Status::internal_error).value("CANCELLED", Status::cancelled);
    py::class_<Object>(m, "Object").def(py::init<>())
        .def_readwrite("id", &Object::id).def_readwrite("type", &Object::type)
        .def_readwrite("description", &Object::description).def_readwrite("state", &Object::state)
        .def_readwrite("instance", &Object::instance).def_readwrite("revision", &Object::revision);
    py::class_<ApplicationCommandDescriptor>(m, "ApplicationCommandDescriptor").def(py::init<>())
        .def_readwrite("name", &ApplicationCommandDescriptor::name).def_readwrite("target_id", &ApplicationCommandDescriptor::target_id)
        .def_readwrite("description", &ApplicationCommandDescriptor::description).def_readwrite("input_schema", &ApplicationCommandDescriptor::input_schema)
        .def_readwrite("output_schema", &ApplicationCommandDescriptor::output_schema).def_readwrite("required_scopes", &ApplicationCommandDescriptor::required_scopes)
        .def_readwrite("capability_id", &ApplicationCommandDescriptor::capability_id).def_readwrite("opaque_input", &ApplicationCommandDescriptor::opaque_input);
    py::class_<ApplicationCommandInvocation>(m, "ApplicationCommandInvocation").def(py::init<>())
        .def_readwrite("request_id", &ApplicationCommandInvocation::request_id).def_readwrite("command", &ApplicationCommandInvocation::command)
        .def_readwrite("target_id", &ApplicationCommandInvocation::target_id).def_readwrite("arguments_json", &ApplicationCommandInvocation::arguments_json)
        .def_readwrite("principal", &ApplicationCommandInvocation::principal).def_readwrite("capability_id", &ApplicationCommandInvocation::capability_id);
    py::class_<ApplicationCommandResult>(m, "ApplicationCommandResult").def(py::init<>())
        .def_readwrite("status", &ApplicationCommandResult::status).def_readwrite("output_json", &ApplicationCommandResult::output_json)
        .def_readwrite("diagnostic", &ApplicationCommandResult::diagnostic).def_readwrite("cancelled", &ApplicationCommandResult::cancelled);
    py::class_<CommandProgressEvent>(m, "CommandProgressEvent").def(py::init<>())
        .def_readwrite("request_id", &CommandProgressEvent::request_id).def_readwrite("token_json", &CommandProgressEvent::token_json)
        .def_readwrite("progress", &CommandProgressEvent::progress).def_readwrite("total", &CommandProgressEvent::total)
        .def_readwrite("message", &CommandProgressEvent::message);
    py::class_<CommandGrant>(m, "CommandGrant").def(py::init<>())
        .def_readwrite("principal", &CommandGrant::principal).def_readwrite("target_id", &CommandGrant::target_id)
        .def_readwrite("command", &CommandGrant::command).def_readwrite("scopes", &CommandGrant::scopes);
    py::class_<CommandTargetResolution>(m, "CommandTargetResolution").def_readonly("status", &CommandTargetResolution::status)
        .def_readonly("object_id", &CommandTargetResolution::object_id);
    py::class_<CommandTargetResolver>(m, "CommandTargetResolver").def(py::init<>())
        .def("execute", [](const CommandTargetResolver& self, const ApplicationCommandInvocation& call, const std::vector<Object>& objects) {
            return self.execute(call, objects);
        });
    py::class_<CommandScopePolicy>(m, "CommandScopePolicy").def(py::init<>()).def("execute", &CommandScopePolicy::execute);
    py::class_<CommandSchemaResource>(m, "CommandSchemaResource").def(py::init<>())
        .def_readwrite("uri", &CommandSchemaResource::uri).def_readwrite("schema_json", &CommandSchemaResource::schema_json);
    py::class_<CommandSchemaValidationRequest>(m, "CommandSchemaValidationRequest").def(py::init<>())
        .def_readwrite("schema_json", &CommandSchemaValidationRequest::schema_json)
        .def_readwrite("instance_json", &CommandSchemaValidationRequest::instance_json)
        .def_readwrite("resources", &CommandSchemaValidationRequest::resources)
        .def_readwrite("assert_formats", &CommandSchemaValidationRequest::assert_formats)
        .def_readwrite("default_dialect", &CommandSchemaValidationRequest::default_dialect);
    py::class_<CommandSchemaValidationResult>(m, "CommandSchemaValidationResult")
        .def_readonly("status", &CommandSchemaValidationResult::status).def_readonly("valid", &CommandSchemaValidationResult::valid)
        .def_readonly("diagnostic", &CommandSchemaValidationResult::diagnostic);
    py::class_<CommandSchemaValidator>(m, "CommandSchemaValidator").def(py::init<>())
        .def("execute", [](const CommandSchemaValidator& self, CommandSchemaValidationRequest request) {
            py::gil_scoped_release release; return self.execute(request);
        });
    py::class_<CommandPublication>(m, "CommandPublication").def(py::init<>())
        .def_readwrite("descriptor", &CommandPublication::descriptor).def_readwrite("allowed", &CommandPublication::allowed);
    py::class_<CommandCapabilityPublisher>(m, "CommandCapabilityPublisher").def(py::init<>()).def("execute", &CommandCapabilityPublisher::execute);
    py::class_<CommandCancellationHandle>(m, "CommandCancellationHandle").def(py::init<>())
        .def("execute", &CommandCancellationHandle::execute)
        .def_property_readonly("cancelled", [](const CommandCancellationHandle& flag) { return flag.token().stop_requested(); });
    py::class_<CliCommandOutput>(m, "CliCommandOutput").def_readonly("status", &CliCommandOutput::status)
        .def_readwrite("invocation", &CliCommandOutput::invocation).def_readonly("diagnostic", &CliCommandOutput::diagnostic);
    py::class_<CliCommandAdapter>(m, "CliCommandAdapter").def(py::init<>()).def("execute", &CliCommandAdapter::execute);
    py::enum_<McpCommandOperation>(m, "McpCommandOperation")
        .value("REQUEST", McpCommandOperation::request).value("RESULT", McpCommandOperation::result).value("PROGRESS", McpCommandOperation::progress);
    py::class_<McpCommandInput>(m, "McpCommandInput").def(py::init<>())
        .def_readwrite("operation", &McpCommandInput::operation).def_readwrite("message", &McpCommandInput::message)
        .def_readwrite("request_id_json", &McpCommandInput::request_id_json).def_readwrite("published", &McpCommandInput::published)
        .def_readwrite("result", &McpCommandInput::result).def_readwrite("progress", &McpCommandInput::progress);
    py::class_<McpCommandOutput>(m, "McpCommandOutput").def_readonly("status", &McpCommandOutput::status)
        .def_readonly("response", &McpCommandOutput::response).def_readonly("request_id_json", &McpCommandOutput::request_id_json)
        .def_readonly("cancellation_id_json", &McpCommandOutput::cancellation_id_json).def_readonly("progress_token_json", &McpCommandOutput::progress_token_json)
        .def_readonly("invocation", &McpCommandOutput::invocation);
    py::class_<McpCommandAdapter>(m, "McpCommandAdapter").def(py::init<>()).def("execute", &McpCommandAdapter::execute);
    py::class_<IsolatedCommandRequest>(m, "IsolatedCommandRequest").def(py::init<>())
        .def_property("arguments", [](const IsolatedCommandRequest& r) { return r.launch.arguments; },
                      [](IsolatedCommandRequest& r, std::vector<std::string> v) { r.launch.arguments = std::move(v); })
        .def_property("working_directory", [](const IsolatedCommandRequest& r) { return r.launch.working_directory; },
                      [](IsolatedCommandRequest& r, std::filesystem::path v) { r.launch.working_directory = std::move(v); })
        .def_property("environment", [](const IsolatedCommandRequest& r) { return r.launch.environment; },
                      [](IsolatedCommandRequest& r, std::map<std::string, std::optional<std::string>, std::less<>> v) {
                          r.launch.environment = std::move(v);
                      })
        .def_readwrite("input", &IsolatedCommandRequest::input).def_readwrite("output_limit", &IsolatedCommandRequest::output_limit)
        .def_readwrite("error_limit", &IsolatedCommandRequest::error_limit);
    py::class_<IsolatedCommandJob>(m, "IsolatedCommandJob")
        .def_property_readonly("cancellation", [](IsolatedCommandJob& job) -> CommandCancellationHandle& { return job.cancellation; }, py::return_value_policy::reference_internal)
        .def("started", [](IsolatedCommandJob& job) { py::gil_scoped_release release; return job.started.get(); })
        .def("result", [](IsolatedCommandJob& job) { py::gil_scoped_release release; return job.result.get(); });
    m.def("start_isolated_command", [](IsolatedCommandRequest request) { return start_isolated_command(std::move(request)); });
    m.def("start_isolated_command", [](IsolatedCommandRequest request, CommandCancellationHandle cancellation) {
        return start_isolated_command(std::move(request), std::move(cancellation));
    }, "Start a caller-authorized child with an explicit shared cancellation signal.");
}
