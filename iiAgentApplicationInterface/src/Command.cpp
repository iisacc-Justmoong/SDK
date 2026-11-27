#include "Command.h"
#include "LegacyMetas.h"
#include "PrecisionSchema/PrecisionSchema.h"
#include <iiSDKCore/Process/Process.h>
#include <jsoncons/json.hpp>
#include <corvus_json_schema.hpp>
#include <colorm.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace iiAgentApplicationInterface {
namespace {
using Json = jsoncons::json;
namespace schema = corvus::json_schema;
constexpr std::size_t max_json_bytes = 2 * 1024 * 1024;
std::string dump(const Json& value) { std::ostringstream out; value.dump(out); return out.str(); }
// The primary engine uses fixed-width numbers. This conservative detector
// chooses the exact backend whenever distinct decimal values could collapse;
// quoted text is ignored and remains opaque application data.
bool exact_enough_number(std::string_view token) {
    std::size_t pos = 0;
    if (!token.empty() && token.front() == '-') ++pos;
    std::string digits; std::size_t fractional = 0; bool floating = false;
    auto digit = [](char c) { return c >= '0' && c <= '9'; };
    while (pos < token.size() && digit(token[pos])) digits.push_back(token[pos++]);
    if (pos < token.size() && token[pos] == '.') {
        floating = true; ++pos;
        while (pos < token.size() && digit(token[pos])) { digits.push_back(token[pos++]); ++fractional; }
    }
    int exponent = 0; bool exponent_negative = false;
    if (pos < token.size() && (token[pos] == 'e' || token[pos] == 'E')) {
        floating = true; ++pos;
        if (pos < token.size() && (token[pos] == '+' || token[pos] == '-')) exponent_negative = token[pos++] == '-';
        while (pos < token.size() && digit(token[pos])) {
            exponent = std::min(1000000, exponent * 10 + (token[pos++] - '0'));
        }
    }
    if (digits.size() > 10000 || exponent > 10000) throw std::invalid_argument("JSON number exceeds the resource limit");
    if (exponent_negative) exponent = -exponent;
    const auto first = digits.find_first_not_of('0');
    if (first == std::string::npos) return true;
    digits.erase(0, first);
    int scale = exponent - static_cast<int>(fractional);
    while (digits.size() > 1 && digits.back() == '0') { digits.pop_back(); ++scale; }
    auto at_most_integer = [&](std::string_view limit) {
        if (scale < 0 || digits.size() + static_cast<std::size_t>(scale) > limit.size()) return false;
        std::string value = digits; value.append(static_cast<std::size_t>(scale), '0');
        return value.size() < limit.size() || value <= limit;
    };
    if (floating) return false;
    return at_most_integer("9007199254740992");
}
bool needs_exact_precision(std::string_view text) {
    auto digit = [](char c) { return c >= '0' && c <= '9'; };
    bool result = false;
    for (std::size_t i = 0; i < text.size();) {
        if (text[i] == '"') {
            ++i;
            while (i < text.size() && text[i] != '"') {
                if (text[i] == '\\' && i + 1 < text.size()) ++i;
                ++i;
            }
            if (i < text.size()) ++i;
        } else if (text[i] == '-' || digit(text[i])) {
            const auto start = i;
            if (text[i] == '-') ++i;
            while (i < text.size() && digit(text[i])) ++i;
            if (i < text.size() && text[i] == '.') { ++i; while (i < text.size() && digit(text[i])) ++i; }
            if (i < text.size() && (text[i] == 'e' || text[i] == 'E')) {
                ++i; if (i < text.size() && (text[i] == '+' || text[i] == '-')) ++i;
                while (i < text.size() && digit(text[i])) ++i;
            }
            result |= !exact_enough_number(text.substr(start, i - start));
        } else ++i;
    }
    return result;
}
Json parse(std::string_view text) {
    if (text.size() > max_json_bytes) throw std::invalid_argument("JSON exceeds the byte limit");
    jsoncons::json_options options; options.max_nesting_depth(128).lossless_number(true).lossless_bignum(true);
    return Json::parse(std::string(text), options);
}
bool valid_id(const Json& id) { return id.is_string() || id.is_int64() || id.is_uint64(); }
std::string json_error(const Json& id, int code, std::string_view message) {
    Json value = Json::object(); value["jsonrpc"] = "2.0"; value["id"] = id;
    Json error = Json::object(); error["code"] = code; error["message"] = std::string(message);
    value["error"] = std::move(error); return dump(value);
}
Json response(const Json& id, Json result) {
    Json out = Json::object(); out["jsonrpc"] = "2.0"; out["id"] = id; out["result"] = std::move(result); return out;
}
bool tool_name(std::string_view name) {
    return !name.empty() && name.size() <= 128 && std::all_of(name.begin(), name.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
    });
}
// Translate the original property-oriented drafts into equivalent Draft 4
// constraints. The translator does not fetch schemas: every referenced document
// remains in the host-provided resource map. A depth bound prevents recursion
// through maliciously nested input before the schema engine sees it.
int legacy_version(std::string_view uri) {
    for (int version = 0; version <= 3; ++version) {
        for (const auto* suffix : {"schema", "hyper-schema", "links"}) {
            const std::string path = "json-schema.org/draft-0" + std::to_string(version) + "/" + suffix;
            const std::string prefix = "http://" + path, secure = "https://" + path;
            if (uri == prefix || uri == prefix + "#" || uri == secure || uri == secure + "#") return version;
        }
    }
    return -1;
}
std::uint32_t precise_draft(std::string_view uri) {
    if (legacy_version(uri) >= 0 || uri == "http://json-schema.org/draft-04/schema#" ||
        uri == "http://json-schema.org/draft-04/schema" || uri == "http://json-schema.org/draft-05/schema#" ||
        uri == "http://json-schema.org/draft-05/schema") return 4;
    if (uri == "http://json-schema.org/draft-06/schema#" || uri == "http://json-schema.org/draft-06/schema") return 6;
    if (uri == "http://json-schema.org/draft-07/schema#" || uri == "http://json-schema.org/draft-07/schema") return 7;
    if (uri == "https://json-schema.org/draft/2019-09/schema") return 2019;
    if (uri == "https://json-schema.org/draft/2020-12/schema") return 2020;
    return 0;
}
Json legacy_schema(const Json& input, int version, std::size_t depth);
Json legacy_type(const Json& input, int version, std::size_t depth) {
    if (depth > 128) throw std::invalid_argument("Legacy schema exceeds the nesting limit");
    if (input.is_string()) {
        Json value = Json::object();
        if (input.as<std::string>() != "any") value["type"] = input;
        return value;
    }
    if (input.is_object()) return legacy_schema(input, version, depth + 1);
    if (input.is_array()) {
        Json variants = Json::array();
        for (const auto& element : input.array_range()) {
            if (element.is_string() && element.as<std::string>() == "any") return Json::object();
            variants.push_back(legacy_type(element, version, depth + 1));
        }
        if (variants.empty()) throw std::invalid_argument("Legacy type array is empty");
        Json output = Json::object(); output["anyOf"] = std::move(variants); return output;
    }
    throw std::invalid_argument("Invalid legacy type constraint");
}
Json legacy_schema(const Json& input, int version, std::size_t depth) {
    if (depth > 128) throw std::invalid_argument("Legacy schema exceeds the nesting limit");
    if (!input.is_object()) throw std::invalid_argument("Legacy schema must be an object");
    if (input.contains("$schema") && input["$schema"].is_string()) {
        const auto dialect = input["$schema"].as<std::string>();
        const auto explicit_version = legacy_version(dialect);
        if (explicit_version < 0) return input;
        version = explicit_version;
    }
    Json output = Json::object(), required = Json::array(), dependencies = Json::object(), all = Json::array();
    for (const auto& member : input.object_range()) {
        const std::string key(member.key()); const auto& value = member.value();
        if (key == "$schema") { output[key] = "http://json-schema.org/draft-04/schema#"; }
        else if (key == "format" && value.is_string()) {
            const auto format = value.as<std::string>();
            if (format == "date" || format == "time" || format == "host-name" || format == "ip-address" ||
                format == "regex" || format == "color") output[key] = "iiagent-legacy-" + format;
            else output[key] = value;
        }
        else if (key == "type") { all.push_back(legacy_type(value, version, depth + 1)); }
        else if (key == "disallow") { Json negation = Json::object(); negation["not"] = legacy_type(value, version, depth + 1); all.push_back(std::move(negation)); }
        else if (key == "extends") {
            if (value.is_array()) for (const auto& element : value.array_range()) all.push_back(legacy_schema(element, version, depth + 1));
            else all.push_back(legacy_schema(value, version, depth + 1));
        }
        else if (key == "divisibleBy" && version >= 2) output["multipleOf"] = value;
        else if (key == "maxDecimal" && version <= 1) {
            if (!value.is_uint64() || value.as<std::uint64_t>() > 10000) throw std::invalid_argument("Invalid maxDecimal");
            output["multipleOf"] = parse("1e-" + std::to_string(value.as<std::uint64_t>()));
        }
        else if (key == "properties" && value.is_object()) {
            Json fields = Json::object();
            for (const auto& property : value.object_range()) {
                const auto& definition = property.value();
                if (!definition.is_object()) throw std::invalid_argument("Invalid property schema");
                bool mandatory = version <= 2;
                if (version <= 2 && definition.contains("optional")) {
                    if (!definition["optional"].is_bool()) throw std::invalid_argument("Invalid optional value");
                    mandatory = !definition["optional"].as<bool>();
                }
                if (version == 3 && definition.contains("required")) {
                    if (!definition["required"].is_bool()) throw std::invalid_argument("Invalid required value");
                    mandatory = definition["required"].as<bool>();
                }
                if (mandatory) required.push_back(std::string(property.key()));
                if (version <= 2 && definition.contains("requires")) {
                    const auto& dep = definition["requires"];
                    if (dep.is_string()) { Json names = Json::array(); names.push_back(dep); dependencies[property.key()] = std::move(names); }
                    else dependencies[property.key()] = legacy_schema(dep, version, depth + 1);
                }
                fields[property.key()] = legacy_schema(definition, version, depth + 1);
            }
            output[key] = std::move(fields);
        }
        else if ((key == "patternProperties" && version >= 3) || key == "definitions" || key == "$defs") {
            if (!value.is_object()) throw std::invalid_argument("Invalid schema mapping");
            Json fields = Json::object();
            for (const auto& property : value.object_range()) fields[property.key()] = legacy_schema(property.value(), version, depth + 1);
            output[key] = std::move(fields);
        }
        else if (key == "items") {
            if (value.is_array()) { Json items = Json::array(); for (const auto& item : value.array_range()) items.push_back(legacy_schema(item, version, depth + 1)); output[key] = std::move(items); }
            else output[key] = legacy_schema(value, version, depth + 1);
        }
        else if (key == "additionalProperties" || (key == "additionalItems" && version >= 3)) {
            output[key] = value.is_bool() ? value : legacy_schema(value, version, depth + 1);
            if (version <= 2 && key == "additionalProperties" && input.contains("items") && input["items"].is_array())
                output["additionalItems"] = value.is_bool() ? value : legacy_schema(value, version, depth + 1);
        }
        else if (key == "dependencies" && version == 3) {
            if (!value.is_object()) throw std::invalid_argument("Invalid dependencies");
            for (const auto& dependency : value.object_range()) {
                if (dependency.value().is_string()) { Json names = Json::array(); names.push_back(dependency.value()); dependencies[dependency.key()] = std::move(names); }
                else if (dependency.value().is_array()) dependencies[dependency.key()] = dependency.value();
                else dependencies[dependency.key()] = legacy_schema(dependency.value(), version, depth + 1);
            }
        }
        else if (key == "minimumCanEqual" && version <= 2) {
            if (!value.is_bool()) throw std::invalid_argument("Invalid minimumCanEqual");
            if (input.contains("minimum")) output["exclusiveMinimum"] = !value.as<bool>();
        }
        else if (key == "maximumCanEqual" && version <= 2) {
            if (!value.is_bool()) throw std::invalid_argument("Invalid maximumCanEqual");
            if (input.contains("maximum")) output["exclusiveMaximum"] = !value.as<bool>();
        }
        else if (key == "required" || key == "optional" || key == "requires") { /* Applied by the parent. */ }
        else if (key == "allOf" || key == "anyOf" || key == "oneOf" || key == "not" || key == "multipleOf" ||
                 key == "minProperties" || key == "maxProperties" || key == "contains" || key == "propertyNames" ||
                 key == "if" || key == "then" || key == "else" || key == "$dynamicRef" || key == "$dynamicAnchor" ||
                 (version <= 2 && key == "patternProperties") || (version <= 1 && key == "uniqueItems")) {
            // These are annotations in the selected old dialect; passing them to
            // Draft 4 could incorrectly turn them into validation constraints.
        } else output[key] = value;
    }
    if (!required.empty()) output["required"] = std::move(required);
    if (!dependencies.empty()) output["dependencies"] = std::move(dependencies);
    if (!all.empty()) output["allOf"] = std::move(all);
    return output;
}

