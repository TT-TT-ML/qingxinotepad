
#include "syntax.h"
#include <cwctype>
#include <algorithm>

/* ------------------------------------------------------------------ */
/*  Keyword tables (null-terminated arrays)                           */
/* ------------------------------------------------------------------ */

static const wchar_t *kwCpp[] = {
    L"alignas", L"alignof", L"and", L"asm", L"auto", L"bitand", L"bitor", L"bool",
    L"break", L"case", L"catch", L"char", L"class", L"const", L"constexpr", L"const_cast",
    L"continue", L"co_await", L"co_return", L"co_yield", L"default", L"delete", L"do",
    L"double", L"dynamic_cast", L"else", L"enum", L"explicit", L"export", L"extern",
    L"false", L"float", L"for", L"friend", L"goto", L"if", L"inline", L"int", L"long",
    L"mutable", L"namespace", L"new", L"noexcept", L"not", L"nullptr", L"operator",
    L"or", L"private", L"protected", L"public", L"register", L"reinterpret_cast",
    L"return", L"short", L"signed", L"sizeof", L"static", L"static_assert", L"static_cast",
    L"struct", L"switch", L"template", L"this", L"thread_local", L"throw", L"true",
    L"try", L"typedef", L"typeid", L"typename", L"union", L"unsigned", L"using", L"virtual",
    L"void", L"volatile", L"wchar_t", L"while", L"xor", L"concept", L"requires", L"if constexpr",
    nullptr};
static const wchar_t *tyCpp[] = {
    L"size_t", L"int8_t", L"int16_t", L"int32_t", L"int64_t", L"uint8_t", L"uint16_t",
    L"uint32_t", L"uint64_t", L"intptr_t", L"uintptr_t", L"ptrdiff_t", L"intmax_t",
    L"char16_t", L"char32_t", L"char8_t", L"FILE", L"string", L"wstring", L"u8string",
    L"vector", L"map", L"unordered_map", L"set", L"unordered_set", L"list", L"deque",
    L"array", L"pair", L"tuple", L"shared_ptr", L"unique_ptr", L"weak_ptr", L"optional",
    L"variant", L"function", L"string_view", L"wstring_view", L"span", L"filesystem",
    L"chrono", L"thread", L"mutex", L"iostream", L"fstream", L"sstream", L"stringstream",
    L"istream", L"ostream", L"ifstream", L"ofstream", L"uint64", L"int64",
    nullptr};
static const wchar_t *liCpp[] = {L"true", L"false", L"nullptr", L"NULL", L"this", nullptr};

static const wchar_t *kwC[] = {
    L"auto", L"break", L"case", L"char", L"const", L"continue", L"default", L"do",
    L"double", L"else", L"enum", L"extern", L"float", L"for", L"goto", L"if", L"inline",
    L"int", L"long", L"register", L"restrict", L"return", L"short", L"signed", L"sizeof",
    L"static", L"struct", L"switch", L"typedef", L"union", L"unsigned", L"void", L"volatile",
    L"while", L"_Bool", L"_Alignas", L"_Alignof", L"_Atomic", L"_Generic", L"_Noreturn",
    L"_Static_assert", L"_Thread_local", L"constexpr", L"true", L"false", nullptr};
static const wchar_t *tyC[] = {
    L"size_t", L"int8_t", L"int16_t", L"int32_t", L"int64_t", L"uint8_t", L"uint16_t",
    L"uint32_t", L"uint64_t", L"intptr_t", L"uintptr_t", L"ptrdiff_t", L"FILE", L"intmax_t",
    nullptr};
static const wchar_t *liC[] = {L"true", L"false", L"NULL", nullptr};

static const wchar_t *kwCs[] = {
    L"abstract", L"as", L"base", L"bool", L"break", L"byte", L"case", L"catch", L"char",
    L"checked", L"class", L"const", L"continue", L"decimal", L"default", L"delegate",
    L"do", L"double", L"else", L"enum", L"event", L"explicit", L"extern", L"false",
    L"finally", L"fixed", L"float", L"for", L"foreach", L"goto", L"if", L"implicit",
    L"in", L"int", L"interface", L"internal", L"is", L"lock", L"long", L"namespace",
    L"new", L"null", L"object", L"operator", L"out", L"override", L"params", L"private",
    L"protected", L"public", L"readonly", L"ref", L"return", L"sbyte", L"sealed", L"short",
    L"sizeof", L"stackalloc", L"static", L"string", L"struct", L"switch", L"this", L"throw",
    L"true", L"try", L"typeof", L"uint", L"ulong", L"unchecked", L"unsafe", L"ushort",
    L"using", L"virtual", L"void", L"volatile", L"while", L"async", L"await", L"record",
    L"init", L"required", L"with", L"var", L"dynamic", nullptr};
static const wchar_t *tyCs[] = {
    L"void", L"int", L"long", L"short", L"byte", L"sbyte", L"uint", L"ulong", L"ushort",
    L"bool", L"char", L"string", L"object", L"decimal", L"double", L"float", L"var",
    L"dynamic", L"nint", L"nuint", L"Task", L"Task<T>", L"Action", L"Func", L"List",
    L"Dictionary", L"HashSet", L"Queue", L"Stack", L"Array", L"StringBuilder", nullptr};
static const wchar_t *liCs[] = {L"true", L"false", L"null", nullptr};

static const wchar_t *kwJava[] = {
    L"abstract", L"assert", L"boolean", L"break", L"byte", L"case", L"catch", L"char",
    L"class", L"const", L"continue", L"default", L"do", L"double", L"else", L"enum",
    L"extends", L"final", L"finally", L"float", L"for", L"goto", L"if", L"implements",
    L"import", L"instanceof", L"int", L"interface", L"long", L"native", L"new", L"package",
    L"private", L"protected", L"public", L"return", L"short", L"static", L"strictfp",
    L"super", L"switch", L"synchronized", L"this", L"throw", L"throws", L"transient",
    L"try", L"void", L"volatile", L"while", L"true", L"false", L"null", L"var",
    L"record", L"sealed", L"permits", L"non-sealed", L"yield", nullptr};
static const wchar_t *tyJava[] = {
    L"String", L"Integer", L"Long", L"Short", L"Byte", L"Character", L"Double", L"Float",
    L"Boolean", L"Object", L"Void", L"List", L"ArrayList", L"LinkedList", L"Map", L"HashMap",
    L"Set", L"HashSet", L"TreeSet", L"Collection", L"Queue", L"Deque", L"Stream", L"Optional",
    L"StringBuilder", L"System", L"Math", L"Exception", L"RuntimeException", L"Throwable",
    nullptr};
static const wchar_t *liJava[] = {L"true", L"false", L"null", nullptr};

static const wchar_t *kwJs[] = {
    L"break", L"case", L"catch", L"class", L"const", L"continue", L"debugger", L"default",
    L"delete", L"do", L"else", L"export", L"extends", L"finally", L"for", L"function",
    L"if", L"import", L"in", L"instanceof", L"let", L"new", L"of", L"return", L"static",
    L"super", L"switch", L"this", L"throw", L"try", L"typeof", L"var", L"void", L"while",
    L"with", L"yield", L"async", L"await", L"true", L"false", L"null", L"undefined",
    L"arguments", L"enum", L"implements", L"interface", L"package", L"private", L"protected",
    L"public", L"readonly", L"get", L"set", nullptr};
static const wchar_t *tyJs[] = {
    L"Number", L"String", L"Boolean", L"Object", L"Array", L"Function", L"Date", L"RegExp",
    L"Error", L"Promise", L"Map", L"Set", L"Symbol", L"BigInt", L"Math", L"JSON", L"ArrayBuffer",
    L"Uint8Array", L"Int32Array", L"Float64Array", L"console", L"window", L"document",
    nullptr};
static const wchar_t *liJs[] = {L"true", L"false", L"null", L"undefined", L"NaN", L"Infinity", nullptr};

