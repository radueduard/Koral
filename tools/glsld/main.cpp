//
// koral-glsld — a GLSL language server that sees a shader the way the runtime does.
//
// An editor that checks GLSL on its own does not know where Koral looks for an `#include`: the
// project's shader directories (koral.json), then the SDK's shaders, then a module project's. So
// `#include <koralUI.glsl>` is an error in the editor and fine at run time. This server checks a
// shader with the runtime's own settings — glslang, Vulkan 1.3 / SPIR-V 1.6, the include
// extension enabled, the stage from the file name ("x.frag.glsl") — and its search roots, which it
// finds itself, per file, so one server serves every project (IDEs register a language server
// once, not per project):
//
//   - the nearest koral.json above the file: its `paths.shaderDirectories` (default "shaders");
//   - that project's `.koral/shaders/*`, which the Hub links to the SDK's and the module projects'
//     shader directories;
//   - the shaders of the SDK this binary ships in (`<bin>/../share/Koral/shaders`);
//   - `KORAL_SHADERS_DIR`, as the runtime honours it, and any `-I <dir>` given on the command line.
//
// A file with no stage in its name is a header: it is checked through a shader that includes it,
// so what it is told about is what that shader would be told. A header nothing includes is only
// preprocessed (its includes and macros), since it is not a shader on its own.
//
// Diagnostics, and a link on each `#include` to the file it resolves to. Language Server Protocol
// over stdio, full-document sync.
//

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <rfl/Generic.hpp>
#include <rfl/json.hpp>

namespace fs = std::filesystem;

namespace
{
    // ---- JSON ---------------------------------------------------------------------------------