// A per-call collection of immutable format programs; both schema backends
// use the same legacy rules. No host data or runtime resources are retained.
struct LegacyFormats final {
    std::vector<schema::validator> modern;
};
LegacyFormats make_legacy_formats() {
    LegacyFormats formats;
    schema::options options; options.default_dialect(schema::dialect::draft7).assert_format(true);
    for (const auto* name : {"date", "hostname", "ipv4", "regex"})
        formats.modern.push_back(schema::validator::compile(std::string("{\"format\":\"") + name + "\"}", options));
    return formats;
}
bool check_legacy_format(const LegacyFormats& formats, std::uint32_t id, std::string_view value) {
    if (id == 2) {
        auto digit = [](char c) { return c >= '0' && c <= '9'; };
        if (value.size() != 8 || value[2] != ':' || value[5] != ':' ||
            !digit(value[0]) || !digit(value[1]) || !digit(value[3]) ||
            !digit(value[4]) || !digit(value[6]) || !digit(value[7])) return false;
        const auto component = [&](std::size_t at) { return (value[at] - '0') * 10 + value[at + 1] - '0'; };
        return component(0) <= 23 && component(3) <= 59 && component(6) <= 59;
    }
    if (id == 6) {
        if (value.find('\0') != std::string_view::npos) return false;
        const auto first = value.find_first_not_of(" \t\r\n\f");
        if (first == std::string_view::npos) return false;
        const auto last = value.find_last_not_of(" \t\r\n\f");
        std::string color(value.substr(first, last - first + 1));
        std::string lower = color;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
            return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : static_cast<char>(c);
        });
        if (lower == "transparent" || lower == "currentcolor") return true;
        if (lower.front() == '#' && lower.size() != 4 && lower.size() != 7) return false;
        if (const auto open = lower.find('('); open != std::string::npos) {
            auto function = lower.substr(0, open);
            while (!function.empty() && function.back() == ' ') function.pop_back();
            if (function != "rgb" && function != "rgba" && function != "hsl" && function != "hsla") return false;
        }
        try { (void)colorm::Rgb(color); return true; }
        catch (const colorm::ParseError&) { return false; }
    }
    const std::size_t index = id == 1 ? 0 : id == 3 ? 1 : id == 4 ? 2 : id == 5 ? 3 : formats.modern.size();
    return index < formats.modern.size() && formats.modern[index].is_valid(dump(Json(std::string(value))));
}
int precise_format(void* context, std::uint32_t id, iiagent_precision_slice text) noexcept {
    try { return check_legacy_format(*static_cast<const LegacyFormats*>(context), id, {text.data, text.size}) ? 1 : 0; }
    catch (...) { return 0; }
}
} // namespace

