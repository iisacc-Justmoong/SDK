//! Private, in-memory JSON Schema adapter for values requiring exact decimal
//! arithmetic. Its caller owns all pointers and supplies every external schema;
//! this module never opens files, sockets, credentials, or processes.
use std::collections::BTreeMap;
use std::ffi::c_void;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::slice;
use std::str;
use jsonschema::{Draft, Retrieve, Uri};
use serde_json::Value;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Text { data: *const u8, size: usize }
#[repr(C)]
pub struct Resource { uri: Text, json: Text }
type Format = unsafe extern "C" fn(*mut c_void, u32, Text) -> i32;
#[repr(C)]
pub struct Request {
    schema: Text,
    instance: Text,
    resources: *const Resource,
    resource_count: usize,
    draft: u32,
    assert_formats: bool,
    format: Option<Format>,
    context: *mut c_void,
}

/// All values are owned snapshots, not handles to host state. Only exact URI
/// matches are considered; a missing document is an explicit compile failure.
struct Resources(BTreeMap<String, String>);
impl Retrieve for Resources {
    fn retrieve(&self, uri: &Uri<String>) -> Result<Value, Box<dyn std::error::Error + Send + Sync>> {
        let raw = self.0.get(uri.as_str()).ok_or_else(|| format!("Unresolved schema resource: {uri}"))?;
        Ok(serde_json::from_str(raw)?)
    }
}

/// Read a borrowed FFI span. The trusted C++ caller must provide a valid UTF-8
/// allocation for the length supplied, alive until this validation returns.
unsafe fn text<'a>(value: Text) -> Result<&'a str, String> {
    if value.size == 0 { return Ok(""); }
    if value.data.is_null() { return Err("Missing input pointer".into()); }
    str::from_utf8(slice::from_raw_parts(value.data, value.size)).map_err(|e| e.to_string())
}

/// Compile and validate one request with immutable local resources. A supplied
/// callback is invoked only while the caller's request remains alive and only
/// for an explicitly recognized legacy format; the callback is thread safe.
unsafe fn run(request: &Request) -> Result<bool, String> {
    let schema: Value = serde_json::from_str(text(request.schema)?).map_err(|e| e.to_string())?;
    let instance: Value = serde_json::from_str(text(request.instance)?).map_err(|e| e.to_string())?;
    if request.resource_count != 0 && request.resources.is_null() { return Err("Missing resource array".into()); }
    let mut resources = BTreeMap::new();
    if request.resource_count != 0 {
        for resource in slice::from_raw_parts(request.resources, request.resource_count) {
            let uri = text(resource.uri)?.to_owned();
            if resources.insert(uri, text(resource.json)?.to_owned()).is_some() { return Err("Duplicate resource URI".into()); }
        }
    }
    let mut options = jsonschema::options().with_retriever(Resources(resources));
    options = match request.draft {
        0 => options, // Resolve an explicit custom meta-schema from the supplied resources.
        4 => options.with_draft(Draft::Draft4), 6 => options.with_draft(Draft::Draft6),
        7 => options.with_draft(Draft::Draft7), 2019 => options.with_draft(Draft::Draft201909),
        2020 => options.with_draft(Draft::Draft202012),
        _ => return Err("Unsupported schema dialect".into()),
    };
    if request.assert_formats {
        options = options.should_validate_formats(true);
        if let Some(callback) = request.format {
            let context = request.context as usize;
            for (name, id) in [("iiagent-legacy-date", 1), ("iiagent-legacy-time", 2),
                ("iiagent-legacy-host-name", 3), ("iiagent-legacy-ip-address", 4),
                ("iiagent-legacy-regex", 5), ("iiagent-legacy-color", 6)] {
                options = options.with_format(name, move |value| {
                    let span = Text { data: value.as_ptr(), size: value.len() };
                    unsafe { callback(context as *mut c_void, id, span) != 0 }
                });
            }
        }
    }
    let validator = options.build(&schema).map_err(|e| e.to_string())?;
    Ok(validator.is_valid(&instance))
}

/// FFI entry point: return 0 for valid, 1 for invalid, 2 for errors. No panic
/// may unwind into C++; output bytes are copied to the caller-owned buffer.
#[no_mangle]
pub unsafe extern "C" fn iiagent_precision_validate(request: *const Request, diagnostic: *mut u8, capacity: usize) -> i32 {
    let result = catch_unwind(AssertUnwindSafe(|| {
        if request.is_null() { return Err("Missing request".to_owned()); }
        run(&*request)
    }));
    let (status, message) = match result {
        Ok(Ok(true)) => (0, String::new()),
        Ok(Ok(false)) => (1, "Exact validation rejected the value".to_owned()),
        Ok(Err(error)) => (2, error),
        Err(_) => (2, "Exact validation failed".to_owned()),
    };
    if capacity != 0 && !diagnostic.is_null() {
        let bytes = message.as_bytes();
        let count = bytes.len().min(capacity - 1);
        std::ptr::copy_nonoverlapping(bytes.as_ptr(), diagnostic, count);
        *diagnostic.add(count) = 0;
    }
    status
}
