//! Standalone WebAssembly entry points for the KaTeX-compatible HTML/CSS
//! renderer. The module has no JavaScript imports or Emscripten glue: the
//! browser allocates an input buffer, calls `vt_render`, and reads the
//! returned UTF-8 bytes before `vt_free_result`.
//!
//! Protocol: the result begins with `O` for a successful document or `E` for
//! a JSON diagnostic (which also carries the KaTeX error markup when
//! `throwOnError` is false).

use katex::macro_expander::MacroMap;
use katex::macros::MacroDefinition;
use katex::types::{OutputFormat, StrictMode, StrictSetting};
use katex::{KatexContext, Settings, render_to_string};
use serde_json::Value;
use std::sync::OnceLock;

fn context() -> &'static KatexContext {
    static CONTEXT: OnceLock<KatexContext> = OnceLock::new();
    CONTEXT.get_or_init(KatexContext::default)
}

#[no_mangle]
pub extern "C" fn vt_alloc(len: usize) -> *mut u8 {
    let mut buffer = Vec::<u8>::with_capacity(len);
    let pointer = buffer.as_mut_ptr();
    std::mem::forget(buffer);
    pointer
}

/// # Safety
/// `pointer`/`capacity` must come from `vt_alloc`.
#[no_mangle]
pub unsafe extern "C" fn vt_free(pointer: *mut u8, capacity: usize) {
    if !pointer.is_null() && capacity > 0 {
        drop(Vec::from_raw_parts(pointer, 0, capacity));
    }
}

#[no_mangle]
pub extern "C" fn vt_result_len() -> usize {
    unsafe { RESULT_LEN }
}

/// # Safety
/// `pointer` must be the value returned by `vt_render` and not freed twice.
#[no_mangle]
pub unsafe extern "C" fn vt_free_result(pointer: *mut u8) {
    if !pointer.is_null() {
        drop(Vec::from_raw_parts(pointer, RESULT_LEN, RESULT_LEN));
    }
}

static mut RESULT_LEN: usize = 0;

fn json_bool(value: &Value, key: &str) -> Option<bool> {
    value.get(key).and_then(Value::as_bool)
}

fn json_str<'a>(value: &'a Value, key: &str) -> Option<&'a str> {
    value.get(key).and_then(Value::as_str)
}

fn json_number(value: &Value, key: &str) -> Option<f64> {
    value.get(key).and_then(Value::as_f64)
}

fn diagnostic_code(error: &katex::ParseError) -> &'static str {
    let text = format!("{:?}", error.kind);
    if text.contains("MacroTooManyExpansions") {
        "ExpansionLimit"
    } else if text.contains("UnsupportedWideCharacter")
        || text.contains("EmptyWideCharacterInput")
        || text.contains("FontMetricsNotFound")
    {
        "UnknownSymbol"
    } else if text.contains("Newcommand")
        || text.contains("Renewcommand")
        || text.contains("InvalidNewcommand")
        || text.contains("MacroDefinition")
        || text.contains("MacroDelimiter")
        || text.contains("MacroIncomplete")
        || text.contains("MacroStack")
        || text.contains("UnbalancedNamespace")
        || text.contains("InvalidMacro")
    {
        "InvalidMacroDefinition"
    } else if text.contains("ExtraCloseBrace") {
        "UnexpectedGroupEnd"
    } else if text.contains("ExpectedGroup")
        || text.contains("ExtraOpenBrace")
        || text.contains("Unclosed")
    {
        "UnclosedGroup"
    } else if text.contains("Unsupported") || text.contains("NoSuchEnvironment") {
        "UnsupportedCommand"
    } else {
        "UnexpectedToken"
    }
}

fn escape_json(text: &str) -> String {
    let mut result = String::with_capacity(text.len() + 2);
    result.push('"');
    for ch in text.chars() {
        match ch {
            '"' => result.push_str("\\\""),
            '\\' => result.push_str("\\\\"),
            '\n' => result.push_str("\\n"),
            '\r' => result.push_str("\\r"),
            '\t' => result.push_str("\\t"),
            c if (c as u32) < 0x20 => {
                use std::fmt::Write as _;
                let _ = write!(result, "\\u{:04x}", c as u32);
            }
            c => result.push(c),
        }
    }
    result.push('"');
    result
}

fn utf16_offset(source: &str, byte: usize) -> usize {
    let byte = byte.min(source.len());
    source[..byte].chars().map(char::len_utf16).sum()
}

fn utf16_length(source: &str, byte_start: usize, byte_length: usize) -> usize {
    let start = byte_start.min(source.len());
    let end = (byte_start.saturating_add(byte_length)).min(source.len());
    if start >= end {
        return 0;
    }
    source[start..end].chars().map(char::len_utf16).sum()
}

fn error_json(source: &str, error: &katex::ParseError, html: Option<&str>) -> String {
    let byte_start = error.position().unwrap_or(0);
    let byte_length = error.length().unwrap_or(0);
    let start = utf16_offset(source, byte_start);
    let length = utf16_length(source, byte_start, byte_length);
    let mut result = format!(
        "{{\"code\":\"{}\",\"message\":{},\"start\":{},\"length\":{}",
        diagnostic_code(error),
        escape_json(&error.to_string()),
        start,
        length
    );
    if let Some(html) = html {
        result.push_str(",\"html\":");
        result.push_str(&escape_json(html));
    }
    result.push('}');
    result
}

