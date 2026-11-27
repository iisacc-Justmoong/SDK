#ifdef NDEBUG
#undef NDEBUG
#endif
#include <iiAgentApplicationInterface/Command.h>
#include <cassert>
#include <iostream>
#include <thread>
#include <atomic>
using namespace iiAgentApplicationInterface;

int main(int argc, char** argv) {
    assert(argc == 3);
    ApplicationCommandDescriptor command{"rename", "doc", "Rename", R"({"type":"object","properties":{"name":{"type":"string","minLength":1}},"required":["name"],"additionalProperties":false})", R"({"type":"object"})", {"document.write"}};
    ApplicationCommandInvocation call{"req-1", "rename", "doc", R"({"name":"New"})", "alice"};
    const std::vector<Object> targets{{"doc", "document", {}, {}, 1, 0}, {"other", "document", {}, {}, 2, 0}};
    auto target = CommandTargetResolver{}.execute(call, targets);
    assert(target.status == Status::ok && target.object_id == "doc");
    auto missing = call; missing.target_id = "missing";
    assert(CommandTargetResolver{}.execute(missing, targets).status == Status::not_found);
    CommandGrant grant{"alice", "doc", "rename", {"document.write"}};
    assert(CommandScopePolicy{}.execute(command, call, grant));
    grant.principal = "mallory";
    assert(!CommandScopePolicy{}.execute(command, call, grant));
    grant.principal = "alice"; grant.scopes.clear();
    assert(!CommandScopePolicy{}.execute(command, call, grant));
    auto visible = CommandCapabilityPublisher{}.execute({{command, true}, {command, false}});
    assert(visible.size() == 1 && visible.front().name == "rename");
    CommandSchemaValidator validator;
    auto validated = validator.execute({command.input_schema, call.arguments_json, {}, false});
    assert(validated.status == Status::ok && validated.valid);
    auto rejected = validator.execute({command.input_schema, R"({"name":""})", {}, false});
    assert(rejected.status == Status::validation_failed && !rejected.valid);
    auto schema = validator.execute({R"({"$schema":"https://json-schema.org/draft/2020-12/schema","$ref":"https://example.invalid/defs"})", "2", {{"https://example.invalid/defs", R"({"type":"integer","minimum":2})"}}, false});
    assert(schema.status == Status::ok && schema.valid);
    auto fragment = validator.execute({R"({"$ref":"https://example.invalid/defs#/$defs/number"})", "2", {{"https://example.invalid/defs", R"({"$defs":{"number":{"type":"integer"}}})"}}, false});
    assert(fragment.status == Status::ok && fragment.valid);
    assert(validator.execute({R"({"$ref":"https://missing.invalid/a"})", "2", {}, false}).status != Status::ok);
    auto old = validator.execute({R"({"$schema":"http://json-schema.org/draft-04/schema#","type":"number","minimum":3,"exclusiveMinimum":true})", "3", {}, false});
    assert(old.status == Status::validation_failed);
    auto draft3 = validator.execute({R"({"$schema":"http://json-schema.org/draft-03/schema#","properties":{"name":{"type":"string","required":true}}})", "{}", {}, false});
    assert(draft3.status == Status::validation_failed);
    auto draft2 = validator.execute({R"({"$schema":"http://json-schema.org/draft-02/schema#","properties":{"name":{"type":"string"},"tag":{"type":"string","optional":true}}})", R"({"name":"x"})", {}, false});
    assert(draft2.status == Status::ok && draft2.valid);
    auto draft1 = validator.execute({R"({"$schema":"http://json-schema.org/draft-01/schema#","type":"number","minimum":3,"minimumCanEqual":false})", "3", {}, false});
    assert(draft1.status == Status::validation_failed);
    auto draft0 = validator.execute({R"({"$schema":"http://json-schema.org/draft-00/schema#","properties":{"id":{"type":"integer"}}})", "{}", {}, false});
    assert(draft0.status == Status::validation_failed);
    auto old_dependency = validator.execute({R"({"$schema":"http://json-schema.org/draft-02/schema#","properties":{"state":{"optional":true},"town":{"optional":true,"requires":"state"}}})", R"({"town":"A"})", {}, false});
    assert(old_dependency.status == Status::validation_failed);
    auto old_tuple = validator.execute({R"({"$schema":"http://json-schema.org/draft-01/schema#","items":[{"type":"integer"}],"additionalProperties":false})", "[1,2]", {}, false});
    assert(old_tuple.status == Status::validation_failed);
    auto old_decimal = validator.execute({R"({"$schema":"http://json-schema.org/draft-00/schema#","maxDecimal":2})", "1.234", {}, false});
    assert(old_decimal.status == Status::validation_failed);
    assert(validator.execute({R"({"$schema":"http://json-schema.org/draft-02/schema#","uniqueItems":true})", "[1,1]", {}, false}).status == Status::validation_failed);
    assert(validator.execute({R"({"$schema":"http://json-schema.org/draft-01/schema#","uniqueItems":true})", "[1,1]", {}, false}).valid);
    auto standard_meta = validator.execute({R"({"$schema":"http://json-schema.org/draft-03/schema#","$ref":"http://json-schema.org/draft-03/schema#"})", R"({"items":{"type":1}})", {}, false});
    assert(standard_meta.status == Status::validation_failed);
    for (const auto* old_format : {"date", "time", "host-name", "ip-address", "regex"}) {
        std::string format_schema = std::string(R"({"$schema":"http://json-schema.org/draft-03/schema#","format":")") + old_format + R"("})";
        const std::string value = std::string(old_format) == "date" ? R"("2021-02-29")" :
            std::string(old_format) == "time" ? R"("25:00:00Z")" :
            std::string(old_format) == "host-name" ? R"("-invalid.example")" :
            std::string(old_format) == "ip-address" ? R"("999.1.1.1")" : R"("(")";
        assert(validator.execute({format_schema, value, {}, true}).status == Status::validation_failed);
    }
    assert(validator.execute({R"({"$schema":"http://json-schema.org/draft-03/schema#","format":"time"})",
                              R"("08:30:06")", {}, true}).valid);
    const std::string color_schema = R"({"$schema":"http://json-schema.org/draft-03/schema#","format":"color"})";
    assert(validator.execute({color_schema, R"("fuchsia")", {}, true}).valid);
    assert(validator.execute({color_schema, R"("#C89")", {}, true}).valid);
    assert(validator.execute({color_schema, R"json("rgb(255, 0, 12)")json", {}, true}).valid);
    assert(validator.execute({color_schema, R"("puce")", {}, true}).status == Status::validation_failed);
    assert(validator.execute({color_schema, R"("#00332520")", {}, true}).status == Status::validation_failed);
    auto cross_draft_format = validator.execute({R"({"$schema":"https://json-schema.org/draft/2020-12/schema","$ref":"https://example.invalid/old"})",
        R"("puce")", {{"https://example.invalid/old", color_schema}}, true});
    assert(cross_draft_format.status == Status::validation_failed);
    // Numbers on either side of a high-precision boundary must remain distinct.
    auto precise = validator.execute({R"({"maximum":972783798187987123879878123.18878137})",
                                      "972783798187987123879878123.188781371", {}, false});
    assert(precise.status == Status::validation_failed && !precise.valid);
    assert(validator.execute({R"({"maximum":972783798187987123879878123.18878137})",
                              "972783798187987123879878123.188781369", {}, false}).valid);
    assert(validator.execute({R"({"type":"integer"})", "18446744073709551615", {}, false}).valid);
    assert(validator.execute({R"({"type":"integer"})", "18446744073709551616", {}, false}).valid);
    assert(validator.execute({R"({"type":"integer"})", "9007199254740992.0", {}, false}).valid);
    assert(validator.execute({R"({"type":"string"})", R"("972783798187987123879878123.188781371")", {}, false}).valid);
    assert(validator.execute({R"({})", "1e-400", {}, false}).valid);
    assert(validator.execute({R"({"multipleOf":0.1})", "0.3", {}, false}).valid);
    assert(validator.execute({R"({"multipleOf":0.1})", "0.30000000000000000001", {}, false}).status == Status::validation_failed);
    assert(validator.execute({R"({"const":12345678910111213141516171819202122232425262728293031})",
                              "12345678910111213141516171819202122232425262728293031", {}, false}).valid);
    assert(validator.execute({R"({"const":12345678910111213141516171819202122232425262728293031})",
                              "12345678910111213141516171819202122232425262728293032", {}, false}).status == Status::validation_failed);
    const std::string exact_old = R"({"$schema":"http://json-schema.org/draft-03/schema#","type":"object","properties":{"value":{"type":"number","minimum":972783798187987123879878123.18878137},"shade":{"type":"string","format":"color"}}})";
    auto exact_reference = validator.execute({R"({"$schema":"https://json-schema.org/draft/2020-12/schema","$ref":"https://example.invalid/exact"})",
        R"({"value":972783798187987123879878123.188781371,"shade":"puce"})",
        {{"https://example.invalid/exact", exact_old}}, true});
    assert(exact_reference.status == Status::validation_failed);
    assert(validator.execute({R"({"$schema":"https://json-schema.org/draft/2020-12/schema","$ref":"https://example.invalid/exact"})",
        R"({"value":972783798187987123879878123.188781371,"shade":"fuchsia"})",
        {{"https://example.invalid/exact", exact_old}}, true}).valid);
    const std::string custom_meta = R"({"$schema":"https://json-schema.org/draft/2020-12/schema","$id":"https://example.invalid/meta","$vocabulary":{"https://json-schema.org/draft/2020-12/vocab/core":true,"https://json-schema.org/draft/2020-12/vocab/validation":true},"type":["object","boolean"]})";
    const std::string custom_schema = R"({"$schema":"https://example.invalid/meta","maximum":0.1234567890123456789})";
    assert(validator.execute({custom_schema, "0.1234567890123456788", {{"https://example.invalid/meta", custom_meta}}, false}).valid);
    assert(validator.execute({custom_schema, "0.1234567890123456790", {{"https://example.invalid/meta", custom_meta}}, false}).status == Status::validation_failed);
    CommandCancellationHandle cancellation;
    auto token = cancellation.token();
    assert(!token.stop_requested());
    std::thread cancel([&] { (void)cancellation.execute(); }); cancel.join();
    assert(token.stop_requested());
    auto cli = CliCommandAdapter{}.execute({"--target", "doc", "--command", "rename", "--arguments", R"({"name":"New"})"});
    assert(cli.status == Status::ok && cli.invocation.target_id == "doc" && cli.invocation.command == "rename");
    auto badcli = CliCommandAdapter{}.execute({"--target", "doc", "--target", "other"});
    assert(badcli.status == Status::invalid_argument);
    McpCommandInput input; input.message = R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})"; input.published = {command};
    auto list = McpCommandAdapter{}.execute(input);
    assert(list.status == Status::ok && list.response.find("rename") != std::string::npos);
    input.message = R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"rename","arguments":{"name":"New"}}})";
    auto invoke = McpCommandAdapter{}.execute(input);
    assert(invoke.status == Status::ok && invoke.invocation && invoke.invocation->target_id == "doc");
    input.message = R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"rename","arguments":{},"_meta":{"progressToken":{}}}})";
    auto bad_token = McpCommandAdapter{}.execute(input);
    assert(bad_token.status == Status::invalid_argument && !bad_token.invocation && !bad_token.response.empty());
    input.message = R"({"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"rename","arguments":{"value":972783798187987123879878123.188781371}}})";
    auto rounded_call = McpCommandAdapter{}.execute(input);
    assert(rounded_call.status == Status::ok && rounded_call.invocation &&
           rounded_call.invocation->arguments_json.find("972783798187987123879878123.188781371") != std::string::npos);
    Runtime runtime([](const AccessRequest&) { return true; });
    assert(runtime.add_object({"doc", "document"}) == Status::ok);
    std::atomic_bool entered{false};
    assert(runtime.add_cancellable_capability({"doc", "slow", "", "", "", {}},
        [](std::string_view, std::string&) { return true; },
        [&entered](const AgentContext&, std::string_view, std::stop_token token, const CommandProgressCallback& progress) {
            entered.store(true);
            if (progress) progress({"request", "\"token\"", 1, 2, "working"});
            while (!token.stop_requested()) std::this_thread::yield();
            return Result{Status::cancelled, "cancelled safely"};
        }) == Status::ok);
    CommandCancellationHandle running; bool progress_seen = false;
    std::thread canceller([&] { while (!entered.load()) std::this_thread::yield(); (void)running.execute(); });
    auto cooperative = runtime.invoke({"alice"}, "doc", "slow", "{}", running.token(),
        [&](const CommandProgressEvent& event) { progress_seen = event.progress == 1; });
    canceller.join();
    assert(cooperative.status == Status::cancelled && progress_seen);
    IsolatedCommandRequest isolated; isolated.launch.arguments = {argv[1]}; isolated.launch.working_directory = argv[2];
    auto job = start_isolated_command(std::move(isolated));
    assert(job.started.get() != 0);
    assert(job.cancellation.execute());
    assert(job.result.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    const auto process_result = job.result.get();
    assert(process_result.cancelled && process_result.status == Status::cancelled);
    std::cout << "command contracts passed\n";
}