static const wchar_t *kwTs[] = {
    L"break", L"case", L"catch", L"class", L"const", L"continue", L"debugger", L"default",
    L"delete", L"do", L"else", L"enum", L"export", L"extends", L"finally", L"for", L"function",
    L"if", L"import", L"in", L"instanceof", L"let", L"new", L"of", L"return", L"static",
    L"super", L"switch", L"this", L"throw", L"try", L"typeof", L"var", L"void", L"while",
    L"with", L"yield", L"async", L"await", L"abstract", L"declare", L"implements", L"interface",
    L"namespace", L"private", L"protected", L"public", L"readonly", L"type", L"as", L"keyof",
    L"infer", L"never", L"unknown", L"asserts", L"is", L"satisfies", L"true", L"false",
    L"null", L"undefined", nullptr};
static const wchar_t *tyTs[] = {
    L"number", L"string", L"boolean", L"object", L"any", L"void", L"unknown", L"never",
    L"bigint", L"symbol", L"Array", L"Promise", L"Function", L"Date", L"Map", L"Set",
    L"Record", L"Partial", L"Required", L"Readonly", L"Pick", L"Omit", L"Exclude",
    L"Extract", L"ReturnType", L"Parameters", L"React", L"HTMLElement", nullptr};
static const wchar_t *liTs[] = {L"true", L"false", L"null", L"undefined", L"NaN", nullptr};

static const wchar_t *kwPy[] = {
    L"and", L"as", L"assert", L"async", L"await", L"break", L"class", L"continue", L"def",
    L"del", L"elif", L"else", L"except", L"finally", L"for", L"from", L"global", L"if",
    L"import", L"in", L"is", L"lambda", L"nonlocal", L"not", L"or", L"pass", L"raise",
    L"return", L"try", L"while", L"with", L"yield", L"match", L"case", L"True", L"False",
    L"None", nullptr};
static const wchar_t *tyPy[] = {
    L"int", L"float", L"bool", L"str", L"bytes", L"bytearray", L"list", L"tuple", L"dict",
    L"set", L"frozenset", L"object", L"type", L"range", L"slice", L"complex", L"super",
    L"property", L"classmethod", L"staticmethod", L"enumerate", L"map", L"filter", L"zip",
    L"iter", L"repr", L"len", L"print", L"input", L"open", L"isinstance", L"issubclass",
    L"hash", L"abs", L"min", L"max", L"round", L"sorted", L"reversed", L"sum", L"any",
    L"all", L"id", L"callable", L"chr", L"ord", L"divmod", L"pow", L"format", L"frozenset",
    L"memoryview", L"next", L"vars", L"dir", L"getattr", L"setattr", L"hasattr", L"delattr",
    L"self", nullptr};
static const wchar_t *liPy[] = {L"True", L"False", L"None", L"NotImplemented", L"Ellipsis", nullptr};

static const wchar_t *kwHtml[] = {
    L"html", L"head", L"body", L"title", L"meta", L"link", L"script", L"style", L"div",
    L"span", L"p", L"a", L"img", L"br", L"h1", L"h2", L"h3", L"h4", L"h5", L"h6",
    L"ul", L"ol", L"li", L"table", L"thead", L"tbody", L"tr", L"th", L"td", L"form",
    L"input", L"button", L"textarea", L"select", L"option", L"label", L"iframe", L"video",
    L"audio", L"canvas", L"section", L"article", L"header", L"footer", L"nav", L"aside",
    L"main", L"figure", L"figcaption", L"blockquote", L"code", L"pre", L"em", L"strong",
    L"small", L"mark", L"del", L"ins", L"sub", L"sup", L"b", L"i", L"u", L"s", L"hr",
    L"nav", L"embed", L"object", L"param", L"source", L"track", L"details", L"summary",
    L"datalist", L"fieldset", L"legend", L"optgroup", L"col", L"colgroup", L"caption",
    L"dt", L"dd", L"dl", L"address", L"time", L"data", L"output", L"progress", L"meter",
    L"area", L"map", L"abbr", L"bdi", L"bdo", L"cite", L"dfn", L"kbd", L"q", L"rp", L"rt",
    L"ruby", L"samp", L"var", L"wbr", nullptr};
static const wchar_t *tyHtml[] = {nullptr};
static const wchar_t *liHtml[] = {nullptr};

static const wchar_t *kwCss[] = {
    L"media", L"import", L"keyframes", L"important", L"from", L"to", L"hover", L"active",
    L"focus", L"visited", L"first-child", L"last-child", L"nth-child", L"not", L"and",
    L"only", L"or", nullptr};
static const wchar_t *tyCss[] = {
    L"color", L"background", L"background-color", L"background-image", L"background-size",
    L"border", L"border-radius", L"margin", L"padding", L"width", L"height", L"min-width",
    L"max-width", L"min-height", L"max-height", L"display", L"position", L"top", L"right",
    L"bottom", L"left", L"z-index", L"float", L"clear", L"overflow", L"overflow-x",
    L"overflow-y", L"font", L"font-size", L"font-family", L"font-weight", L"line-height",
    L"text-align", L"text-decoration", L"text-transform", L"text-shadow", L"letter-spacing",
    L"word-spacing", L"vertical-align", L"white-space", L"cursor", L"opacity", L"transform",
    L"transition", L"animation", L"box-shadow", L"box-sizing", L"flex", L"flex-direction",
    L"flex-wrap", L"justify-content", L"align-items", L"align-content", L"grid",
    L"grid-template-columns", L"gap", L"content", L"visibility", L"clip", L"filter",
    L"outline", L"outline-color", L"outline-width", L"list-style", L"table-layout",
    L"border-collapse", L"border-spacing", nullptr};
static const wchar_t *liCss[] = {nullptr};

static const wchar_t *kwPhp[] = {
    L"abstract", L"and", L"array", L"as", L"break", L"callable", L"case", L"catch", L"class",
    L"clone", L"const", L"continue", L"declare", L"default", L"die", L"do", L"echo", L"else",
    L"elseif", L"empty", L"enddeclare", L"endfor", L"endforeach", L"endif", L"endswitch",
    L"endwhile", L"eval", L"exit", L"extends", L"final", L"finally", L"fn", L"for", L"foreach",
    L"function", L"global", L"goto", L"if", L"implements", L"include", L"include_once",
    L"instanceof", L"insteadof", L"interface", L"isset", L"list", L"match", L"namespace",
    L"new", L"or", L"print", L"private", L"protected", L"public", L"readonly", L"require",
    L"require_once", L"return", L"static", L"switch", L"throw", L"trait", L"try", L"unset",
    L"use", L"var", L"while", L"xor", L"yield", L"true", L"false", L"null", nullptr};
static const wchar_t *tyPhp[] = {
    L"int", L"float", L"bool", L"string", L"array", L"object", L"mixed", L"callable",
    L"void", L"iterable", L"self", L"parent", L"$this", L"stdClass", L"Exception",
    L"Throwable", L"Closure", nullptr};
static const wchar_t *liPhp[] = {L"true", L"false", L"null", L"__FILE__", L"__LINE__", L"__DIR__", nullptr};

static const wchar_t *kwGo[] = {
    L"break", L"case", L"chan", L"const", L"continue", L"default", L"defer", L"else",
    L"fallthrough", L"for", L"func", L"go", L"goto", L"if", L"import", L"interface", L"map",
    L"package", L"range", L"return", L"select", L"struct", L"switch", L"type", L"var",
    nullptr};
static const wchar_t *tyGo[] = {
    L"bool", L"byte", L"complex64", L"complex128", L"error", L"float32", L"float64",
    L"int", L"int8", L"int16", L"int32", L"int64", L"rune", L"string", L"uint", L"uint8",
    L"uint16", L"uint32", L"uint64", L"uintptr", L"any", L"comparable", L"string",
    nullptr};
static const wchar_t *liGo[] = {L"true", L"false", L"nil", L"iota", nullptr};

