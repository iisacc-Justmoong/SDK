#include "iiLocalizerC.h"
#include "iiLocalizer.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// A C caller owns this wrapper; the underlying immutable catalog owns its data.
struct iil_catalog final {
    explicit iil_catalog(ii::localizer::Catalog value) : catalog(std::move(value)) {}
    ii::localizer::Catalog catalog;
};

namespace {
// Return an independent NUL-terminated copy compatible with iil_string_free.
char* copy(std::string_view text) noexcept {
    auto* result=static_cast<char*>(std::malloc(text.size()+1));
    if (!result) return nullptr;
    if (!text.empty()) std::memcpy(result,text.data(),text.size());
    result[text.size()]='\0'; return result;
}

// Set an optional error message and return the C failure indicator.
int fail(char** error,std::string_view message) noexcept {
    if (error) *error=copy(message);
    return 1;
}

// Validate a byte pointer while permitting an empty length-delimited value.
bool valid_pointer(const char* text,std::size_t size) noexcept { return text || size==0; }
std::string_view view(const char* text,std::size_t size) noexcept { return text?std::string_view(text,size):std::string_view{}; }
}

// Construct a C-owned handle only after the native parser has accepted every entry.
extern "C" int iil_catalog_create(const char* bytes,size_t length,iil_catalog** output,char** error) {
    if (error) *error=nullptr;
    if (output) *output=nullptr;
    if (!output || !valid_pointer(bytes,length)) return fail(error,"Invalid catalog input or output pointer");
    try {
        auto parsed=ii::localizer::Catalog::parse(view(bytes,length));
        if (!parsed) return fail(error,parsed.error().message);
        *output=new iil_catalog(std::move(*parsed)); return 0;
    } catch (const std::exception& exception) { return fail(error,exception.what()); }
    catch (...) { return fail(error,"Catalog creation failed"); }
}

// Destruction releases the wrapper and its immutable catalog snapshot.
extern "C" void iil_catalog_destroy(iil_catalog* catalog) { delete catalog; }

// Return an independent copy so the result never borrows from a C++ object.
extern "C" int iil_catalog_language(const iil_catalog* catalog,char** output,char** error) {
    if (error) *error=nullptr;
    if (output) *output=nullptr;
    if (!catalog || !output) return fail(error,"Invalid catalog or output pointer");
    *output=copy(catalog->catalog.language());
    return *output?0:fail(error,"Could not allocate language text");
}

// Copy bounded argument bytes, perform native translation, and return owned text.
extern "C" int iil_catalog_translate(const iil_catalog* catalog,const char* source,size_t source_length,
        const char* context,size_t context_length,int has_context,const char* const* arguments,
        const size_t* argument_lengths,size_t argument_count,char** output,int* origin,char** error) {
    if (error) *error=nullptr;
    if (output) *output=nullptr;
    if (!catalog || !output || !origin || !valid_pointer(source,source_length) ||
        (has_context && !valid_pointer(context,context_length)) ||
        (argument_count && (!arguments || !argument_lengths))) return fail(error,"Invalid translation input or output pointer");
    if (argument_count>1024) return fail(error,"Too many message arguments");
    try {
        std::vector<std::string> values; values.reserve(argument_count);
        for (size_t i=0;i<argument_count;++i) {
            if (!valid_pointer(arguments[i],argument_lengths[i])) return fail(error,"Invalid argument pointer");
            if (argument_lengths[i]>65536) return fail(error,"Message argument exceeds its size limit");
            values.emplace_back(view(arguments[i],argument_lengths[i]));
        }
        const auto ctx=has_context?std::optional<std::string_view>{view(context,context_length)}:std::nullopt;
        auto translated=catalog->catalog.translate(view(source,source_length),values,ctx);
        if (!translated) return fail(error,translated.error().message);
        *output=copy(translated->text);
        if (!*output) return fail(error,"Could not allocate translation text");
        *origin=static_cast<int>(translated->origin); return 0;
    } catch (const std::exception& exception) { return fail(error,exception.what()); }
    catch (...) { return fail(error,"Translation failed"); }
}

// Validate and copy the caller's bounded registry before selecting any locations.
extern "C" int iil_select_label_usages(const iil_label_usage* uses,size_t use_count,
        const char* source,size_t source_length,const char* location,size_t location_length,
        int has_location,size_t** output_indices,size_t* output_count,char** error) {
    if (error) *error=nullptr;
    if (output_indices) *output_indices=nullptr;
    if (output_count) *output_count=0;
    if (!output_indices || !output_count || (use_count && !uses) ||
        !valid_pointer(source,source_length) || (has_location && !valid_pointer(location,location_length)))
        return fail(error,"Invalid label selection input or output pointer");
    if (use_count>10000 || source_length>65536 || (has_location && location_length>65536))
        return fail(error,"Label selection exceeds its size limit");
    try {
        std::size_t bytes=source_length+(has_location?location_length:0);
        std::vector<ii::localizer::LabelUsage> values; values.reserve(use_count);
        for (size_t i=0;i<use_count;++i) {
            const auto& item=uses[i];
            if (!valid_pointer(item.location,item.location_length) || !valid_pointer(item.source,item.source_length) ||
                !valid_pointer(item.key,item.key_length)) return fail(error,"Invalid label usage pointer");
            for (const auto length:{item.location_length,item.source_length,item.key_length}) {
                if (length>65536 || length>8*1024*1024-bytes) return fail(error,"Label usage registry exceeds its size limit");
                bytes+=length;
            }
            values.push_back({std::string(view(item.location,item.location_length)),
                std::string(view(item.source,item.source_length)),std::string(view(item.key,item.key_length))});
        }
        const auto target=has_location?std::optional<std::string_view>{view(location,location_length)}:std::nullopt;
        auto selected=ii::localizer::select_label_usages(values,view(source,source_length),target);
        if (!selected) return fail(error,selected.error().message);
        if (!selected->empty()) {
            auto* indices=static_cast<size_t*>(std::malloc(selected->size()*sizeof(size_t)));
            if (!indices) return fail(error,"Could not allocate label selection");
            std::copy(selected->begin(),selected->end(),indices);
            *output_indices=indices;
        }
        *output_count=selected->size();
        return 0;
    } catch (const std::exception& exception) { return fail(error,exception.what()); }
    catch (...) { return fail(error,"Label selection failed"); }
}

// Free selection indices with the allocator used by the C ABI.
extern "C" void iil_indices_free(size_t* indices) { std::free(indices); }

// Free strings with the same allocator used by all other C ABI functions.
extern "C" void iil_string_free(char* text) { std::free(text); }
