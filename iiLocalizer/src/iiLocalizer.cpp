#include "iiLocalizer.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <map>
#include <set>
#include <utility>

namespace ii::localizer {
namespace {
constexpr std::size_t maximum_input = 8 * 1024 * 1024;
constexpr std::size_t maximum_field = 65536;
constexpr std::size_t maximum_entries = 10000;
constexpr std::size_t maximum_arguments = 1024;
using Key = std::pair<std::optional<std::string>, std::string>;

// Validate UTF-8 scalars without depending on a platform locale or another SDK.
bool valid_utf8(std::string_view value) noexcept {
    for (std::size_t i=0;i<value.size();) {
        const auto a=static_cast<unsigned char>(value[i]);
        if (a==0) return false;
        if (a<0x80) { ++i; continue; }
        std::size_t length=0; unsigned int point=0,minimum=0;
        if (a>=0xc2 && a<=0xdf) { length=2; point=a&0x1f; minimum=0x80; }
        else if (a>=0xe0 && a<=0xef) { length=3; point=a&0x0f; minimum=0x800; }
        else if (a>=0xf0 && a<=0xf4) { length=4; point=a&0x07; minimum=0x10000; }
        else return false;
        if (i+length>value.size()) return false;
        for (std::size_t j=1;j<length;++j) {
            const auto next=static_cast<unsigned char>(value[i+j]);
            if ((next&0xc0)!=0x80) return false;
            point=(point<<6)|(next&0x3f);
        }
        if (point<minimum || point>0x10ffff || (point>=0xd800 && point<=0xdfff)) return false;
        i+=length;
    }
    return true;
}

// Trim ASCII PO syntax whitespace; message contents are never trimmed.
std::string_view trim(std::string_view text) noexcept {
    while (!text.empty() && (text.front()==' ' || text.front()=='\t' || text.front()=='\r')) text.remove_prefix(1);
    while (!text.empty() && (text.back()==' ' || text.back()=='\t' || text.back()=='\r')) text.remove_suffix(1);
    return text;
}

// Compare metadata names and charset tokens without changing message identities.
std::string ascii_lower(std::string_view text) {
    std::string out(text);
    for (auto& c:out) if (c>='A' && c<='Z') c=static_cast<char>(c-'A'+'a');
    return out;
}

// Decode one quoted PO fragment, rejecting unsupported or unsafe escapes.
std::expected<std::string, Error> quoted(std::string_view text, std::size_t line) {
    text=trim(text);
    if (text.empty() || text.front()!='"') return std::unexpected(Error{"Expected a quoted PO string",line});
    std::string out;
    for (std::size_t i=1;i<text.size();++i) {
        const char c=text[i];
        if (c=='"') {
            if (!trim(text.substr(i+1)).empty()) return std::unexpected(Error{"Unexpected text after PO string",line});
            return out;
        }
        if (c!='\\') { out.push_back(c); continue; }
        if (++i>=text.size()) return std::unexpected(Error{"Incomplete PO escape",line});
        switch (text[i]) {
        case 'a': out.push_back('\a'); break;
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        case 'v': out.push_back('\v'); break;
        case '\\': out.push_back('\\'); break;
        case '"': out.push_back('"'); break;
        default: {
            unsigned int value=0;
            if (text[i]>='0' && text[i]<='7') {
                int count=0;
                do { value=value*8+static_cast<unsigned int>(text[i]-'0'); ++count; ++i; }
                while (i<text.size() && count<3 && text[i]>='0' && text[i]<='7');
                --i;
            } else if (text[i]=='x') {
                auto digit=[](char x)->int {
                    if (x>='0' && x<='9') return x-'0';
                    if (x>='a' && x<='f') return x-'a'+10;
                    if (x>='A' && x<='F') return x-'A'+10;
                    return -1;
                };
                ++i; bool any=false;
                while (i<text.size() && digit(text[i])>=0) {
                    any=true; const auto d=static_cast<unsigned int>(digit(text[i]));
                    if (value>(255-d)/16) return std::unexpected(Error{"PO hexadecimal escape is out of range",line});
                    value=value*16+d; ++i;
                }
                if (!any) return std::unexpected(Error{"Empty PO hexadecimal escape",line});
                --i;
            } else return std::unexpected(Error{"Unsupported PO escape",line});
            if (value==0 || value>255) return std::unexpected(Error{"PO byte escape is out of range",line});
            out.push_back(static_cast<char>(value));
            break;
        }}
        if (out.size()>maximum_field) return std::unexpected(Error{"PO field exceeds its size limit",line});
    }
    return std::unexpected(Error{"Unterminated PO string",line});
}

// Detect a workflow flag in either current gettext comment spelling.
bool fuzzy_flag(std::string_view text) noexcept {
    std::size_t i=0;
    while (i<text.size()) {
        while (i<text.size() && (text[i]==' ' || text[i]=='\t' || text[i]==',')) ++i;
        auto start=i;
        while (i<text.size() && text[i]!=',' && text[i]!=' ' && text[i]!='\t') ++i;
        if (text.substr(start,i-start)=="fuzzy") return true;
    }
    return false;
}

// Store a single parsed item until its complete identity and value are known.
struct Entry final {
    std::optional<std::string> context;
    std::string id,translation;
    bool has_id=false,has_translation=false,fuzzy=false;
    std::size_t line=0;
    enum class Field { none, context, id, translation } field=Field::none;
    [[nodiscard]] bool has_content() const noexcept { return context.has_value() || has_id || has_translation; }
};

// Append a decoded continuation while enforcing a bounded field size.
std::expected<void, Error> append(std::string& destination, std::string_view part, std::size_t line) {
    if (part.size()>maximum_field-destination.size()) return std::unexpected(Error{"PO field exceeds its size limit",line});
    destination.append(part);
    return {};
}

// Interpret only metadata needed by this first-stage reader.
std::expected<void, Error> read_header(std::string_view header, std::string& language, std::size_t line) {
    bool saw_language=false, saw_content_type=false;
    while (!header.empty()) {
        const auto end=header.find('\n');
        const auto item=trim(header.substr(0,end));
        header=end==header.npos?std::string_view{}:header.substr(end+1);
        const auto colon=item.find(':');
        if (colon==item.npos) continue;
        const auto name=ascii_lower(trim(item.substr(0,colon)));
        const auto value=trim(item.substr(colon+1));
        if (name=="language") {
            if (saw_language) return std::unexpected(Error{"Duplicate PO Language header",line});
            saw_language=true; language=std::string(value);
        } else if (name=="content-type") {
            if (saw_content_type) return std::unexpected(Error{"Duplicate PO Content-Type header",line});
            saw_content_type=true;
            const auto lower=ascii_lower(value); const auto pos=lower.find("charset");
            if (pos!=std::string::npos) {
                auto part=trim(std::string_view(lower).substr(pos+7));
                if (part.empty() || part.front()!='=') return std::unexpected(Error{"Invalid PO charset header",line});
                part=trim(part.substr(1));
                if (!part.empty() && (part.front()=='\"' || part.front()=='\'')) {
                    const auto quote=part.front(); part.remove_prefix(1);
                    const auto close=part.find(quote);
                    if (close==part.npos) return std::unexpected(Error{"Invalid PO charset header",line});
                    part=part.substr(0,close);
                } else part=trim(part.substr(0,part.find(';')));
                if (part!="utf-8") return std::unexpected(Error{"PO charset must be UTF-8",line});
            }
        }
    }
    return {};
}

// Substitute numbered arguments once; values cannot inject new placeholders.
std::expected<std::string, Error> format(std::string_view text, std::span<const std::string> arguments) {
    std::string output;
    const auto add=[&](std::string_view piece)->bool {
        if (piece.size()>maximum_input-output.size()) return false;
        output.append(piece); return true;
    };
    for (std::size_t i=0;i<text.size();) {
        if (text[i]!='%') {
            auto next=text.find('%',i); if (next==text.npos) next=text.size();
            if (!add(text.substr(i,next-i))) return std::unexpected(Error{"Formatted text exceeds its size limit",0});
            i=next; continue;
        }
        if (i+1<text.size() && text[i+1]=='%') {
            if (!add("%")) return std::unexpected(Error{"Formatted text exceeds its size limit",0});
            i+=2; continue;
        }
        if (i+1>=text.size() || text[i+1]<'0' || text[i+1]>'9') {
            if (!add("%")) return std::unexpected(Error{"Formatted text exceeds its size limit",0});
            ++i; continue;
        }
        std::size_t number=0; ++i;
        while (i<text.size() && text[i]>='0' && text[i]<='9') {
            const auto digit=static_cast<std::size_t>(text[i]-'0');
            if (number>((std::numeric_limits<std::size_t>::max)()-digit)/10)
                return std::unexpected(Error{"Placeholder number is too large",0});
            number=number*10+digit; ++i;
        }
        if (number==0 || number>arguments.size()) return std::unexpected(Error{"Placeholder argument is missing",0});
        if (!add(arguments[number-1])) return std::unexpected(Error{"Formatted text exceeds its size limit",0});
    }
    return output;
}
} // namespace

// The shared immutable snapshot keeps catalog copies and concurrent reads independent.
struct Catalog::Data final {
    std::string language;
    std::map<Key,std::string> entries;
};

// Retain a complete immutable snapshot; copies share only read-only data.
Catalog::Catalog(std::shared_ptr<const Data> data) noexcept : data_(std::move(data)) {}

// Parse the complete input into a private snapshot before making it observable.
std::expected<Catalog, Error> Catalog::parse(std::string_view bytes) {
    if (bytes.size()>maximum_input) return std::unexpected(Error{"PO input exceeds its size limit",0});
    if (!valid_utf8(bytes)) return std::unexpected(Error{"PO input is not valid UTF-8",0});
    auto data=std::make_shared<Data>();
    std::set<Key> seen;
    Entry entry;
    bool saw_header=false;
    const auto finish=[&]()->std::expected<void,Error> {
        if (!entry.has_content()) { entry=Entry{}; return {}; }
        if (!entry.has_id || !entry.has_translation) return std::unexpected(Error{"PO entry requires msgid and msgstr",entry.line});
        if (!valid_utf8(entry.id) || !valid_utf8(entry.translation) || (entry.context && !valid_utf8(*entry.context)))
            return std::unexpected(Error{"PO field is not valid UTF-8",entry.line});
        if (entry.id.empty()) {
            if (entry.context || saw_header) return std::unexpected(Error{"Duplicate or contextual PO header",entry.line});
            saw_header=true;
            if (auto result=read_header(entry.translation,data->language,entry.line);!result) return result;
        } else {
            Key key{std::move(entry.context),std::move(entry.id)};
            if (seen.size()>=maximum_entries) return std::unexpected(Error{"Too many PO entries",entry.line});
            if (!seen.insert(key).second) return std::unexpected(Error{"Duplicate PO message identity",entry.line});
            if (!entry.fuzzy && !entry.translation.empty()) data->entries.emplace(std::move(key),std::move(entry.translation));
        }
        entry=Entry{}; return {};
    };
    std::size_t line_number=0;
    while (!bytes.empty()) {
        ++line_number;
        const auto end=bytes.find('\n');
        auto line=trim(bytes.substr(0,end));
        bytes=end==bytes.npos?std::string_view{}:bytes.substr(end+1);
        if (line.empty()) { if (auto done=finish();!done) return std::unexpected(done.error()); continue; }
        if (line.front()=='#') {
            if (entry.has_translation) { if (auto done=finish();!done) return std::unexpected(done.error()); }
            if (line.starts_with("#,") || line.starts_with("#=")) entry.fuzzy=entry.fuzzy || fuzzy_flag(line.substr(2));
            continue;
        }
        if (line.starts_with("msgid_plural") || line.starts_with("msgstr["))
            return std::unexpected(Error{"Plural PO entries are not supported yet",line_number});
        if (line.front()=='"') {
            auto value=quoted(line,line_number); if (!value) return std::unexpected(value.error());
            std::string* destination=nullptr;
            switch(entry.field) {
            case Entry::Field::context: destination=entry.context?&*entry.context:nullptr; break;
            case Entry::Field::id: destination=&entry.id; break;
            case Entry::Field::translation: destination=&entry.translation; break;
            case Entry::Field::none: break;
            }
            if (!destination) return std::unexpected(Error{"Orphan PO string continuation",line_number});
            if (auto added=append(*destination,*value,line_number);!added) return std::unexpected(added.error());
            continue;
        }
        const auto space=line.find_first_of(" \t");
        if (space==line.npos) return std::unexpected(Error{"Invalid PO directive",line_number});
        const auto directive=line.substr(0,space);
        if ((directive=="msgid" || directive=="msgctxt") && entry.has_translation) {
            if (auto done=finish();!done) return std::unexpected(done.error());
        }
        if (!entry.line) entry.line=line_number;
        auto value=quoted(line.substr(space),line_number); if (!value) return std::unexpected(value.error());
        if (value->size()>maximum_field) return std::unexpected(Error{"PO field exceeds its size limit",line_number});
        if (directive=="msgctxt") {
            if (entry.context || entry.has_id) return std::unexpected(Error{"Invalid PO context order",line_number});
            entry.context=std::move(*value); entry.field=Entry::Field::context;
        } else if (directive=="msgid") {
            if (entry.has_id || entry.has_translation) return std::unexpected(Error{"Duplicate PO msgid",line_number});
            entry.id=std::move(*value); entry.has_id=true; entry.field=Entry::Field::id;
        } else if (directive=="msgstr") {
            if (!entry.has_id || entry.has_translation) return std::unexpected(Error{"Invalid PO msgstr order",line_number});
            entry.translation=std::move(*value); entry.has_translation=true; entry.field=Entry::Field::translation;
        } else return std::unexpected(Error{"Unsupported PO directive",line_number});
    }
    if (auto done=finish();!done) return std::unexpected(done.error());
    return Catalog(std::move(data));
}

// Borrow the header value for as long as this catalog snapshot remains alive.
std::string_view Catalog::language() const noexcept { return data_->language; }

// Resolve the exact context and source, then format a translation or safe fallback.
std::expected<Translation, Error> Catalog::translate(std::string_view source,
        std::span<const std::string> arguments, std::optional<std::string_view> context) const {
    if (source.empty() || source.size()>maximum_field || !valid_utf8(source) ||
        (context && (context->size()>maximum_field || !valid_utf8(*context))))
        return std::unexpected(Error{"Message and context must be bounded UTF-8",0});
    if (arguments.size()>maximum_arguments) return std::unexpected(Error{"Too many message arguments",0});
    for (const auto& argument:arguments)
        if (argument.size()>maximum_field || !valid_utf8(argument)) return std::unexpected(Error{"Message arguments must be bounded UTF-8",0});
    Key key{context?std::optional<std::string>{std::string(*context)}:std::nullopt,std::string(source)};
    const auto found=data_->entries.find(key);
    if (found!=data_->entries.end()) {
        if (auto value=format(found->second,arguments)) return Translation{std::move(*value),Origin::translated};
        if (auto fallback=format(source,arguments)) return Translation{std::move(*fallback),Origin::source_invalid_translation};
        return std::unexpected(Error{"Both translation and source have invalid arguments",0});
    }
    auto fallback=format(source,arguments);
    if (!fallback) return std::unexpected(fallback.error());
    return Translation{std::move(*fallback),Origin::source_missing};
}

// Validate the entire registry before returning stable indices into the caller's input.
std::expected<std::vector<std::size_t>, Error> select_label_usages(
        std::span<const LabelUsage> uses, std::string_view source, std::optional<std::string_view> location) {
    const auto valid_identifier=[](std::string_view value) {
        return !value.empty() && value.size()<=maximum_field && valid_utf8(value);
    };
    if (uses.size()>maximum_entries) return std::unexpected(Error{"Too many label usages",0});
    if (!valid_identifier(source) || (location && !valid_identifier(*location)))
        return std::unexpected(Error{"Label source and location must be nonempty bounded UTF-8",0});
    std::size_t bytes=source.size()+(location?location->size():0);
    std::set<std::string_view> locations,keys;
    std::vector<std::size_t> selected;
    for (std::size_t i=0;i<uses.size();++i) {
        const auto& use=uses[i];
        if (!valid_identifier(use.location) || !valid_identifier(use.source) || !valid_identifier(use.key))
            return std::unexpected(Error{"Label usages require nonempty bounded UTF-8 fields",0});
        for (const auto field:{std::string_view(use.location),std::string_view(use.source),std::string_view(use.key)}) {
            if (field.size()>maximum_input-bytes) return std::unexpected(Error{"Label usage registry exceeds its size limit",0});
            bytes+=field.size();
        }
        if (!locations.insert(use.location).second) return std::unexpected(Error{"Duplicate label location",0});
        if (!keys.insert(use.key).second) return std::unexpected(Error{"Duplicate label string key",0});
        if (use.source==source && (!location || use.location==*location)) selected.push_back(i);
    }
    return selected;
}
} // namespace ii::localizer