static const wchar_t *kwRust[] = {
    L"as", L"async", L"await", L"break", L"const", L"continue", L"crate", L"dyn", L"else",
    L"enum", L"extern", L"false", L"fn", L"for", L"if", L"impl", L"in", L"let", L"loop",
    L"match", L"mod", L"move", L"mut", L"pub", L"ref", L"return", L"self", L"Self",
    L"static", L"struct", L"super", L"trait", L"true", L"type", L"unsafe", L"use", L"where",
    L"while", nullptr};
static const wchar_t *tyRust[] = {
    L"bool", L"char", L"str", L"f32", L"f64", L"i8", L"i16", L"i32", L"i64", L"i128",
    L"isize", L"u8", L"u16", L"u32", L"u64", L"u128", L"usize", L"String", L"Vec",
    L"Option", L"Result", L"Box", L"dyn", L"impl", L"CString", L"Rc", L"Arc", L"HashMap",
    L"HashSet", L"BTreeMap", L"BTreeSet", L"Iterator", L"Error", nullptr};
static const wchar_t *liRust[] = {L"true", L"false", L"None", L"Some", L"Ok", L"Err", nullptr};

static const wchar_t *kwLua[] = {
    L"and", L"break", L"do", L"else", L"elseif", L"end", L"false", L"for", L"function",
    L"goto", L"if", L"in", L"local", L"nil", L"not", L"or", L"repeat", L"return", L"then",
    L"true", L"until", L"while", nullptr};
static const wchar_t *tyLua[] = {
    L"string", L"number", L"boolean", L"table", L"function", L"thread", L"userdata",
    L"nil", L"math", L"io", L"os", L"string", L"table", L"coroutine", L"require",
    L"pcall", L"xpcall", L"pairs", L"ipairs", L"next", L"print", L"type", L"tonumber",
    L"tostring", nullptr};
static const wchar_t *liLua[] = {L"true", L"false", L"nil", nullptr};

static const wchar_t *kwSql[] = {
    L"select", L"insert", L"update", L"delete", L"from", L"where", L"group", L"by",
    L"having", L"order", L"join", L"left", L"right", L"inner", L"outer", L"full", L"on",
    L"as", L"and", L"or", L"not", L"null", L"is", L"in", L"like", L"between", L"case",
    L"when", L"then", L"else", L"end", L"union", L"all", L"distinct", L"limit", L"offset",
    L"create", L"alter", L"drop", L"table", L"index", L"view", L"procedure", L"function",
    L"trigger", L"primary", L"key", L"foreign", L"references", L"default", L"check",
    L"constraint", L"values", L"set", L"into", L"returning", L"with", L"recursive",
    L"over", L"partition", L"rank", L"row_number", L"count", L"sum", L"avg", L"min",
    L"max", L"exists", L"any", L"all", L"some", L"unique", L"asc", L"desc", L"true",
    L"false", nullptr};
static const wchar_t *tySql[] = {
    L"int", L"integer", L"bigint", L"smallint", L"tinyint", L"decimal", L"numeric",
    L"float", L"real", L"double", L"varchar", L"char", L"text", L"nvarchar", L"nchar",
    L"date", L"datetime", L"timestamp", L"time", L"boolean", L"bool", L"blob", L"binary",
    L"uuid", L"json", L"jsonb", nullptr};
static const wchar_t *liSql[] = {L"true", L"false", L"null", L"NULL", nullptr};

static const wchar_t *kwSh[] = {
    L"if", L"then", L"elif", L"else", L"fi", L"case", L"esac", L"for", L"while", L"until",
    L"do", L"done", L"function", L"select", L"in", L"local", L"export", L"readonly",
    L"shift", L"source", L"return", L"break", L"continue", L"exit", L"set", L"unset",
    L"eval", L"exec", L"trap", L"true", L"false", L"declare", L"typeset", L"alias",
    L"bg", L"bind", L"builtin", L"cd", L"command", L"compgen", L"complete", L"disown",
    L"enable", L"help", L"history", L"jobs", L"kill", L"let", L"logout", L"popd",
    L"printf", L"pushd", L"pwd", L"read", L"ulimit", L"umask", L"wait", nullptr};
static const wchar_t *tySh[] = {nullptr};
static const wchar_t *liSh[] = {L"true", L"false", L"null", nullptr};

static const wchar_t *kwBat[] = {
    L"call", L"goto", L"if", L"else", L"for", L"in", L"do", L"set", L"setlocal",
    L"endlocal", L"exit", L"rem", L"shift", L"choice", L"pause", L"echo", L"cd", L"chdir",
    L"copy", L"del", L"erase", L"md", L"mkdir", L"rd", L"rmdir", L"ren", L"rename",
    L"type", L"title", L"color", L"cls", L"date", L"time", L"path", L"pushd", L"popd",
    L"where", L"start", L"timeout", L"ping", L"find", L"findstr", L"more", L"sort",
    L"xcopy", L"robocopy", L"tasklist", L"taskkill", L"reg", L"regadd", L"regdel",
    nullptr};
static const wchar_t *tyBat[] = {nullptr};
static const wchar_t *liBat[] = {nullptr};

static const wchar_t *kwJson[] = {nullptr};
static const wchar_t *tyJson[] = {nullptr};
static const wchar_t *liJson[] = {L"true", L"false", L"null", nullptr};

static const wchar_t *kwXml[] = {
    L"xml", L"version", L"encoding", L"standalone", L"DOCTYPE", L"ENTITY", L"NOTATION",
    L"CDATA", L"PCDATA", L"xsl", L"template", L"match", L"select", L"value-of",
    L"for-each", L"if", L"choose", L"when", L"otherwise", L"variable", L"param",
    nullptr};
static const wchar_t *tyXml[] = {nullptr};
static const wchar_t *liXml[] = {nullptr};

/* Markdown */
static const wchar_t *kwMd[] = {nullptr};
static const wchar_t *tyMd[] = {nullptr};
static const wchar_t *liMd[] = {nullptr};

/* YAML */
static const wchar_t *kwYaml[] = {
    L"true", L"false", L"null", L"yes", L"no", L"on", L"off", L"None", L"True",
    L"False", L"Null", nullptr};
static const wchar_t *tyYaml[] = {nullptr};
static const wchar_t *liYaml[] = {L"true", L"false", L"null", L"None", L"True", L"False", L"Null", nullptr};

/* INI */
static const wchar_t *kwIni[] = {nullptr};
static const wchar_t *tyIni[] = {nullptr};
static const wchar_t *liIni[] = {nullptr};

/* TOML */
static const wchar_t *kwToml[] = {nullptr};
static const wchar_t *tyToml[] = {nullptr};
static const wchar_t *liToml[] = {nullptr};

/* Ruby */
static const wchar_t *kwRuby[] = {
    L"alias", L"and", L"begin", L"break", L"case", L"class", L"def", L"defined?",
    L"do", L"else", L"elsif", L"end", L"ensure", L"false", L"for", L"if", L"in",
    L"module", L"next", L"nil", L"not", L"or", L"redo", L"rescue", L"retry",
    L"return", L"self", L"super", L"then", L"true", L"undef", L"unless", L"until",
    L"when", L"while", L"yield", nullptr};
static const wchar_t *tyRuby[] = {
    L"String", L"Integer", L"Float", L"Array", L"Hash", L"Symbol", L"Proc",
    L"Method", L"Class", L"Object", L"Numeric", L"NilClass", nullptr};
static const wchar_t *liRuby[] = {L"true", L"false", L"nil", nullptr};

/* Swift */
static const wchar_t *kwSwift[] = {
    L"var", L"let", L"func", L"if", L"else", L"guard", L"switch", L"case",
    L"default", L"for", L"while", L"repeat", L"return", L"class", L"struct",
    L"enum", L"protocol", L"extension", L"import", L"init", L"deinit", L"public",
    L"private", L"internal", L"fileprivate", L"open", L"static", L"override",
    L"in", L"as", L"is", L"where", L"do", L"catch", L"throw", L"throws", L"try",
    L"await", L"async", L"nil", L"true", L"false", L"self", L"super", L"break",
    L"continue", L"defer", L"fallthrough", L"typealias", L"associatedtype",
    L"mutating", L"nonmutating", L"lazy", L"weak", L"unowned", L"some", L"any",
    nullptr};