fn settings_from_json(options: &Value, output: OutputFormat) -> Settings {
    let mut settings = Settings::default();
    settings.output = output;
    settings.display_mode = json_bool(options, "displayMode").unwrap_or(false);
    settings.throw_on_error = json_bool(options, "throwOnError").unwrap_or(true);
    settings.leqno = json_bool(options, "leqno").unwrap_or(false);
    settings.fleqn = json_bool(options, "fleqn").unwrap_or(false);
    settings.color_is_text_color = json_bool(options, "colorIsTextColor").unwrap_or(false);
    settings.global_group = json_bool(options, "globalGroup").unwrap_or(false);
    settings.min_rule_thickness = json_number(options, "minRuleThickness").unwrap_or(0.0);
    settings.max_size = json_number(options, "maxSize").unwrap_or(f64::INFINITY);
    if let Some(max_expand) = options.get("maxExpand").and_then(Value::as_u64) {
        settings.max_expand = max_expand as usize;
    }
    if let Some(color) = json_str(options, "errorColor") {
        settings.error_color = color.to_owned();
    }
    if let Some(color) = json_str(options, "color") {
        settings.color = Some(color.to_owned());
    }
    settings.strict = match options.get("strict") {
        Some(Value::Bool(true)) => StrictSetting::Mode(StrictMode::Error),
        Some(Value::Bool(false)) => StrictSetting::Mode(StrictMode::Ignore),
        Some(Value::String(value)) => match value.as_str() {
            "error" => StrictSetting::Mode(StrictMode::Error),
            "ignore" => StrictSetting::Mode(StrictMode::Ignore),
            _ => StrictSetting::Mode(StrictMode::Warn),
        },
        _ => StrictSetting::Mode(StrictMode::Warn),
    };
    if let Some(macros) = options.get("macros").and_then(Value::as_object) {
        let map: &mut MacroMap = settings.macros.get_mut();
        for (name, body) in macros {
            if let Some(body) = body.as_str() {
                map.insert(name.clone(), MacroDefinition::String(body.to_owned()));
            }
        }
    }
    settings
}

fn render(source: &str, options: &Value, output: OutputFormat) -> Vec<u8> {
    let mut settings = settings_from_json(options, output);
    let soft_errors = !settings.throw_on_error;
    // Always ask the parser for a diagnostic first; when the caller requested
    // KaTeX-style readable errors, render the same source again with the
    // upstream error-markup mode.
    settings.throw_on_error = true;
    match render_to_string(context(), source, &settings) {
        Ok(markup) => {
            let mut buffer = Vec::with_capacity(markup.len() + 1);
            buffer.push(b'O');
            buffer.extend_from_slice(markup.as_bytes());
            buffer
        }
        Err(error) => {
            let html = if soft_errors {
                settings.throw_on_error = false;
                render_to_string(context(), source, &settings).ok()
            } else {
                None
            };
            let json = error_json(source, &error, html.as_deref());
            let mut buffer = Vec::with_capacity(json.len() + 1);
            buffer.push(b'E');
            buffer.extend_from_slice(json.as_bytes());
            buffer
        }
    }
}

/// Render a UTF-8 formula. `output` is 0 for KaTeX's htmlAndMathml, 1 for
/// HTML-only, and 2 for MathML-only. Returns a pointer to a leaked result
/// buffer; use `vt_result_len` and `vt_free_result`.
///
/// # Safety
/// `input`/`input_len` and `options`/`options_len` must describe valid
/// in-bounds UTF-8 byte ranges in the module memory.
#[no_mangle]
pub unsafe extern "C" fn vt_render(
    input: *const u8,
    input_len: usize,
    options: *const u8,
    options_len: usize,
    output: u32,
) -> *mut u8 {
    if input.is_null() || (!options.is_null() && options_len > 0 && options.is_null()) {
        return std::ptr::null_mut();
    }
    let source = match std::str::from_utf8(std::slice::from_raw_parts(input, input_len)) {
        Ok(source) => source,
        Err(_) => return std::ptr::null_mut(),
    };
    let options: Value = if options.is_null() || options_len == 0 {
        Value::Object(serde_json::Map::new())
    } else {
        match serde_json::from_slice(std::slice::from_raw_parts(options, options_len)) {
            Ok(value) => value,
            Err(_) => Value::Object(serde_json::Map::new()),
        }
    };
    let format = match output {
        1 => OutputFormat::Html,
        2 => OutputFormat::Mathml,
        _ => OutputFormat::HtmlAndMathml,
    };
    let mut result = render(source, &options, format);
    result.shrink_to_fit();
    RESULT_LEN = result.len();
    let pointer = result.as_mut_ptr();
    std::mem::forget(result);
    pointer
}

#[no_mangle]
pub extern "C" fn vt_version() -> u32 {
    1
}