    std::string Quote(const std::string_view s)
    {
        std::string out = "\"";
        for (const unsigned char c : s) {
            switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
            }
        }
        return out + "\"";
    }

    const rfl::Generic* Member(const rfl::Generic& object, const std::string& key)
    {
        const auto* map = std::get_if<rfl::Generic::Object>(&object.variant());
        if (!map) return nullptr;
        for (const auto& [name, value] : *map)
            if (name == key) return &value;
        return nullptr;
    }

    const rfl::Generic* Path(const rfl::Generic& object, std::initializer_list<const char*> keys)
    {
        const rfl::Generic* at = &object;
        for (const char* key : keys) {
            at = Member(*at, key);
            if (!at) return nullptr;
        }
        return at;
    }

    std::string String(const rfl::Generic* value)
    {
        if (!value) return {};
        const auto* s = std::get_if<std::string>(&value->variant());
        return s ? *s : std::string{};
    }

    /// An id as it was sent: a number or a string, echoed back verbatim.
    std::string IdText(const rfl::Generic* id)
    {
        if (!id) return "null";
        if (const auto* n = std::get_if<int64_t>(&id->variant())) return std::to_string(*n);
        if (const auto* s = std::get_if<std::string>(&id->variant())) return Quote(*s);
        return "null";
    }

    // ---- Transport ----------------------------------------------------------------------------

    std::optional<std::string> ReadMessage()
    {
        std::size_t length = 0;
        std::string line;
        while (std::getline(std::cin, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) break;
            constexpr std::string_view header = "Content-Length:";
            if (line.size() > header.size() && std::equal(header.begin(), header.end(), line.begin(),
                    [](char a, char b) { return std::tolower(a) == std::tolower(b); }))
                length = std::strtoull(line.c_str() + header.size(), nullptr, 10);
        }
        if (!std::cin || length == 0) return std::nullopt;
        std::string body(length, '\0');
        std::cin.read(body.data(), static_cast<std::streamsize>(length));
        if (!std::cin) return std::nullopt;
        return body;
    }

    void Send(const std::string& json)
    {
        std::cout << "Content-Length: " << json.size() << "\r\n\r\n" << json;
        std::cout.flush();
    }

    void Respond(const rfl::Generic* id, const std::string& result)
    {
        Send(R"({"jsonrpc":"2.0","id":)" + IdText(id) + R"(,"result":)" + result + "}");
    }

    // ---- Paths and URIs -----------------------------------------------------------------------

    std::string UriToPath(const std::string& uri)
    {
        std::string path = uri.starts_with("file://") ? uri.substr(7) : uri;
        std::string out;
        for (std::size_t i = 0; i < path.size(); ++i) {
            if (path[i] == '%' && i + 2 < path.size()) {
                out += static_cast<char>(std::strtol(path.substr(i + 1, 2).c_str(), nullptr, 16));
                i += 2;
            } else {
                out += path[i];
            }
        }
#ifdef _WIN32
        // file:///C:/x → /C:/x → C:/x
        if (out.size() > 2 && out[0] == '/' && out[2] == ':') out.erase(0, 1);
#endif
        return out;
    }

    std::string PathToUri(const fs::path& path)
    {
        std::string generic = path.generic_string();
        std::string out = "file://";
#ifdef _WIN32
        out += '/';
#endif
        for (const unsigned char c : generic) {
            if (std::isalnum(c) || std::strchr("/-._~:", c)) {
                out += static_cast<char>(c);
            } else {
                char buf[4];
                std::snprintf(buf, sizeof buf, "%%%02X", c);
                out += buf;
            }
        }
        return out;
    }

    fs::path Normal(const fs::path& path)
    {
        std::error_code ec;
        auto canonical = fs::weakly_canonical(path, ec);
        return ec ? path.lexically_normal() : canonical;
    }

    fs::path ExecutableDirectory()
    {
#ifdef _WIN32
        wchar_t buf[MAX_PATH];
        const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
        return n ? fs::path(std::wstring(buf, n)).parent_path() : fs::path{};
#elif defined(__APPLE__)
        char buf[4096];
        uint32_t size = sizeof buf;
        return _NSGetExecutablePath(buf, &size) == 0 ? Normal(buf).parent_path() : fs::path{};
#else
        std::error_code ec;
        const auto self = fs::read_symlink("/proc/self/exe", ec);
        return ec ? fs::path{} : self.parent_path();
#endif
    }

    std::optional<std::string> ReadFile(const fs::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file) return std::nullopt;
        return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    }

    // ---- What the runtime does ----------------------------------------------------------------

    /// The stage tag in a file name, as Shader::Builder reads it ("x.vert.glsl" → vertex).
    std::optional<EShLanguage> StageFromFilename(const fs::path& path)
    {
        static const std::unordered_map<std::string, EShLanguage> tags = {
            {"vert", EShLangVertex},  {"tesc", EShLangTessControl}, {"tese", EShLangTessEvaluation},
            {"geom", EShLangGeometry}, {"frag", EShLangFragment},    {"comp", EShLangCompute},
            {"task", EShLangTask},     {"mesh", EShLangMesh},        {"rgen", EShLangRayGen},
            {"rahit", EShLangAnyHit},  {"rchit", EShLangClosestHit}, {"rmiss", EShLangMiss},
            {"rint", EShLangIntersect}, {"rcall", EShLangCallable},
        };
        for (auto stem = path.filename(); stem.has_extension(); stem = stem.stem()) {
            auto ext = stem.extension().string();
            if (!ext.empty() && ext.front() == '.') ext.erase(0, 1);
            if (const auto it = tags.find(ext); it != tags.end()) return it->second;
        }
        return std::nullopt;
    }

    bool IsGlslFile(const fs::path& path)
    {
        static const std::set<std::string> extensions = {
            ".glsl", ".vert", ".frag", ".comp", ".geom", ".tesc", ".tese", ".task", ".mesh",
            ".rgen", ".rahit", ".rchit", ".rmiss", ".rint", ".rcall",
        };
        return extensions.contains(path.extension().string());
    }

    struct Server
    {
        std::vector<fs::path> extraRoots;                 // -I
        std::map<fs::path, std::string> open;             // path → the editor's text
        std::map<fs::path, std::string> uris;             // path → the uri the editor named it by

        /// The text of a file: the editor's, while it is open, so unsaved edits are what is checked.
        std::optional<std::string> Text(const fs::path& path) const
        {
            if (const auto it = open.find(path); it != open.end()) return it->second;
            return ReadFile(path);
        }

        /// The project a file belongs to: the directory of the nearest koral.json above it.
        static std::optional<fs::path> ProjectRoot(const fs::path& file)
        {
            for (auto dir = file.parent_path(); !dir.empty(); dir = dir.parent_path()) {
                std::error_code ec;
                if (fs::is_regular_file(dir / "koral.json", ec)) return dir;
                if (dir == dir.root_path()) break;
            }
            return std::nullopt;
        }

        /// Where the runtime would look for an `#include` made from @p file, in its order.
        std::vector<fs::path> SearchRoots(const fs::path& file) const
        {
            std::vector<fs::path> roots;
            const auto add = [&roots](const fs::path& dir) {
                std::error_code ec;
                if (dir.empty() || !fs::is_directory(dir, ec)) return;
                auto normal = Normal(dir);
                if (std::ranges::find(roots, normal) == roots.end()) roots.push_back(std::move(normal));
            };

            if (const auto root = ProjectRoot(file)) {
                std::vector<std::string> dirs;
                if (const auto text = ReadFile(*root / "koral.json")) {
                    if (const auto doc = rfl::json::read<rfl::Generic>(*text)) {
                        for (const char* key : {"shaderDirectories", "shadersDir"}) {
                            const auto* value = Path(*doc, {"paths", key});
                            if (!value) continue;
                            if (const auto* list = std::get_if<rfl::Generic::Array>(&value->variant())) {
                                for (const auto& item : *list) dirs.push_back(String(&item));
                            } else {
                                dirs.push_back(String(value));
                            }
                            break;
                        }
                    }
                }
                if (dirs.empty()) dirs.emplace_back("shaders");
                for (const auto& dir : dirs) add(fs::path(dir).is_absolute() ? fs::path(dir) : *root / dir);

                // The Hub's links to the SDK's and module projects' shader directories.
                std::error_code ec;
                std::vector<fs::path> links;
                for (const auto& entry : fs::directory_iterator(*root / ".koral" / "shaders", ec)) links.push_back(entry.path());
                std::ranges::sort(links, [](const fs::path& a, const fs::path& b) {
                    // The SDK's first, as the runtime searches it before a module project's.
                    if ((a.filename() == "koral") != (b.filename() == "koral")) return a.filename() == "koral";
                    return a < b;
                });
                for (const auto& link : links) add(link);
            } else {
                // Not in a project — the engine's own tree, say. Its shaders include their
                // neighbours by `<name>`, which the runtime finds because they are installed together.
                add(file.parent_path());
            }

            if (const char* env = std::getenv("KORAL_SHADERS_DIR"); env && *env) add(env);
            if (const auto exe = ExecutableDirectory(); !exe.empty()) {
                add(exe / "shaders");
                add(exe.parent_path() / "share" / "Koral" / "shaders");
            }
#ifdef KORAL_GLSLD_SOURCE_SHADERS
            add(KORAL_GLSLD_SOURCE_SHADERS);
#endif
            for (const auto& dir : extraRoots) add(dir);
            return roots;
        }

        /// Where `#include` @p name made from @p from resolves, as the runtime's includer resolves it:
        /// a quoted name next to the including file first, then the roots; `<name>` only the roots.
        std::optional<fs::path> Resolve(const std::string& name, const bool local, const fs::path& from,
                                        const std::vector<fs::path>& roots) const
        {
            std::vector<fs::path> candidates;
            if (local) candidates.push_back(from.parent_path());
            candidates.insert(candidates.end(), roots.begin(), roots.end());
            for (const auto& root : candidates) {
                auto candidate = Normal(root / name);
                std::error_code ec;
                if (open.contains(candidate) || fs::is_regular_file(candidate, ec)) return candidate;
            }
            return std::nullopt;
        }

        // ---- Checking -------------------------------------------------------------------------

        class Includer final : public glslang::TShader::Includer
        {
        public:
            Includer(const Server& server, fs::path source, std::vector<fs::path> roots)
                : _server(server), _roots(std::move(roots)) { _stack.push_back(std::move(source)); }

            IncludeResult* includeLocal(const char* name, const char*, size_t) override { return Load(name, true); }
            IncludeResult* includeSystem(const char* name, const char*, size_t) override { return Load(name, false); }
            void releaseInclude(IncludeResult* result) override
            {
                if (!result) return;
                _stack.pop_back();
                delete static_cast<std::string*>(result->userData);
                delete result;
            }

        private:
            IncludeResult* Load(const char* name, const bool local)
            {
                const auto path = _server.Resolve(name, local, _stack.back(), _roots);
                if (!path) return nullptr;
                const auto text = _server.Text(*path);
                if (!text) return nullptr;
                auto* data = new std::string(*text);
                _stack.push_back(*path);
                return new IncludeResult(path->string(), data->data(), data->size(), data);
            }

            const Server& _server;
            std::vector<fs::path> _roots;
            std::vector<fs::path> _stack;
        };

        struct Diagnostic
        {
            fs::path file;  ///< Empty when glslang named no file.
            int line = 0;   ///< 1-based; 0 when glslang gave none.
            bool error = true;
            std::string message;
        };

        /// glslang's info log, one diagnostic per "ERROR: file:line: message" line.
        static std::vector<Diagnostic> ParseLog(const std::string& log, const fs::path& main)
        {
            static const std::regex located(R"(^(ERROR|WARNING): (.*):(\d+):(?:\d+:)? (.*)$)");
            static const std::regex unlocated(R"(^(ERROR|WARNING): (.*)$)");
            std::vector<Diagnostic> out;
            std::istringstream lines(log);
            std::string line;
            while (std::getline(lines, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                std::smatch m;
                if (std::regex_match(line, m, located)) {
                    fs::path file = m[2].str();
                    if (file == "0" || file.empty()) file = main;
                    out.push_back({Normal(file), std::stoi(m[3].str()), m[1] == "ERROR", m[4].str()});
                } else if (std::regex_match(line, m, unlocated)) {
                    const auto text = m[2].str();
                    // The tally glslang appends ("1 compilation errors.  No code generated.").
                    if (text.find("compilation errors") != std::string::npos ||
                        text.find("No code generated") != std::string::npos) continue;
                    out.push_back({{}, 0, m[1] == "ERROR", text});
                }
            }
            return out;
        }

        /// Compile @p file as @p stage with the runtime's settings: parse, link and number the bindings.
        std::vector<Diagnostic> Compile(const fs::path& file, const EShLanguage stage, const bool preprocessOnly) const
        {
            const auto text = Text(file);
            if (!text) return {{file, 0, true, "cannot read the file"}};

            glslang::TShader shader(stage);
            const char* strings[] = {text->c_str()};
            const int lengths[] = {static_cast<int>(text->size())};
            const auto name = file.string();
            const char* names[] = {name.c_str()};
            shader.setStringsWithLengthsAndNames(strings, lengths, names, 1);
            shader.setPreamble("#extension GL_GOOGLE_include_directive : require\n");
            shader.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientVulkan, 450);
            shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_3);
            shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_6);
            shader.setAutoMapBindings(true);
            constexpr auto messages = static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules | EShMsgDefault | EShMsgEnhanced);

            Includer includer(*this, file, SearchRoots(file));
            if (preprocessOnly) {
                std::string output;
                if (!shader.preprocess(GetDefaultResources(), 450, ENoProfile, false, false, messages, &output, includer))
                    return ParseLog(shader.getInfoLog(), file);
                return {};
            }
            if (!shader.parse(GetDefaultResources(), 450, false, messages, includer))
                return ParseLog(shader.getInfoLog(), file);

            auto diagnostics = ParseLog(shader.getInfoLog(), file); // warnings
            glslang::TProgram program;
            program.addShader(&shader);
            if (!program.link(messages) || !program.mapIO()) {
                auto more = ParseLog(program.getInfoLog(), file);
                diagnostics.insert(diagnostics.end(), more.begin(), more.end());
            }
            return diagnostics;
        }

        struct Include
        {
            int line = 0;           ///< 0-based.
            int start = 0, end = 0; ///< The name, without its quotes or brackets.
            std::string name;
            bool local = true;
        };

        static std::vector<Include> Includes(const std::string& text)
        {
            static const std::regex directive(R"(^\s*#\s*include\s*(["<])([^">]+)[">])");
            std::vector<Include> out;
            std::istringstream lines(text);
            std::string line;
            for (int n = 0; std::getline(lines, line); ++n) {
                std::smatch m;
                if (!std::regex_search(line, m, directive)) continue;
                const int start = static_cast<int>(m.position(2));
                out.push_back({n, start, start + static_cast<int>(m.length(2)), m[2].str(), m[1] == "\""});
            }
            return out;
        }

        /// A shader that includes @p header, directly or through another header: the one whose
        /// diagnostics say what @p header means. Open shaders first, then the project's roots.
        std::optional<fs::path> ShaderIncluding(const fs::path& header) const
        {
            std::vector<fs::path> candidates;
            for (const auto& [path, _] : open) candidates.push_back(path);
            std::error_code ec;
            std::vector<fs::path> dirs = {header.parent_path()};
            if (const auto root = ProjectRoot(header)) dirs.push_back(*root);
            for (const auto& dir : dirs)
                for (auto it = fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec);
                     it != fs::recursive_directory_iterator(); it.increment(ec)) {
                    if (ec) break;
                    const auto name = it->path().filename().string();
                    if (it->is_directory(ec) && (name.starts_with(".") || name.starts_with("cmake-build") || name == "build")) {
                        it.disable_recursion_pending();
                        continue;
                    }
                    if (it->is_regular_file(ec) && IsGlslFile(it->path())) candidates.push_back(Normal(it->path()));
                }

            // Who includes whom, by resolving every candidate's includes; then a breadth-first walk
            // up from the header to the first file with a stage.
            std::map<fs::path, std::vector<fs::path>> includedBy;
            for (const auto& candidate : candidates) {
                if (includedBy.contains(candidate) && !includedBy[candidate].empty()) continue;
                const auto text = Text(candidate);
                if (!text) continue;
                const auto roots = SearchRoots(candidate);
                for (const auto& include : Includes(*text))
                    if (const auto target = Resolve(include.name, include.local, candidate, roots))
                        includedBy[*target].push_back(candidate);
            }
            std::vector<fs::path> queue = {header};
            std::set<fs::path> seen = {header};
            for (std::size_t i = 0; i < queue.size(); ++i) {
                for (const auto& user : includedBy[queue[i]]) {
                    if (!seen.insert(user).second) continue;
                    if (StageFromFilename(user)) return user;
                    queue.push_back(user);
                }
            }
            return std::nullopt;
        }

        /// What the editor shows for @p file: its own diagnostics, and each one in a file it
        /// includes on the `#include` line that brought it in.
        void Check(const fs::path& file)
        {
            std::vector<Diagnostic> diagnostics;
            std::string context;
            if (const auto stage = StageFromFilename(file)) {
                diagnostics = Compile(file, *stage, false);
            } else if (const auto user = ShaderIncluding(file)) {
                diagnostics = Compile(*user, *StageFromFilename(*user), false);
                context = " (checked as included by " + user->filename().string() + ")";
            } else {
                diagnostics = Compile(file, EShLangFragment, true);
            }

            const auto text = Text(file).value_or("");
            const auto includes = Includes(text);
            const auto roots = SearchRoots(file);

            std::vector<std::string> lineText;
            {
                std::istringstream lines(text);
                for (std::string l; std::getline(lines, l);) lineText.push_back(l);
            }
            const auto range = [&](const int line, int start = -1, int end = -1) {
                const std::string& l = line >= 0 && line < static_cast<int>(lineText.size()) ? lineText[line] : std::string{};
                if (start < 0) {
                    start = static_cast<int>(l.find_first_not_of(" \t"));
                    if (start < 0) start = 0;
                    end = static_cast<int>(l.size());
                    if (end <= start) end = start + 1;
                }
                return R"({"start":{"line":)" + std::to_string(line) + R"(,"character":)" + std::to_string(start) +
                       R"(},"end":{"line":)" + std::to_string(line) + R"(,"character":)" + std::to_string(end) + "}}";
            };

            std::string items;
            const auto emit = [&](const std::string& r, const bool error, const std::string& message) {
                if (!items.empty()) items += ',';
                items += R"({"range":)" + r + R"(,"severity":)" + (error ? "1" : "2") +
                         R"(,"source":"koral-glsld","message":)" + Quote(message) + "}";
            };

            for (const auto& d : diagnostics) {
                if (d.file.empty() || d.line == 0) {
                    emit(range(0), d.error, d.message + context);
                } else if (d.file == file) {
                    emit(range(d.line - 1), d.error, d.message + context);
                } else {
                    // In another file: on the include that leads to it, when this file has one.
                    const Include* via = nullptr;
                    for (const auto& include : includes)
                        if (Resolve(include.name, include.local, file, roots) == d.file) via = &include;
                    if (!via && !includes.empty() && StageFromFilename(file)) via = &includes.front();
                    if (!via) continue; // an error of the including shader's own, not this header's
                    emit(range(via->line, via->start, via->end), d.error,
                         d.file.filename().string() + ":" + std::to_string(d.line) + ": " + d.message);
                }
            }

            const auto uri = uris.contains(file) ? uris.at(file) : PathToUri(file);
            Send(R"({"jsonrpc":"2.0","method":"textDocument/publishDiagnostics","params":{"uri":)" + Quote(uri) +
                 R"(,"diagnostics":[)" + items + "]}}");
        }

        std::string DocumentLinks(const fs::path& file) const
        {
            const auto text = Text(file).value_or("");
            const auto roots = SearchRoots(file);
            std::string out = "[";
            for (const auto& include : Includes(text)) {
                const auto target = Resolve(include.name, include.local, file, roots);
                if (!target) continue;
                if (out.size() > 1) out += ',';
                out += R"({"range":{"start":{"line":)" + std::to_string(include.line) + R"(,"character":)" +
                       std::to_string(include.start) + R"(},"end":{"line":)" + std::to_string(include.line) +
                       R"(,"character":)" + std::to_string(include.end) + R"(}},"target":)" + Quote(PathToUri(*target)) + "}";
            }
            return out + "]";
        }

        void CheckAllOpen()
        {
            std::vector<fs::path> files;
            for (const auto& [path, _] : open) files.push_back(path);
            for (const auto& file : files) Check(file);
        }
    };
}