static const wchar_t *tySwift[] = {
    L"Int", L"Int32", L"Int64", L"UInt", L"Double", L"Float", L"Bool", L"String",
    L"Character", L"Void", L"Any", L"AnyObject", L"Array", L"Dictionary", L"Set",
    L"Optional", L"Error", L"URL", L"Date", L"Data", L"CGFloat", L"CGPoint",
    L"CGSize", L"CGRect", nullptr};
static const wchar_t *liSwift[] = {L"true", L"false", L"nil", nullptr};

/* Kotlin */
static const wchar_t *kwKotlin[] = {
    L"val", L"var", L"fun", L"if", L"else", L"when", L"for", L"while", L"do",
    L"return", L"class", L"object", L"interface", L"enum", L"data", L"sealed",
    L"abstract", L"open", L"override", L"internal", L"private", L"protected",
    L"public", L"inline", L"suspend", L"operator", L"lateinit", L"init",
    L"constructor", L"import", L"package", L"null", L"true", L"false", L"this",
    L"super", L"is", L"in", L"as", L"where", L"throw", L"try", L"catch", L"finally",
    L"break", L"continue", L"companion", L"lazy", L"by", L"vararg", L"reified",
    L"crossinline", L"noinline", L"tailrec", nullptr};
static const wchar_t *tyKotlin[] = {
    L"Int", L"Long", L"Short", L"Byte", L"Double", L"Float", L"Boolean", L"Char",
    L"String", L"Unit", L"Any", L"Nothing", L"List", L"MutableList", L"Set",
    L"Map", L"Array", L"Pair", L"Triple", L"Range", L"IntRange", L"Exception",
    L"Throwable", L"Any?", nullptr};
static const wchar_t *liKotlin[] = {L"true", L"false", L"null", nullptr};

/* Dart */
static const wchar_t *kwDart[] = {
    L"void", L"var", L"final", L"const", L"class", L"extends", L"implements",
    L"with", L"mixin", L"abstract", L"interface", L"enum", L"extension",
    L"typedef", L"return", L"if", L"else", L"switch", L"case", L"default", L"for",
    L"while", L"do", L"break", L"continue", L"try", L"catch", L"on", L"finally",
    L"throw", L"rethrow", L"assert", L"import", L"export", L"part", L"library",
    L"this", L"super", L"null", L"true", L"false", L"async", L"await", L"yield",
    L"static", L"factory", L"get", L"set", L"operator", L"covariant", L"required",
    L"late", L"new", nullptr};
static const wchar_t *tyDart[] = {
    L"int", L"double", L"num", L"bool", L"String", L"Object", L"dynamic", L"List",
    L"Map", L"Set", L"Function", L"Future", L"Stream", L"DateTime", L"Duration",
    L"Error", L"Exception", L"Record", L"Never", nullptr};
static const wchar_t *liDart[] = {L"true", L"false", L"null", nullptr};

/* PowerShell */
static const wchar_t *kwPs[] = {
    L"function", L"filter", L"param", L"begin", L"process", L"end", L"if", L"else",
    L"elseif", L"switch", L"foreach", L"for", L"while", L"do", L"until", L"return",
    L"break", L"continue", L"throw", L"try", L"catch", L"finally", L"trap", L"class",
    L"enum", L"using", L"namespace", L"in", L"this", L"global", L"local", L"script",
    L"private", L"public", L"static", L"hidden", L"readonly", L"const", L"default",
    L"exit", L"where", L"not", L"and", L"or", L"xor", L"true", L"false", L"data",
    L"dynamicparam", L"workflow", L"parallel", L"sequence", L"inlinescript",
    L"configuration", L"using", nullptr};
static const wchar_t *tyPs[] = {
    L"$true", L"$false", L"$null", L"String", L"Int", L"Int32", L"Int64", L"Double",
    L"Single", L"Bool", L"Boolean", L"DateTime", L"Object", L"Array", L"Hashtable",
    L"PSCustomObject", L"SwitchParameter", L"Version", L"Guid", L"Char", L"Byte",
    nullptr};
static const wchar_t *liPs[] = {L"$true", L"$false", L"$null", nullptr};

/* Perl */
static const wchar_t *kwPerl[] = {
    L"if", L"elsif", L"else", L"unless", L"while", L"until", L"for", L"foreach",
    L"do", L"sub", L"return", L"my", L"local", L"our", L"use", L"require",
    L"package", L"bless", L"die", L"warn", L"print", L"printf", L"chomp", L"chop",
    L"push", L"pop", L"shift", L"unshift", L"splice", L"grep", L"map", L"sort",
    L"join", L"split", L"open", L"close", L"defined", L"undef", L"ref", L"exists",
    L"delete", L"keys", L"values", L"each", L"caller", L"wantarray", L"next",
    L"last", L"redo", L"continue", L"goto", L"eq", L"ne", L"lt", L"gt", L"le",
    L"ge", L"cmp", L"and", L"or", L"not", L"xor", L"true", L"false", L"state",
    L"proto", L"format", L"foreach", nullptr};
static const wchar_t *tyPerl[] = {
    L"scalar", L"array", L"hash", L"ref", L"glob", L"filehandle", L"CODE", L"HASH",
    L"ARRAY", L"SCALAR", L"GLOB", L"IO", L"Regexp", nullptr};
static const wchar_t *liPerl[] = {L"true", L"false", L"undef", nullptr};

/* R */
static const wchar_t *kwR[] = {
    L"function", L"if", L"else", L"repeat", L"while", L"for", L"in", L"next",
    L"break", L"TRUE", L"FALSE", L"NULL", L"NA", L"NaN", L"Inf", L"return",
    L"library", L"require", L"source", L"print", L"cat", L"c", L"list",
    L"data.frame", L"function", L"switch", L"else", nullptr};
static const wchar_t *tyR[] = {
    L"integer", L"numeric", L"double", L"character", L"logical", L"complex",
    L"raw", L"list", L"factor", L"data.frame", L"matrix", L"array", L"vector",
    L"function", L"environment", L"expression", L"pairlist", L"symbol",
    nullptr};
static const wchar_t *liR[] = {L"TRUE", L"FALSE", L"NULL", L"NA", L"NaN", L"Inf", nullptr};

/* Haskell */
static const wchar_t *kwHs[] = {
    L"case", L"class", L"data", L"default", L"deriving", L"do", L"else",
    L"foreign", L"if", L"import", L"in", L"infix", L"infixl", L"infixr",
    L"instance", L"let", L"module", L"newtype", L"of", L"then", L"type",
    L"where", L"qualified", L"as", L"hiding", L"family", L"forall",
    nullptr};
static const wchar_t *tyHs[] = {
    L"Int", L"Integer", L"Float", L"Double", L"Bool", L"Char", L"String",
    L"IO", L"Maybe", L"Either", L"List", L"Ordering", L"FilePath",
    nullptr};
static const wchar_t *liHs[] = {L"True", L"False", nullptr};

/* Scala */
static const wchar_t *kwScala[] = {
    L"abstract", L"case", L"catch", L"class", L"def", L"do", L"else",
    L"extends", L"false", L"final", L"finally", L"for", L"forSome", L"if",
    L"implicit", L"import", L"lazy", L"match", L"new", L"null", L"object",
    L"override", L"package", L"private", L"protected", L"return", L"sealed",
    L"super", L"this", L"throw", L"trait", L"try", L"true", L"type", L"val",
    L"var", L"while", L"with", L"yield", nullptr};
