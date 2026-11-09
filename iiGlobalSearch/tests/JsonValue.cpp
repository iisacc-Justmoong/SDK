#include "../src/Local/JsonValue.h"
#include <cstdint>
#include <stdexcept>
#include <string>

static void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

int main() {
    using ii::core::Value;
    const auto value = iiGlobalSearch::detail::parseCoreJson(
        R"({"text":"한글","items":[true,-4,18446744073709551615,1.25,null],"nested":{"key":"value"}})");
    check(value.has_value(), "valid JSON was rejected");
    const auto text = value->find("text");
    check(text && text->get().get<std::string>() && text->get().get<std::string>()->get() == "한글",
          "UTF-8 text was not preserved");
    const auto itemField = value->find("items");
    check(itemField.has_value(), "array was not preserved");
    const auto items = itemField->get().get<Value::Array>();
    check(items && items->get().size() == 5, "array contents were not preserved");
    const auto& array = items->get();
    check(array[0].get<bool>() && array[0].get<bool>()->get(), "boolean was not preserved");
    check(array[1].get<std::int64_t>() && array[1].get<std::int64_t>()->get() == -4, "signed integer was not preserved");
    check(array[2].get<std::uint64_t>() && array[2].get<std::uint64_t>()->get() == UINT64_MAX, "unsigned integer was not preserved");
    check(array[3].get<double>() && array[3].get<double>()->get() == 1.25, "real number was not preserved");
    check(array[4].kind() == ii::core::ValueKind::null, "null was not preserved");
    const auto nested = value->find("nested");
    check(nested.has_value(), "nested object was not preserved");
    const auto key = nested->get().find("key");
    check(key && key->get().get<std::string>() && key->get().get<std::string>()->get() == "value",
          "nested string was not preserved");
    check(!iiGlobalSearch::detail::parseCoreJson("{broken"), "invalid JSON was accepted");
}