CommandTargetResolution CommandTargetResolver::execute(const ApplicationCommandInvocation& invocation,
                                                        std::span<const Object> objects) const {
    if (invocation.target_id.empty()) return {Status::invalid_argument, {}};
    std::size_t found = 0;
    for (const auto& object : objects) if (object.id == invocation.target_id) ++found;
    if (found > 1) return {Status::invalid_argument, {}};
    if (!found) return {Status::not_found, {}};
    return {Status::ok, invocation.target_id};
}

bool CommandScopePolicy::execute(const ApplicationCommandDescriptor& descriptor,
                                 const ApplicationCommandInvocation& invocation,
                                 const CommandGrant& grant) const {
    if (invocation.principal.empty() || invocation.target_id.empty() || invocation.command.empty() ||
        descriptor.name != invocation.command || descriptor.target_id != invocation.target_id ||
        grant.principal != invocation.principal || grant.target_id != invocation.target_id || grant.command != invocation.command)
        return false;
    return std::all_of(descriptor.required_scopes.begin(), descriptor.required_scopes.end(), [&](const auto& scope) {
        return !scope.empty() && std::find(grant.scopes.begin(), grant.scopes.end(), scope) != grant.scopes.end();
    });
}

std::vector<ApplicationCommandDescriptor> CommandCapabilityPublisher::execute(std::vector<CommandPublication> input) const {
    std::vector<ApplicationCommandDescriptor> output;
    for (auto& item : input) if (item.allowed) output.push_back(std::move(item.descriptor));
    return output;
}