static const wchar_t *tyScala[] = {
    L"Int", L"Long", L"Short", L"Byte", L"Double", L"Float", L"Boolean",
    L"Char", L"String", L"Unit", L"Any", L"AnyVal", L"AnyRef", L"Nothing",
    L"List", L"Map", L"Set", L"Option", L"Some", L"None", L"Seq", L"Array",
    L"Either", L"Future", L"Try", L"PartialFunction", nullptr};
static const wchar_t *liScala[] = {L"true", L"false", L"null", nullptr};

/* Groovy */
static const wchar_t *kwGroovy[] = {
    L"def", L"as", L"in", L"if", L"else", L"for", L"while", L"do", L"switch",
    L"case", L"default", L"break", L"continue", L"return", L"throw", L"try",
    L"catch", L"finally", L"class", L"interface", L"enum", L"trait",
    L"extends", L"implements", L"static", L"public", L"private", L"protected",
    L"abstract", L"final", L"synchronized", L"native", L"void", L"import",
    L"package", L"new", L"this", L"super", L"true", L"false", L"null",
    L"assert", L"instanceof", L"groovydoc", nullptr};
static const wchar_t *tyGroovy[] = {
    L"int", L"long", L"short", L"byte", L"double", L"float", L"boolean",
    L"char", L"String", L"Object", L"List", L"Map", L"Set", L"Closure",
    L"BigDecimal", L"Integer", L"Boolean", L"Date", L"File", nullptr};
static const wchar_t *liGroovy[] = {L"true", L"false", L"null", nullptr};

/* Objective-C */
static const wchar_t *kwObjC[] = {
    L"id", L"self", L"super", L"nil", L"Nil", L"YES", L"NO", L"BOOL",
    L"instancetype", L"void", L"char", L"int", L"short", L"long", L"float",
    L"double", L"unsigned", L"signed", L"struct", L"union", L"enum",
    L"typedef", L"const", L"static", L"extern", L"inline", L"@interface",
    L"@implementation", L"@protocol", L"@end", L"@property", L"@synthesize",
    L"@dynamic", L"@selector", L"@class", L"@public", L"@private",
    L"@protected", L"@try", L"@catch", L"@finally", L"@throw",
    L"@autoreleasepool", L"return", L"if", L"else", L"for", L"while", L"do",
    L"switch", L"case", L"default", L"break", L"continue", L"goto",
    L"sizeof", L"new", L"alloc", L"init", L"copy", L"retain", L"release",
    L"autorelease", L"dealloc", nullptr};
static const wchar_t *tyObjC[] = {
    L"NSString", L"NSArray", L"NSDictionary", L"NSSet", L"NSNumber",
    L"NSData", L"NSDate", L"NSObject", L"NSInteger", L"NSUInteger",
    L"CGFloat", L"CGPoint", L"CGSize", L"CGRect", L"NSURL", L"NSThread",
    L"NSNotification", L"NSBundle", L"NSError", nullptr};
static const wchar_t *liObjC[] = {L"nil", L"Nil", L"YES", L"NO", nullptr};

/* VB.NET */
static const wchar_t *kwVb[] = {
    L"Public", L"Private", L"Friend", L"Protected", L"Shared", L"Dim", L"Const",
    L"As", L"If", L"Then", L"Else", L"ElseIf", L"End", L"Select", L"Case",
    L"For", L"Each", L"Next", L"While", L"Wend", L"Do", L"Loop", L"Until",
    L"Exit", L"Function", L"Sub", L"Return", L"Try", L"Catch", L"Finally",
    L"Throw", L"Module", L"Class", L"Structure", L"Enum", L"Interface",
    L"Implements", L"Inherits", L"Overrides", L"Overloads", L"MustOverride",
    L"NotOverridable", L"ReadOnly", L"WriteOnly", L"Property", L"Get", L"Set",
    L"New", L"Nothing", L"True", L"False", L"And", L"Or", L"Not", L"Xor",
    L"ByVal", L"ByRef", L"Optional", L"ParamArray", L"With", L"Using",
    L"Imports", L"Namespace", L"Option", L"Explicit", L"Strict", L"Compare",
    L"Continue", L"GoTo", L"On", L"Error", L"Resume", L"Me", L"MyBase",
    L"MyClass", L"ReDim", L"TypeOf", L"Is", L"IsNot", L"Handles",
    L"AddressOf", nullptr};
static const wchar_t *tyVb[] = {
    L"Integer", L"Long", L"Short", L"Byte", L"Double", L"Single", L"Decimal",
    L"Boolean", L"Char", L"String", L"Date", L"Object", L"Integer",
    L"UInteger", L"ULong", L"UShort", L"Array", L"Exception",
    nullptr};
static const wchar_t *liVb[] = {L"True", L"False", L"Nothing", nullptr};

/* CMake */
static const wchar_t *kwCmake[] = {
    L"cmake_minimum_required", L"project", L"set", L"unset", L"if", L"elseif", L"else",
    L"endif", L"while", L"endwhile", L"foreach", L"endforeach", L"function", L"endfunction",
    L"macro", L"endmacro", L"return", L"break", L"continue", L"include", L"include_guard",
    L"add_executable", L"add_library", L"add_subdirectory", L"add_custom_target",
    L"add_custom_command", L"add_compile_options", L"add_definitions", L"add_dependencies",
    L"add_test", L"aux_source_directory", L"include_directories", L"target_include_directories",
    L"target_link_libraries", L"link_libraries", L"target_compile_definitions",
    L"target_compile_features", L"target_compile_options", L"target_sources", L"target_precompile_headers",
    L"find_package", L"find_path", L"find_library", L"find_file", L"find_program",
    L"install", L"configure_file", L"file", L"option", L"set_target_properties",
    L"get_target_property", L"get_property", L"set_property", L"get_filename_component",
    L"string", L"list", L"math", L"message", L"enable_testing", L"cmake_policy",
    L"enable_language", L"export", L"try_compile", L"try_run", L"execute_process",
    L"separate_arguments", L"define_property", L"remove_definitions", L"mark_as_advanced",
    L"check_cxx_compiler_flag", L"check_c_compiler_flag", L"check_function_exists",
    L"include_regular_expression", L"variable_watch", L"site_name", L"install",
    nullptr};
static const wchar_t *tyCmake[] = {
    L"WIN32", L"UNIX", L"APPLE", L"MSVC", L"GNU", L"CYGWIN", L"MSYS", L"CMAKE_HOST_WIN32",
    L"CMAKE_HOST_UNIX", L"ANDROID", L"EMSCRIPTEN", L"WINDOWS", L"LINUX",
    nullptr};
static const wchar_t *liCmake[] = {L"TRUE", L"FALSE", L"ON", L"OFF", nullptr};

/* qmake */
static const wchar_t *kwQmake[] = {
    L"TEMPLATE", L"TARGET", L"CONFIG", L"QT", L"SOURCES", L"HEADERS", L"FORMS", L"RESOURCES",
    L"DEFINES", L"INCLUDEPATH", L"LIBS", L"DESTDIR", L"OBJECTS_DIR", L"MOC_DIR", L"RCC_DIR",
    L"UI_DIR", L"TRANSLATIONS", L"VERSION", L"QMAKE_CXXFLAGS", L"QMAKE_CFLAGS", L"QMAKE_LFLAGS",
    L"QMAKE_CXXFLAGS_RELEASE", L"QMAKE_CXXFLAGS_DEBUG", L"win32", L"unix", L"macx", L"android",
    L"debug", L"release", L"app", L"lib", L"plugin", L"subdirs", L"console", L"widgets",
    L"qt", L"gui", L"network", L"sql", L"xml", L"testlib", L"core", L"opengl", L"multimedia",
    L"equals", L"contains", L"exists", L"count", L"isEmpty", L"defineReplace", L"defineTest",
    L"message", L"warning", L"error", L"include",
    nullptr};
static const wchar_t *tyQmake[] = {nullptr};
static const wchar_t *liQmake[] = {L"yes", L"no", L"true", L"false", nullptr};

