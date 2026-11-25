#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque immutable catalog owned by the caller until iil_catalog_destroy. */
typedef struct iil_catalog iil_catalog;

/* Result provenance; values correspond to the C++ Translation::origin. */
enum iil_origin { IIL_TRANSLATED = 0, IIL_SOURCE_MISSING = 1, IIL_SOURCE_INVALID_TRANSLATION = 2 };

/* One application-owned location, source message and independent string key. */
typedef struct iil_label_usage {
    const char* location;
    size_t location_length;
    const char* source;
    size_t source_length;
    const char* key;
    size_t key_length;
} iil_label_usage;

/* Parse length-delimited UTF-8 PO bytes. On failure, error receives allocated text. */
int iil_catalog_create(const char* bytes, size_t length, iil_catalog** output, char** error);

/* Release an opaque catalog; passing NULL has no effect. */
void iil_catalog_destroy(iil_catalog* catalog);

/* Copy the Language header to an allocated, NUL-terminated UTF-8 string. */
int iil_catalog_language(const iil_catalog* catalog, char** output, char** error);

/* Translate an exact message and optional context with length-delimited arguments. */
int iil_catalog_translate(const iil_catalog* catalog, const char* source, size_t source_length,
    const char* context, size_t context_length, int has_context,
    const char* const* arguments, const size_t* argument_lengths, size_t argument_count,
    char** output, int* origin, char** error);

/* Return matching input indices in order; has_location=0 selects all uses of the source.
   No match is a successful empty result. Duplicate locations or keys are errors.
   The caller releases a non-NULL result with iil_indices_free. */
int iil_select_label_usages(const iil_label_usage* uses, size_t use_count,
    const char* source, size_t source_length, const char* location, size_t location_length,
    int has_location, size_t** output_indices, size_t* output_count, char** error);

/* Release the index array allocated by iil_select_label_usages. */
void iil_indices_free(size_t* indices);

/* Release output or error text allocated by this interface. */
void iil_string_free(char* text);

#ifdef __cplusplus
}
#endif