IsolatedCommandJob start_isolated_command(IsolatedCommandRequest request, CommandCancellationHandle cancellation) {
    std::promise<ApplicationCommandResult> promise;
    std::promise<std::uint64_t> started_promise;
    auto future = promise.get_future();
    auto started = started_promise.get_future();
    std::jthread worker([request = std::move(request), cancellation, promise = std::move(promise),
                         started_promise = std::move(started_promise)]
                        (std::stop_token stop) mutable {
        std::stop_callback on_stop(stop, [&cancellation] { (void)cancellation.execute(); });
        bool notified = false;
        try {
            if (request.launch.arguments.empty() || !std::filesystem::path(request.launch.arguments.front()).is_absolute() ||
                !request.launch.working_directory.is_absolute()) {
                started_promise.set_value(0); notified = true;
                promise.set_value({Status::invalid_argument, {}, "Process executable and working directory must be explicit; inherited resources are disabled", false}); return;
            }
            if (cancellation.token().stop_requested()) {
                started_promise.set_value(0); notified = true;
                promise.set_value({Status::cancelled, {}, "Cancelled before process launch", true}); return;
            }
            ii::process::Launch launch;
            launch.arguments = std::move(request.launch.arguments);
            launch.working_directory = std::move(request.launch.working_directory);
            launch.environment = std::move(request.launch.environment);
            launch.inherit_environment = false;
            auto child = ii::process::Process::start(launch);
            if (!child) {
                started_promise.set_value(0); notified = true;
                promise.set_value({Status::handler_failed, {}, child.error().message, false}); return;
            }
            started_promise.set_value(child->id()); notified = true;
            ii::process::CommunicationOptions options;
            options.stop = cancellation.token(); options.output_limit = request.output_limit; options.error_limit = request.error_limit;
#if defined(_WIN32)
            options.polling_interval = std::chrono::milliseconds(20);
#endif
            auto result = child->communicate(ii::core::byte_view(request.input), options);
            if (!result) { promise.set_value({Status::handler_failed, {}, result.error().message, false}); return; }
            auto as_string = [](const ii::core::Bytes& bytes) {
                return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            };
            const bool cancelled = result->exit.reason == ii::process::ExitReason::cancelled;
            if (result->output_truncated || result->error_truncated) {
                promise.set_value({cancelled ? Status::cancelled : Status::handler_failed, {}, "Process output exceeded its limit", cancelled}); return;
            }
            const bool success = result->exit.reason == ii::process::ExitReason::exited && result->exit.code == 0;
            promise.set_value({cancelled ? Status::cancelled : success ? Status::ok : Status::handler_failed, as_string(result->output),
                               as_string(result->error), cancelled});
        } catch (const std::exception& error) {
            if (!notified) started_promise.set_value(0);
            promise.set_value({Status::internal_error, {}, error.what(), false});
        } catch (...) {
            if (!notified) started_promise.set_value(0);
            promise.set_value({Status::internal_error, {}, "Process execution failed", false});
        }
    });
    return {std::move(cancellation), std::move(started), std::move(future), std::move(worker)};
}