/* Windows resource script (.rc) */
static const wchar_t *kwRc[] = {
    L"LANGUAGE", L"VERSION", L"CAPTION", L"FONT", L"STYLE", L"EXSTYLE", L"DIALOG", L"DIALOGEX",
    L"MENU", L"MENUEX", L"POPUP", L"MENUITEM", L"SEPARATOR", L"STRINGTABLE", L"BEGIN", L"END",
    L"ACCELERATORS", L"VIRTKEY", L"SHIFT", L"CONTROL", L"ICON", L"BITMAP", L"CURSOR", L"RCDATA",
    L"VERSIONINFO", L"FILEVERSION", L"PRODUCTVERSION", L"FILEOS", L"FILETYPE", L"FILESUBTYPE",
    L"BLOCK", L"VALUE", L"CHARACTERISTICS", L"LTEXT", L"RTEXT", L"CTEXT", L"EDITTEXT",
    L"PUSHBUTTON", L"DEFPUSHBUTTON", L"CHECKBOX", L"RADIOBUTTON", L"GROUPBOX", L"COMBOBOX",
    L"LISTBOX", L"SCROLLBAR", L"STATIC", L"TEXT",
    nullptr};
static const wchar_t *tyRc[] = {nullptr};
static const wchar_t *liRc[] = {nullptr};

/* Makefile */
static const wchar_t *kwMake[] = {
    L"ifdef", L"ifndef", L"ifeq", L"ifneq", L"else", L"endif", L"define", L"endef",
    L"include", L"override", L"export", L"unexport", L"undefine", L"private", L"vpath",
    L"foreach", L"call", L"eval", L"value", L"origin", L"flavor", L"wildcard", L"notdir",
    L"dir", L"subst", L"patsubst", L"strip", L"findstring", L"filter", L"filter-out",
    L"sort", L"word", L"wordlist", L"words", L"firstword", L"lastword", L"addprefix",
    L"addsuffix", L"join", L"basename", L"realpath", L"abspath", L"error", L"warning",
    L"info", L"shell", L".PHONY", L".DEFAULT", L".SUFFIXES", L".SILENT",
    L".EXPORT_ALL_VARIABLES", L".NOTPARALLEL", L".SECONDARY", L".PRECIOUS",
    nullptr};
static const wchar_t *tyMake[] = {nullptr};
static const wchar_t *liMake[] = {nullptr};

/* Dockerfile */
static const wchar_t *kwDocker[] = {
    L"FROM", L"RUN", L"CMD", L"LABEL", L"MAINTAINER", L"EXPOSE", L"ENV", L"ADD", L"COPY",
    L"ENTRYPOINT", L"VOLUME", L"USER", L"WORKDIR", L"ARG", L"ONBUILD", L"STOPSIGNAL",
    L"HEALTHCHECK", L"SHELL", L"AS", L"base", L"builder", L"platform", L"network", L"host",
    nullptr};
static const wchar_t *tyDocker[] = {nullptr};
static const wchar_t *liDocker[] = {nullptr};

/* Protocol Buffers */
static const wchar_t *kwProto[] = {
    L"syntax", L"package", L"import", L"option", L"message", L"enum", L"service", L"rpc",
    L"returns", L"stream", L"repeated", L"optional", L"required", L"reserved", L"extensions",
    L"oneof", L"map", L"public", L"weak", L"deprecated", L"edition", L"group", L"extend",
    L"to", L"max", L"default", L"json_name", L"proto3", L"proto2", L"packed", L"ctype",
    L"jstype", L"lazy", L"unverified_lazy", L"weak", nullptr};
static const wchar_t *tyProto[] = {
    L"int32", L"int64", L"uint32", L"uint64", L"sint32", L"sint64", L"fixed32", L"fixed64",
    L"sfixed32", L"sfixed64", L"float", L"double", L"bool", L"string", L"bytes",
    L"Timestamp", L"Duration", L"Any", L"Struct", L"Value", L"Empty", L"FieldMask",
    nullptr};
static const wchar_t *liProto[] = {nullptr};

/* ------------------------------------------------------------------ */
/*  Language table                                                     */
/* ------------------------------------------------------------------ */

static const LanguageDef g_languages[] = {
    {L"text", L"Plain Text", kwJson, tyJson, liJson, nullptr, nullptr, nullptr, true, true, false, false, false},
    {L"cpp", L"C/C++", kwCpp, tyCpp, liCpp, L"//", L"/*", L"*/", true, false, false, false, true},
    {L"c", L"C", kwC, tyC, liC, L"//", L"/*", L"*/", true, false, false, false, true},
    {L"cs", L"C#", kwCs, tyCs, liCs, L"//", L"/*", L"*/", true, false, false, false, true},
    {L"java", L"Java", kwJava, tyJava, liJava, L"//", L"/*", L"*/", true, false, false, false, false},
    {L"js", L"JavaScript", kwJs, tyJs, liJs, L"//", L"/*", L"*/", true, false, true, false, false},
    {L"ts", L"TypeScript", kwTs, tyTs, liTs, L"//", L"/*", L"*/", true, false, true, false, false},
    {L"py", L"Python", kwPy, tyPy, liPy, L"#", nullptr, nullptr, true, true, false, true, false},
    {L"html", L"HTML", kwHtml, tyHtml, liHtml, nullptr, L"<!--", L"-->", true, true, false, false, false},
    {L"css", L"CSS", kwCss, tyCss, liCss, nullptr, L"/*", L"*/", true, true, false, false, false},
    {L"php", L"PHP", kwPhp, tyPhp, liPhp, L"//", L"/*", L"*/", true, true, false, false, true},
    {L"go", L"Go", kwGo, tyGo, liGo, L"//", L"/*", L"*/", true, false, true, false, false},
    {L"rust", L"Rust", kwRust, tyRust, liRust, L"//", L"/*", L"*/", true, true, false, true, false},
    {L"lua", L"Lua", kwLua, tyLua, liLua, L"--", L"--[[", L"]]", true, true, false, false, false},
    {L"sql", L"SQL", kwSql, tySql, liSql, L"--", L"/*", L"*/", true, true, false, false, false},
    {L"sh", L"Shell", kwSh, tySh, liSh, L"#", nullptr, nullptr, true, true, true, false, false},
    {L"bat", L"Batch", kwBat, tyBat, liBat, L"::", nullptr, nullptr, true, false, false, false, false},
    {L"json", L"JSON", kwJson, tyJson, liJson, nullptr, nullptr, nullptr, true, false, false, false, false},
    {L"xml", L"XML", kwXml, tyXml, liXml, nullptr, L"<!--", L"-->", true, true, false, false, false},
    {L"md", L"Markdown", kwMd, tyMd, liMd, L"#", nullptr, nullptr, false, false, true, false, false},
    {L"yaml", L"YAML", kwYaml, tyYaml, liYaml, L"#", nullptr, nullptr, true, true, false, false, false},
    {L"ini", L"INI", kwIni, tyIni, liIni, L";", nullptr, nullptr, true, true, false, false, false},
    {L"toml", L"TOML", kwToml, tyToml, liToml, L"#", nullptr, nullptr, true, true, false, false, false},
    {L"ruby", L"Ruby", kwRuby, tyRuby, liRuby, L"#", nullptr, nullptr, true, true, true, false, false},
    {L"swift", L"Swift", kwSwift, tySwift, liSwift, L"//", L"/*", L"*/", true, true, false, false, false},
    {L"kotlin", L"Kotlin", kwKotlin, tyKotlin, liKotlin, L"//", L"/*", L"*/", true, true, false, false, false},
    {L"dart", L"Dart", kwDart, tyDart, liDart, L"//", L"/*", L"*/", true, true, false, false, false},
    {L"ps", L"PowerShell", kwPs, tyPs, liPs, L"#", nullptr, nullptr, true, true, false, false, false},
    {L"perl", L"Perl", kwPerl, tyPerl, liPerl, L"#", nullptr, nullptr, true, true, true, false, false},
    {L"r", L"R", kwR, tyR, liR, L"#", nullptr, nullptr, true, true, false, false, false},
    {L"haskell", L"Haskell", kwHs, tyHs, liHs, L"--", L"{-", L"-}", true, false, false, false, false},
    {L"scala", L"Scala", kwScala, tyScala, liScala, L"//", L"/*", L"*/", true, true, false, false, false},
    {L"groovy", L"Groovy", kwGroovy, tyGroovy, liGroovy, L"//", L"/*", L"*/", true, true, true, false, false},
    {L"objc", L"Objective-C", kwObjC, tyObjC, liObjC, L"//", L"/*", L"*/", true, true, false, false, true},
    {L"vb", L"VB.NET", kwVb, tyVb, liVb, L"'", nullptr, nullptr, true, true, false, false, false},
    {L"cmake", L"CMake", kwCmake, tyCmake, liCmake, L"#", nullptr, nullptr, true, true, false, false, false},
    {L"qmake", L"qmake", kwQmake, tyQmake, liQmake, L"#", nullptr, nullptr, true, true, false, false, false},
    {L"rc", L"Resource", kwRc, tyRc, liRc, L"//", L"/*", L"*/", true, true, false, false, false},
    {L"make", L"Makefile", kwMake, tyMake, liMake, L"#", nullptr, nullptr, true, true, false, false, false},
    {L"docker", L"Dockerfile", kwDocker, tyDocker, liDocker, L"#", nullptr, nullptr, true, true, false, false, false},
    {L"proto", L"Protobuf", kwProto, tyProto, liProto, L"//", L"/*", L"*/", true, true, false, false, false},
};