int main(int argc, char** argv)
{
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    std::ios::sync_with_stdio(false);

    Server server;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-I" && i + 1 < argc) server.extraRoots.emplace_back(argv[++i]);
        else if (arg.starts_with("-I")) server.extraRoots.emplace_back(arg.substr(2));
        else if (arg == "--version") { std::cout << "koral-glsld 1\n"; return 0; }
        else if (arg == "--stdio") {}
        else if (arg == "--help" || arg == "-h") {
            std::cout << "koral-glsld [-I <dir>]... — a GLSL language server (LSP over stdio) that resolves\n"
                         "#include as the Koral runtime does: koral.json's shader directories, the project's\n"
                         ".koral/shaders links, the SDK's shaders, then each -I.\n";
            return 0;
        }
    }

    glslang::InitializeProcess();
    bool shuttingDown = false;
    while (const auto body = ReadMessage()) {
        const auto parsed = rfl::json::read<rfl::Generic>(*body);
        if (!parsed) continue;
        const auto& message = *parsed;
        const auto method = String(Member(message, "method"));
        const auto* id = Member(message, "id");

        const auto documentPath = [&]() {
            const auto uri = String(Path(message, {"params", "textDocument", "uri"}));
            return std::pair{Normal(UriToPath(uri)), uri};
        };

        if (method == "initialize") {
            Respond(id, R"({"capabilities":{"textDocumentSync":{"openClose":true,"change":1,"save":{"includeText":false}},)"
                        R"("documentLinkProvider":{"resolveProvider":false}},"serverInfo":{"name":"koral-glsld","version":"1"}})");
        } else if (method == "shutdown") {
            shuttingDown = true;
            Respond(id, "null");
        } else if (method == "exit") {
            break;
        } else if (method == "textDocument/didOpen") {
            const auto [path, uri] = documentPath();
            server.open[path] = String(Path(message, {"params", "textDocument", "text"}));
            server.uris[path] = uri;
            server.Check(path);
        } else if (method == "textDocument/didChange") {
            const auto [path, uri] = documentPath();
            const auto* changes = Path(message, {"params", "contentChanges"});
            if (const auto* list = changes ? std::get_if<rfl::Generic::Array>(&changes->variant()) : nullptr; list && !list->empty())
                server.open[path] = String(Member(list->back(), "text"));
            server.uris[path] = uri;
            // A header's edit changes what the shaders including it mean.
            server.CheckAllOpen();
        } else if (method == "textDocument/didSave") {
            server.CheckAllOpen();
        } else if (method == "textDocument/didClose") {
            const auto [path, uri] = documentPath();
            server.open.erase(path);
            server.uris.erase(path);
            Send(R"({"jsonrpc":"2.0","method":"textDocument/publishDiagnostics","params":{"uri":)" + Quote(uri) + R"(,"diagnostics":[]}})");
        } else if (method == "textDocument/documentLink") {
            Respond(id, server.DocumentLinks(documentPath().first));
        } else if (id && !method.empty()) {
            // A request this server does not serve.
            Send(R"({"jsonrpc":"2.0","id":)" + IdText(id) + R"(,"error":{"code":-32601,"message":"method not found"}})");
        }
    }
    glslang::FinalizeProcess();
    return shuttingDown ? 0 : 1;
}
