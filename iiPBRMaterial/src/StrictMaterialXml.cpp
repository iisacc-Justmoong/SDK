#include "StrictMaterialXml.h"

#include <set>
#include <vector>

namespace ii::pbr::exchange::detail {
namespace {
// One frame tracks the raw XML element and names among its immediate children.
struct Frame {
    std::string tag;
    std::set<std::string> childNames;
};

bool space(char value)
{
    return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

bool delimiter(char value)
{
    return space(value) || value == '/' || value == '>' || value == '=' || value == '<' ||
           value == '\'' || value == '"';
}

StrictXmlError unsupported(std::string message)
{
    return {"strict_xml_unsupported", std::move(message)};
}
} // namespace

std::optional<StrictXmlError> checkStrictMaterialXml(std::string_view xml)
{
    std::vector<Frame> stack(1); // Sentinel represents the document root scope.
    std::size_t position = 0;
    std::size_t elementCount = 0;
    while ((position = xml.find('<', position)) != std::string_view::npos) {
        if (xml.substr(position, 4) == "<!--") {
            const auto end = xml.find("-->", position + 4);
            if (end == std::string_view::npos) return unsupported("Unterminated XML comment.");
            position = end + 3;
            continue;
        }
        if (xml.substr(position, 9) == "<![CDATA[") {
            const auto end = xml.find("]]>", position + 9);
            if (end == std::string_view::npos) return unsupported("Unterminated CDATA section.");
            position = end + 3;
            continue;
        }
        if (xml.substr(position, 2) == "<?") {
            const auto end = xml.find("?>", position + 2);
            if (end == std::string_view::npos) return unsupported("Unterminated processing instruction.");
            position = end + 2;
            continue;
        }
        if (xml.substr(position, 2) == "<!")
            return unsupported("DTD and other XML declarations are not supported by strict inspection.");

        const bool closing = xml.substr(position, 2) == "</";
        std::size_t cursor = position + (closing ? 2 : 1);
        const auto nameStart = cursor;
        while (cursor < xml.size() && !delimiter(xml[cursor])) ++cursor;
        if (cursor == nameStart) return unsupported("Malformed XML element name.");
        const std::string tag(xml.substr(nameStart, cursor - nameStart));
        if (closing) {
            while (cursor < xml.size() && space(xml[cursor])) ++cursor;
            if (cursor >= xml.size() || xml[cursor] != '>' || stack.size() == 1 || stack.back().tag != tag)
                return unsupported("Malformed or mismatched XML closing element.");
            stack.pop_back();
            position = cursor + 1;
            continue;
        }

        if (++elementCount > 1'000'000 || stack.size() > 256)
            return StrictXmlError{"strict_xml_limit", "Strict XML element or depth limit exceeded."};
        bool selfClosing = false;
        bool ended = false;
        bool hasName = false;
        std::string childName;
        while (cursor < xml.size()) {
            while (cursor < xml.size() && space(xml[cursor])) ++cursor;
            if (cursor >= xml.size()) break;
            if (xml[cursor] == '>') { ++cursor; ended = true; break; }
            if (xml[cursor] == '/' && cursor + 1 < xml.size() && xml[cursor + 1] == '>') {
                cursor += 2; selfClosing = true; ended = true; break;
            }
            const auto attrStart = cursor;
            while (cursor < xml.size() && !delimiter(xml[cursor])) ++cursor;
            if (cursor == attrStart) return unsupported("Malformed XML attribute name.");
            const auto attribute = xml.substr(attrStart, cursor - attrStart);
            while (cursor < xml.size() && space(xml[cursor])) ++cursor;
            if (cursor >= xml.size() || xml[cursor++] != '=') return unsupported("Malformed XML attribute.");
            while (cursor < xml.size() && space(xml[cursor])) ++cursor;
            if (cursor >= xml.size() || (xml[cursor] != '\'' && xml[cursor] != '"'))
                return unsupported("XML attribute values must be quoted.");
            const char quote = xml[cursor++];
            const auto valueStart = cursor;
            const auto end = xml.find(quote, cursor);
            if (end == std::string_view::npos) return unsupported("Unterminated XML attribute value.");
            const auto value = xml.substr(valueStart, end - valueStart);
            cursor = end + 1;
            if (attribute == "name") {
                if (hasName || value.find_first_of("&\t\r\n") != std::string_view::npos)
                    return unsupported("Escaped, normalized, or repeated name attributes are not supported.");
                hasName = true;
                childName.assign(value);
            }
        }
        if (!ended) return unsupported("Unterminated XML element.");
        if (!childName.empty() && !stack.back().childNames.insert(childName).second)
            return StrictXmlError{"duplicate_element_name", "Duplicate sibling name: " + childName};
        if (!selfClosing) stack.push_back({tag, {}});
        position = cursor;
    }
    if (stack.size() != 1) return unsupported("Unclosed XML element.");
    return std::nullopt;
}
} // namespace ii::pbr::exchange::detail