static const size_t kLanguageCount = sizeof(g_languages) / sizeof(g_languages[0]);

int LanguageCount() { return static_cast<int>(kLanguageCount); }
const LanguageDef *LanguageByIndex(int i)
{
    if (i < 0 || i >= static_cast<int>(kLanguageCount))
        return &g_languages[0];
    return &g_languages[i];
}
const LanguageDef *LanguageById(const wchar_t *id)
{
    if (!id)
        return &g_languages[0];
    for (size_t i = 0; i < kLanguageCount; ++i)
        if (wcscmp(g_languages[i].id, id) == 0)
            return &g_languages[i];
    return &g_languages[0];
}

const LanguageDef *LanguageFromPath(const std::wstring &path)
{
    // Extract the leaf file name (for no-extension files like Makefile).
    size_t slash = path.find_last_of(L"\\/");
    std::wstring leaf = (slash == std::wstring::npos) ? path : path.substr(slash + 1);
    if (leaf == L"CMakeLists.txt" || leaf == L"CMakeLists")
        return &g_languages[35];
    if (leaf == L"Makefile" || leaf == L"makefile" || leaf == L"GNUmakefile")
        return &g_languages[38];
    if (leaf == L"Dockerfile" || leaf == L"Containerfile")
        return &g_languages[39];

    size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos || dot + 1 >= path.size())
        return &g_languages[0];
    std::wstring ext = path.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), towlower);

    if (ext == L"c" || ext == L"h")
        return &g_languages[2];
    if (ext == L"cc" || ext == L"cpp" || ext == L"cxx" || ext == L"hpp" || ext == L"hxx" ||
        ext == L"hh" || ext == L"ipp" || ext == L"inc" || ext == L"cu" || ext == L"cuh")
        return &g_languages[1];
    if (ext == L"cs")
        return &g_languages[3];
    if (ext == L"java")
        return &g_languages[4];
    if (ext == L"js" || ext == L"mjs" || ext == L"cjs" || ext == L"jsx")
        return &g_languages[5];
    if (ext == L"ts" || ext == L"tsx")
        return &g_languages[6];
    if (ext == L"py" || ext == L"pyw")
        return &g_languages[7];
    if (ext == L"html" || ext == L"htm" || ext == L"xhtml" || ext == L"vue")
        return &g_languages[8];
    if (ext == L"css" || ext == L"scss" || ext == L"sass" || ext == L"less")
        return &g_languages[9];
    if (ext == L"php" || ext == L"phtml")
        return &g_languages[10];
    if (ext == L"go")
        return &g_languages[11];
    if (ext == L"rs")
        return &g_languages[12];
    if (ext == L"lua")
        return &g_languages[13];
    if (ext == L"sql")
        return &g_languages[14];
    if (ext == L"sh" || ext == L"bash" || ext == L"zsh")
        return &g_languages[15];
    if (ext == L"bat" || ext == L"cmd")
        return &g_languages[16];
    if (ext == L"json")
        return &g_languages[17];
    if (ext == L"xml" || ext == L"xaml" || ext == L"svg" || ext == L"csproj" ||
        ext == L"vcxproj" || ext == L"slnx" || ext == L"manifest" || ext == L"resx" ||
        ext == L"xsd" || ext == L"xsl")
        return &g_languages[18];
    if (ext == L"md" || ext == L"markdown")
        return &g_languages[19];
    if (ext == L"yml" || ext == L"yaml")
        return &g_languages[20];
    if (ext == L"ini" || ext == L"conf" || ext == L"cfg" || ext == L"properties" ||
        ext == L"env" || ext == L"gitconfig")
        return &g_languages[21];
    if (ext == L"toml")
        return &g_languages[22];
    if (ext == L"rb" || ext == L"erb" || ext == L"rake")
        return &g_languages[23];
    if (ext == L"swift")
        return &g_languages[24];
    if (ext == L"kt" || ext == L"kts" || ext == L"gradle")
        return &g_languages[25];
    if (ext == L"dart")
        return &g_languages[26];
    if (ext == L"ps1" || ext == L"psm1" || ext == L"psd1")
        return &g_languages[27];
    if (ext == L"pl" || ext == L"pm")
        return &g_languages[28];
    if (ext == L"r")
        return &g_languages[29];
    if (ext == L"hs" || ext == L"lhs")
        return &g_languages[30];
    if (ext == L"scala" || ext == L"sc")
        return &g_languages[31];
    if (ext == L"groovy" || ext == L"gvy")
        return &g_languages[32];
    if (ext == L"m" || ext == L"mm")
        return &g_languages[33];
    if (ext == L"vb" || ext == L"vbs")
        return &g_languages[34];
    if (ext == L"cmake")
        return &g_languages[35];
    if (ext == L"pro" || ext == L"pri" || ext == L"qrc" || ext == L"qss")
        return &g_languages[36];
    if (ext == L"rc")
        return &g_languages[37];
    if (ext == L"mk" || ext == L"mak")
        return &g_languages[38];
    if (ext == L"dockerfile" || ext == L"containerfile")
        return &g_languages[39];
    if (ext == L"proto")
        return &g_languages[40];
    return &g_languages[0];
}

static bool IsCjkPunct(wchar_t c); // forward decl (defined below)

bool IsIdentifierStart(wchar_t c) { return c == L'_' || iswalpha(c) != 0 || (c > 0x7F && !IsCjkPunct(c)); }
bool IsIdentifierChar(wchar_t c) { return c == L'_' || iswalnum(c) != 0 || (c > 0x7F && !IsCjkPunct(c)); }

bool IsCjkPunct(wchar_t c)
{
    return (c >= 0x3000 && c <= 0x303F) ||   // CJK punctuation
           (c >= 0xFF01 && c <= 0xFF0F) ||   // fullwidth !  " # $ % & ' ( ) * + , - . /
           (c >= 0xFF1A && c <= 0xFF20) ||   // fullwidth : ; < = > ? @
           (c >= 0xFF3B && c <= 0xFF40) ||   // fullwidth [ \ ] ^ _ `
           (c >= 0xFF5B && c <= 0xFF65) ||   // fullwidth { | } ~ ・
           (c >= 0xFE30 && c <= 0xFE4F) ||   // CJK compatibility forms
           (c >= 0x2018 && c <= 0x201F) ||   // quotes
           (c >= 0x3008 && c <= 0x301F);     // CJK brackets
}

/* ------------------------------------------------------------------ */
/*  Tokenizer                                                          */
/* ------------------------------------------------------------------ */