CommandSchemaValidationResult CommandSchemaValidator::execute(const CommandSchemaValidationRequest& request) const {
    try {
        if (request.schema_json.size() > max_json_bytes || request.instance_json.size() > max_json_bytes)
            return {Status::invalid_argument, false, "JSON exceeds the byte limit"};
        bool precise = needs_exact_precision(request.instance_json);
        precise |= needs_exact_precision(request.schema_json);
        std::map<std::string, std::string, std::less<>> resources;
        for (const auto& resource : request.resources) {
            if (resource.uri.empty() || resource.uri.find('#') != std::string::npos ||
                resource.schema_json.size() > max_json_bytes || resources.contains(resource.uri))
                return {Status::invalid_argument, false, "Schema resource URIs must be unique document URIs"};
            resources.emplace(resource.uri, resource.schema_json);
            precise |= needs_exact_precision(resource.schema_json);
        }
        schema::options options;
        const auto& dialect = request.default_dialect;
        int legacy = legacy_version(dialect);
        if (legacy >= 0 || dialect == "http://json-schema.org/draft-05/schema#" || dialect == "http://json-schema.org/draft-05/schema")
            options.default_dialect(schema::dialect::draft4);
        else if (dialect == "http://json-schema.org/draft-04/schema#" || dialect == "http://json-schema.org/draft-04/schema")
            options.default_dialect(schema::dialect::draft4);
        else if (dialect == "http://json-schema.org/draft-06/schema#" || dialect == "http://json-schema.org/draft-06/schema")
            options.default_dialect(schema::dialect::draft6);
        else if (dialect == "http://json-schema.org/draft-07/schema#" || dialect == "http://json-schema.org/draft-07/schema")
            options.default_dialect(schema::dialect::draft7);
        else if (dialect == "https://json-schema.org/draft/2019-09/schema")
            options.default_dialect(schema::dialect::draft2019_09);
        else if (dialect == "https://json-schema.org/draft/2020-12/schema")
            options.default_dialect(schema::dialect::draft2020_12);
        else return {Status::invalid_argument, false, "Unsupported default JSON Schema dialect"};
        if (request.assert_formats) options.assert_format(true);
        auto source = parse(request.schema_json);
        bool alias5 = false;
        std::string selected_dialect = dialect;
        if (source.is_object() && source.contains("$schema") && source["$schema"].is_string()) {
            const auto selected = source["$schema"].as<std::string>();
            selected_dialect = selected;
            legacy = legacy_version(selected);
            if (selected == "http://json-schema.org/draft-05/schema#" || selected == "http://json-schema.org/draft-05/schema") {
                alias5 = true;
                source["$schema"] = "http://json-schema.org/draft-04/schema#";
            }
        }
        std::shared_ptr<LegacyFormats> formats;
        if (request.assert_formats) {
            formats = std::make_shared<LegacyFormats>(make_legacy_formats());
            for (const auto& [name, id] : {
                     std::pair{"iiagent-legacy-date", 1U}, std::pair{"iiagent-legacy-time", 2U},
                     std::pair{"iiagent-legacy-host-name", 3U}, std::pair{"iiagent-legacy-ip-address", 4U},
                     std::pair{"iiagent-legacy-regex", 5U}, std::pair{"iiagent-legacy-color", 6U}}) {
                options.format(name, [formats, id](std::string_view value) { return check_legacy_format(*formats, id, value); });
            }
        }
        std::string schema_text = legacy >= 0 ? dump(legacy_schema(source, legacy, 0)) : alias5 ? dump(source) : request.schema_json;
        auto resolve_resource = [&resources, legacy](std::string_view uri) -> std::optional<std::string> {
            auto document_uri = std::string(uri);
            if (const auto fragment = document_uri.find('#'); fragment != std::string::npos) document_uri.resize(fragment);
            auto found = resources.find(document_uri);
            auto builtin = detail::legacy_meta(document_uri);
            if (found == resources.end() && !builtin) return std::nullopt;
            std::string document = found != resources.end() ? found->second : std::string(*builtin);
            auto resource = parse(document);
            int version = legacy;
            if (resource.is_object() && resource.contains("$schema") && resource["$schema"].is_string()) {
                const auto selected = resource["$schema"].as<std::string>();
                version = legacy_version(selected);
                if (selected == "http://json-schema.org/draft-05/schema#" || selected == "http://json-schema.org/draft-05/schema") {
                    resource["$schema"] = "http://json-schema.org/draft-04/schema#";
                    return dump(resource);
                }
            }
            if (version < 0 && builtin) version = legacy_version(document_uri);
            if (version >= 0) return dump(legacy_schema(resource, version, 0));
            return document;
        };
        options.resolver(resolve_resource);
        // Translating a legacy maxDecimal synthesizes a fractional multipleOf
        // even when all original numbers are integers. Select the exact engine
        // after translation for both the root and caller-provided documents.
        precise |= needs_exact_precision(schema_text);
        for (const auto& [uri, document] : resources) {
            try {
                if (auto translated = resolve_resource(uri)) precise |= needs_exact_precision(*translated);
            } catch (...) {
                // Keep invalid, unused resources lazy; a referenced one will
                // report its error during compilation in the selected engine.
            }
        }
        if (precise) {
            const auto draft = precise_draft(selected_dialect);
            std::map<std::string, std::string, std::less<>> documents;
            for (const auto& [uri, raw] : resources) {
                try { documents.emplace(uri, resolve_resource(uri).value_or(raw)); }
                catch (...) { documents.emplace(uri, raw); }
            }
            for (int version = 0; version <= 3; ++version) {
                for (const auto* suffix : {"schema", "hyper-schema", "links"}) {
                    for (const auto* scheme : {"http://", "https://"}) {
                        const auto uri = std::string(scheme) + "json-schema.org/draft-0" + std::to_string(version) + "/" + suffix;
                        if (!documents.contains(uri)) {
                            if (auto document = resolve_resource(uri)) documents.emplace(uri, std::move(*document));
                        }
                    }
                }
            }
            auto span = [](std::string_view value) { return iiagent_precision_slice{value.data(), value.size()}; };
            std::vector<iiagent_precision_resource> explicit_resources;
            explicit_resources.reserve(documents.size());
            for (const auto& [uri, document] : documents) explicit_resources.push_back({span(uri), span(document)});
            iiagent_precision_request exact{span(schema_text), span(request.instance_json),
                explicit_resources.data(), explicit_resources.size(), draft, request.assert_formats,
                formats ? precise_format : nullptr, formats.get()};
            char diagnostic[2048]{};
            const int result = iiagent_precision_validate(&exact, diagnostic, sizeof(diagnostic));
            if (result == 0) return {Status::ok, true, {}};
            if (result == 1) return {Status::validation_failed, false, diagnostic};
            return {Status::invalid_argument, false, diagnostic};
        }
        auto compiled = schema::validator::compile(schema_text, options);
        schema::collector errors(schema::results_level::detailed);
        if (compiled.evaluate(request.instance_json, errors)) return {Status::ok, true, {}};
        std::string reason = "JSON Schema validation failed";
        for (std::size_t i = 0; i < errors.size(); ++i) {
            const auto row = errors[i];
            if (!row.is_match && !row.message.empty()) {
                reason = std::string(row.instance_location) + ": " + std::string(row.message); break;
            }
        }
        return {Status::validation_failed, false, std::move(reason)};
    } catch (const std::exception& error) {
        return {Status::invalid_argument, false, error.what()};
    }
}