static bool StartsWith(const std::wstring &s, size_t at, const wchar_t *p)
{
    if (!p)
        return false;
    for (size_t k = 0; p[k]; ++k)
        if (at + k >= s.size() || s[at + k] != p[k])
            return false;
    return true;
}

static bool ListContains(const wchar_t *const *list, const std::wstring &word)
{
    if (!list)
        return false;
    for (size_t i = 0; list[i]; ++i)
        if (word == list[i])
            return true;
    return false;
}

void TokenizeLine(const LanguageDef *lang, const std::wstring &line,
                  unsigned char stateIn, unsigned char &stateOut,
                  std::vector<LineToken> &tokens)
{
    tokens.clear();
    stateOut = stateIn;
    if (!lang)
        return;
    const size_t n = line.size();
    size_t i = 0;

    // Multi-line block states inherited from the previous line.
    if (stateIn == LBS_BlockComment && lang->blockEnd)
    {
        size_t e = line.find(lang->blockEnd, i);
        if (e == std::wstring::npos)
        {
            tokens.push_back({TokType::Comment, 0, n});
            stateOut = LBS_BlockComment;
            return;
        }
        tokens.push_back({TokType::Comment, 0, e + wcslen(lang->blockEnd)});
        i = e + wcslen(lang->blockEnd);
        stateOut = LBS_None;
    }
    else if (stateIn == LBS_TripleDouble)
    {
        size_t e = line.find(L"\"\"\"", i);
        if (e == std::wstring::npos)
        {
            tokens.push_back({TokType::String, 0, n});
            stateOut = LBS_TripleDouble;
            return;
        }
        tokens.push_back({TokType::String, 0, e + 3});
        i = e + 3;
        stateOut = LBS_None;
    }
    else if (stateIn == LBS_TripleSingle)
    {
        size_t e = line.find(L"'''", i);
        if (e == std::wstring::npos)
        {
            tokens.push_back({TokType::String, 0, n});
            stateOut = LBS_TripleSingle;
            return;
        }
        tokens.push_back({TokType::String, 0, e + 3});
        i = e + 3;
        stateOut = LBS_None;
    }

    // Preprocessor line (#include / #define / #pragma ...).
    if (lang->preprocessor && stateOut == LBS_None && i == 0)
    {
        size_t fs = 0;
        while (fs < n && (line[fs] == L' ' || line[fs] == L'\t'))
            ++fs;
        if (fs < n && line[fs] == L'#')
        {
            tokens.push_back({TokType::Preprocessor, fs, n - fs});
            return;
        }
    }

    while (i < n)
    {
        wchar_t c = line[i];

        // Line comment runs to end of line.
        if (lang->lineComment && StartsWith(line, i, lang->lineComment))
        {
            tokens.push_back({TokType::Comment, i, n - i});
            return;
        }

        // Block comment.
        if (lang->blockStart && StartsWith(line, i, lang->blockStart))
        {
            size_t e = line.find(lang->blockEnd, i + wcslen(lang->blockStart));
            if (e == std::wstring::npos)
            {
                tokens.push_back({TokType::Comment, i, n - i});
                stateOut = LBS_BlockComment;
                return;
            }
            tokens.push_back({TokType::Comment, i, e + wcslen(lang->blockEnd) - i});
            i = e + wcslen(lang->blockEnd);
            continue;
        }

        // Triple-quoted string (Python, Rust).
        if (lang->tripleString && i + 3 <= n)
        {
            if (StartsWith(line, i, L"\"\"\""))
            {
                size_t e = line.find(L"\"\"\"", i + 3);
                if (e == std::wstring::npos)
                {
                    tokens.push_back({TokType::String, i, n - i});
                    stateOut = LBS_TripleDouble;
                    return;
                }
                tokens.push_back({TokType::String, i, e + 3 - i});
                i = e + 3;
                continue;
            }
            if (StartsWith(line, i, L"'''"))
            {
                size_t e = line.find(L"'''", i + 3);
                if (e == std::wstring::npos)
                {
                    tokens.push_back({TokType::String, i, n - i});
                    stateOut = LBS_TripleSingle;
                    return;
                }
                tokens.push_back({TokType::String, i, e + 3 - i});
                i = e + 3;
                continue;
            }
        }

        // Double-quoted string.
        if (c == L'"' && lang->stringDouble)
        {
            size_t k = i + 1;
            while (k < n && line[k] != L'"')
            {
                if (line[k] == L'\\' && k + 1 < n)
                    ++k;
                ++k;
            }
            size_t end = (k < n) ? k + 1 : n;
            tokens.push_back({TokType::String, i, end - i});
            i = end;
            continue;
        }

        // Single-quoted string / char literal.
        if (c == L'\'' && (lang->stringSingle || lang->stringDouble))
        {
            size_t k = i + 1;
            while (k < n && line[k] != L'\'')
            {
                if (line[k] == L'\\' && k + 1 < n)
                    ++k;
                ++k;
            }
            size_t end = (k < n) ? k + 1 : n;
            tokens.push_back({TokType::String, i, end - i});
            i = end;
            continue;
        }

        // Backtick string (JS/TS/Go/Shell).
        if (c == L'`' && lang->stringBacktick)
        {
            size_t k = i + 1;
            while (k < n && line[k] != L'`')
            {
                if (line[k] == L'\\' && k + 1 < n)
                    ++k;
                ++k;
            }
            size_t end = (k < n) ? k + 1 : n;
            tokens.push_back({TokType::String, i, end - i});
            i = end;
            continue;
        }

        // Numbers.
        if (iswdigit(c) != 0 || (c == L'.' && i + 1 < n && iswdigit(line[i + 1]) != 0))
        {
            size_t k = i;
            bool isHex = false;
            if (c == L'0' && i + 1 < n && (line[i + 1] == L'x' || line[i + 1] == L'X'))
            {
                isHex = true;
                k = i + 2;
                while (k < n && (iswxdigit(line[k]) != 0 || line[k] == L'_'))
                    ++k;
            }
            else if (c == L'0' && i + 1 < n && (line[i + 1] == L'b' || line[i + 1] == L'B'))
            {
                k = i + 2;
                while (k < n && (line[k] == L'0' || line[k] == L'1' || line[k] == L'_'))
                    ++k;
            }
            else
            {
                while (k < n && iswdigit(line[k]) != 0)
                    ++k;
                if (k < n && line[k] == L'.')
                {
                    ++k;
                    while (k < n && iswdigit(line[k]) != 0)
                        ++k;
                }
                if (k < n && (line[k] == L'e' || line[k] == L'E'))
                {
                    size_t t = k + 1;
                    if (t < n && (line[t] == L'+' || line[t] == L'-'))
                        ++t;
                    if (t < n && iswdigit(line[t]) != 0)
                    {
                        k = t;
                        while (k < n && iswdigit(line[k]) != 0)
                            ++k;
                    }
                }
            }
            while (k < n && (line[k] == L'u' || line[k] == L'U' || line[k] == L'l' ||
                             line[k] == L'L' || line[k] == L'f' || line[k] == L'F'))
                ++k;
            (void)isHex;
            tokens.push_back({TokType::Number, i, k - i});
            i = k;
            continue;
        }

        // Identifiers / keywords / types / literals.
        if (IsIdentifierStart(c))
        {
            size_t k = i;
            while (k < n && IsIdentifierChar(line[k]))
                ++k;
            std::wstring word = line.substr(i, k - i);
            TokType t = TokType::Default;
            if (ListContains(lang->keywords, word))
                t = TokType::Keyword;
            else if (ListContains(lang->types, word))
                t = TokType::Type;
            else if (ListContains(lang->literals, word))
                t = TokType::Literal;
            tokens.push_back({t, i, k - i});
            i = k;
            continue;
        }

        // Single non-identifier character (operator / punctuation / space).
        tokens.push_back({TokType::Default, i, 1});
        ++i;
    }
}