CliCommandOutput CliCommandAdapter::execute(std::vector<std::string> args) const {
    CliCommandOutput output;
    bool seen_target = false, seen_command = false, seen_arguments = false;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const auto& flag = args[i];
        if (i + 1 >= args.size()) { output.diagnostic = "Missing option value"; return output; }
        if (flag == "--target" && !seen_target) { seen_target = true; output.invocation.target_id = args[++i]; }
        else if (flag == "--command" && !seen_command) { seen_command = true; output.invocation.command = args[++i]; }
        else if (flag == "--arguments" && !seen_arguments) { seen_arguments = true; output.invocation.arguments_json = args[++i]; }
        else { output.diagnostic = "Unknown or duplicate option"; return output; }
    }
    if (!seen_target || !seen_command || output.invocation.target_id.empty() || output.invocation.command.empty()) {
        output.diagnostic = "Target and command are required"; return output;
    }
    if (!seen_arguments) output.invocation.arguments_json = "{}";
    try { (void)parse(output.invocation.arguments_json); }
    catch (const std::exception& error) { output.diagnostic = error.what(); return output; }
    output.status = Status::ok; return output;
}

McpCommandOutput McpCommandAdapter::execute(const McpCommandInput& input) const {
    McpCommandOutput output;
    try {
        if (input.operation == McpCommandOperation::result) {
            auto id = parse(input.request_id_json);
            if (!valid_id(id)) throw std::invalid_argument("Invalid request ID");
            Json result = Json::object(), content = Json::array(), text = Json::object();
            text["type"] = "text";
            text["text"] = input.result.status == Status::ok ? input.result.output_json : input.result.diagnostic;
            content.push_back(std::move(text)); result["content"] = std::move(content);
            result["isError"] = input.result.status != Status::ok || input.result.cancelled;
            if (input.result.status == Status::ok && !input.result.output_json.empty()) {
                try { auto parsed = parse(input.result.output_json); if (parsed.is_object()) result["structuredContent"] = std::move(parsed); }
                catch (...) { /* Plain text is a valid tool result. */ }
            }
            output.response = dump(response(id, std::move(result))); return output;
        }
        if (input.operation == McpCommandOperation::progress) {
            const auto& event = input.progress;
            auto token = parse(event.token_json);
            if (!valid_id(token) || !std::isfinite(event.progress) || event.progress < 0 ||
                (event.total && (!std::isfinite(*event.total) || *event.total <= 0 || event.progress > *event.total)))
                throw std::invalid_argument("Invalid progress event");
            Json message = Json::object(), params = Json::object();
            message["jsonrpc"] = "2.0"; message["method"] = "notifications/progress";
            params["progressToken"] = std::move(token); params["progress"] = event.progress;
            if (event.total) params["total"] = *event.total;
            if (!event.message.empty()) params["message"] = event.message;
            message["params"] = std::move(params); output.response = dump(message); return output;
        }
        Json request;
        try { request = parse(input.message); }
        catch (...) { output.status = Status::invalid_argument; output.response = json_error(Json::null(), -32700, "Parse error"); return output; }
        if (!request.is_object() || !request.contains("jsonrpc") || !request["jsonrpc"].is_string() ||
            request["jsonrpc"].as<std::string>() != "2.0" || !request.contains("method") || !request["method"].is_string() ||
            (request.contains("id") && !valid_id(request["id"]))) {
            output.status = Status::invalid_argument; output.response = json_error(Json::null(), -32600, "Invalid Request"); return output;
        }
        const bool has_id = request.contains("id");
        const Json id = has_id ? request["id"] : Json::null();
        if (has_id) output.request_id_json = dump(id);
        const auto method = request["method"].as<std::string>();
        const Json params = request.contains("params") ? request["params"] : Json::object();
        if (!params.is_object()) {
            output.status = Status::invalid_argument; if (has_id) output.response = json_error(id, -32602, "Invalid params"); return output;
        }
        if (method == "notifications/cancelled" && !has_id) {
            if (params.contains("requestId") && valid_id(params["requestId"])) output.cancellation_id_json = dump(params["requestId"]);
            return output;
        }
        if (!has_id) return output;
        if (method == "initialize") {
            if (!params.contains("protocolVersion") || !params["protocolVersion"].is_string() ||
                !params.contains("capabilities") || !params["capabilities"].is_object() ||
                !params.contains("clientInfo") || !params["clientInfo"].is_object()) {
                output.status = Status::invalid_argument; output.response = json_error(id, -32602, "Invalid initialize parameters"); return output;
            }
            Json result = Json::object(), capabilities = Json::object(), tools = Json::object(), info = Json::object();
            result["protocolVersion"] = "2025-11-25"; tools["listChanged"] = false; capabilities["tools"] = std::move(tools);
            info["name"] = "iiAgentApplicationInterface"; info["version"] = "0.1.0";
            result["capabilities"] = std::move(capabilities); result["serverInfo"] = std::move(info);
            output.response = dump(response(id, std::move(result))); return output;
        }
        if (method == "ping") { output.response = dump(response(id, Json::object())); return output; }
        if (method == "tools/list") {
            std::vector<ApplicationCommandDescriptor> entries = input.published;
            std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
            std::string cursor;
            if (params.contains("cursor")) {
                if (!params["cursor"].is_string()) throw std::invalid_argument("Invalid cursor");
                cursor = params["cursor"].as<std::string>();
            }
            Json items = Json::array(); std::string last; std::size_t count = 0; bool more = false;
            for (std::size_t i = 0; i < entries.size(); ++i) {
                const auto& entry = entries[i];
                if (!tool_name(entry.name) || entry.target_id.empty() || (i && entries[i-1].name == entry.name))
                    throw std::invalid_argument("Invalid or duplicate published tool");
                if (!cursor.empty() && entry.name <= cursor) continue;
                if (count == 100) { more = true; break; }
                Json schema_value = parse(entry.input_schema);
                if (!schema_value.is_object()) throw std::invalid_argument("MCP input schema must be an object");
                Json item = Json::object(); item["name"] = entry.name; item["description"] = entry.description;
                item["inputSchema"] = std::move(schema_value);
                if (!entry.output_schema.empty()) {
                    auto out_schema = parse(entry.output_schema);
                    if (!out_schema.is_object()) throw std::invalid_argument("MCP output schema must be an object");
                    item["outputSchema"] = std::move(out_schema);
                }
                items.push_back(std::move(item)); ++count; last = entry.name;
            }
            Json result = Json::object(); result["tools"] = std::move(items); if (more) result["nextCursor"] = last;
            output.response = dump(response(id, std::move(result))); return output;
        }
        if (method == "tools/call") {
            if (!params.contains("name") || !params["name"].is_string() ||
                (params.contains("arguments") && !params["arguments"].is_object())) {
                output.status = Status::invalid_argument; output.response = json_error(id, -32602, "Invalid tool call"); return output;
            }
            const auto name = params["name"].as<std::string>(); const ApplicationCommandDescriptor* selected = nullptr;
            for (const auto& entry : input.published) if (entry.name == name) {
                if (selected) throw std::invalid_argument("Ambiguous tool name"); selected = &entry;
            }
            if (!selected || !tool_name(name) || selected->target_id.empty()) {
                output.status = Status::not_found; output.response = json_error(id, -32602, "Tool unavailable"); return output;
            }
            auto args = params.contains("arguments") ? params["arguments"] : Json::object();
            output.invocation = ApplicationCommandInvocation{output.request_id_json, name, selected->target_id, dump(args), {},
                selected->capability_id.empty() ? name : selected->capability_id};
            if (params.contains("_meta") && params["_meta"].is_object() && params["_meta"].contains("progressToken")) {
                auto token = params["_meta"]["progressToken"];
                if (!valid_id(token)) throw std::invalid_argument("Invalid progress token");
                output.progress_token_json = dump(token);
            }
            return output;
        }
        output.status = Status::not_found; output.response = json_error(id, -32601, "Method not found"); return output;
    } catch (const std::exception&) {
        output.status = Status::invalid_argument;
        output.invocation.reset();
        output.progress_token_json.clear();
        if (!output.request_id_json.empty()) {
            try { output.response = json_error(parse(output.request_id_json), -32602, "Invalid command metadata or params"); }
            catch (...) { output.response = json_error(Json::null(), -32600, "Invalid Request"); }
        }
        return output;
    }
}
} // namespace iiAgentApplicationInterface
