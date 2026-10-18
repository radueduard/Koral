//
// koral-slangd — runs the slangd beside it, with its empty answers made `null`.
//
// slangd answers a request it has nothing for — a definition at a position with no symbol, a hover
// over whitespace — with `{}` rather than `null`. VS Code's client lets that pass; LSP4IJ (the
// JetBrains IDEs) does not: `textDocument/definition` must be null or a list of locations, and
// `{}` fails the IDE with "Unexpected token BEGIN_OBJECT: expected BEGIN_ARRAY". So this sits
// between the two: the editor's messages go to slangd untouched (it reads this process's stdin),
// and slangd's come back through here, with a response whose result is an empty object turned
// into `"result":null`. No request has an empty object as a meaningful answer.
//
// It also gives the built-in modules a file. slangd names a definition in them — `float4`, `Texture2D`
// — as `slang-synth://core/core.builtin`, a document only it has; VS Code's extension shows it by
// asking `slangd --print-builtin-module core`, which no other editor knows to do. So the first time
// a module is named, it is printed into a cache directory (named by its text's hash), and every
// `slang-synth://<module>/…` in slangd's messages becomes that file — Ctrl+click lands in it. The
// diagnostics slangd would publish for such a file, opened as a module of its own, are dropped.
//
// Arguments are passed through to slangd.
//

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <regex>
#include <string>
#include <variant>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif

#include <rfl/Generic.hpp>
#include <rfl/json.hpp>

namespace fs = std::filesystem;

namespace
{
    fs::path ExecutableDirectory()
    {
#ifdef _WIN32
        wchar_t buf[MAX_PATH];
        const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
        return n ? fs::path(std::wstring(buf, n)).parent_path() : fs::path{};
#elif defined(__APPLE__)
        char buf[4096];
        uint32_t size = sizeof buf;
        return _NSGetExecutablePath(buf, &size) == 0 ? fs::canonical(buf).parent_path() : fs::path{};
#else
        std::error_code ec;
        const auto self = fs::read_symlink("/proc/self/exe", ec);
        return ec ? fs::path{} : self.parent_path();
#endif
    }

    const rfl::Generic* Member(const rfl::Generic& object, const std::string& key)
    {
        const auto* map = std::get_if<rfl::Generic::Object>(&object.variant());
        if (!map) return nullptr;
        for (const auto& [name, value] : *map)
            if (name == key) return &value;
        return nullptr;
    }

    fs::path g_slangd;

    /// Where the built-in modules are printed, per user: under it, one directory per module text.
    fs::path BuiltinCache()
    {
        fs::path base;
#ifdef _WIN32
        if (const char* local = std::getenv("LOCALAPPDATA")) base = fs::path(local) / "Koral" / "cache";
#elif defined(__APPLE__)
        if (const char* home = std::getenv("HOME")) base = fs::path(home) / "Library" / "Caches" / "Koral";
#else
        if (const char* xdg = std::getenv("XDG_CACHE_HOME"); xdg && *xdg) base = fs::path(xdg) / "koral";
        else if (const char* home = std::getenv("HOME")) base = fs::path(home) / ".cache" / "koral";
#endif
        if (base.empty()) base = fs::temp_directory_path() / "koral";
        return base / "slang-builtin";
    }

    /// FNV-1a: names a module's text, so a different Slang (a new SDK) never shows an old one's.
    std::string Hash(const std::string& text)
    {
        std::uint64_t h = 1469598103934665603ull;
        for (const unsigned char c : text) h = (h ^ c) * 1099511628211ull;
        char buf[17];
        std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(h));
        return buf;
    }

    std::string FileUri(const fs::path& path)
    {
        std::string out = "file://";
#ifdef _WIN32
        out += '/';
#endif
        for (const unsigned char c : path.generic_string()) {
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

    /// The file a built-in module was printed to, printing it the first time: empty when slangd
    /// has no such module.
    std::string BuiltinModuleUri(const std::string& module)
    {
        static std::map<std::string, std::string> printed;
        if (const auto it = printed.find(module); it != printed.end()) return it->second;

        // Printed once per session (it takes a tenth of a second): the module lives in the Slang
        // library, not in slangd, so nothing about slangd's own file says which one it is.
        const auto command = "\"" + g_slangd.string() + "\" --print-builtin-module " + module;
#ifdef _WIN32
        std::FILE* out = _wpopen((L"\"" + std::wstring(command.begin(), command.end()) + L"\"").c_str(), L"rb");
#else
        std::FILE* out = popen(command.c_str(), "r");
#endif
        std::string text;
        if (out) {
            char buf[65536];
            for (std::size_t n; (n = std::fread(buf, 1, sizeof buf, out)) > 0;) text.append(buf, n);
#ifdef _WIN32
            _pclose(out);
#else
            pclose(out);
#endif
        }
        if (text.size() <= 1) return printed[module] = {}; // no such module

        const auto dir = BuiltinCache() / Hash(text);
        const auto file = dir / (module + ".slang");
        std::error_code ec;
        if (!fs::is_regular_file(file, ec)) {
            fs::create_directories(dir, ec);
            const auto partial = dir / (module + ".slang.part");
            std::ofstream(partial, std::ios::binary).write(text.data(), static_cast<std::streamsize>(text.size()));
            fs::rename(partial, file, ec); // whole or not at all: another instance may be reading it
            // A copy of what slangd has compiled in: editing it would change nothing.
            fs::permissions(file, fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write,
                            fs::perm_options::remove, ec);
        }
        return printed[module] = FileUri(file);
    }

    /// Every `slang-synth://<module>/…` in a message, made the module's file.
    std::string ResolveBuiltins(std::string body)
    {
        static const std::regex synth(R"(slang-synth://([A-Za-z0-9_\-]+)/[^"\s)\]]*)");
        std::string out;
        auto from = body.cbegin();
        for (std::sregex_iterator it(body.begin(), body.end(), synth), end; it != end; ++it) {
            const auto uri = BuiltinModuleUri((*it)[1].str());
            out.append(from, body.cbegin() + it->position());
            out += uri.empty() ? it->str() : uri;
            from = body.cbegin() + it->position() + it->length();
        }
        out.append(from, body.cend());
        return out;
    }

    /// `\/` made `/`: the same JSON, but slangd escapes every slash (`slang-synth:\/\/core\/…`,
    /// `textDocument\/publishDiagnostics`), which nothing below would otherwise match. A slash after
    /// an escaped backslash (`\\/`) is not an escape and is left alone.
    std::string UnescapeSlashes(const std::string& body)
    {
        std::string out;
        out.reserve(body.size());
        for (std::size_t i = 0; i < body.size(); ++i) {
            if (body[i] == '\\' && i + 1 < body.size()) {
                if (body[i + 1] == '/') { out += '/'; ++i; continue; }
                out += body[i];
                out += body[++i]; // any other escape, kept whole: its second character is not a new escape
                continue;
            }
            out += body[i];
        }
        return out;
    }

    /// The message with an empty-object result made null; the message itself otherwise.
    std::string Normalise(std::string body)
    {
        if (body.find("\\/") != std::string::npos) body = UnescapeSlashes(body);
        if (body.find("slang-synth://") != std::string::npos) body = ResolveBuiltins(std::move(body));

        // slangd's own view of a built-in module opened as a file: errors about code it compiled in,
        // which it publishes under the module's file or under a bare `file://` that names nothing.
        if (body.find("textDocument/publishDiagnostics") != std::string::npos) {
            static const std::string prefix = FileUri(BuiltinCache());
            const auto parsed = rfl::json::read<rfl::Generic>(body);
            const auto* params = parsed ? Member(*parsed, "params") : nullptr;
            const auto* uri = params ? Member(*params, "uri") : nullptr;
            if (const auto* text = uri ? std::get_if<std::string>(&uri->variant()) : nullptr; text && (text->starts_with(prefix) || *text == "file://"))
                return R"({"jsonrpc":"2.0","method":"textDocument/publishDiagnostics","params":{"uri":)" +
                       rfl::json::write(*text) + R"(,"diagnostics":[]}})";
        }

        // Cheap test first: most messages are not empty responses.
        if (body.find("\"result\"") == std::string::npos) return body;
        const auto parsed = rfl::json::read<rfl::Generic>(body);
        if (!parsed || Member(*parsed, "method")) return body;
        const auto* result = Member(*parsed, "result");
        const auto* id = Member(*parsed, "id");
        const auto* object = result ? std::get_if<rfl::Generic::Object>(&result->variant()) : nullptr;
        if (!object || !object->empty() || !id) return body;

        std::string idText;
        if (const auto* n = std::get_if<int64_t>(&id->variant())) idText = std::to_string(*n);
        else if (const auto* s = std::get_if<std::string>(&id->variant())) idText = rfl::json::write(*s);
        else return body;
        return R"({"jsonrpc":"2.0","id":)" + idText + R"(,"result":null})";
    }

    /// Reads slangd's output: the LSP framing, one message at a time.
    class Reader
    {
    public:
        explicit Reader(std::FILE* from) : _from(from) {}

        std::optional<std::string> Next()
        {
            std::size_t length = 0;
            std::string line;
            for (;;) {
                const int c = std::fgetc(_from);
                if (c == EOF) return std::nullopt;
                if (c != '\n') { line += static_cast<char>(c); continue; }
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.empty()) break;
                constexpr std::string_view header = "content-length:";
                std::string lower = line;
                for (auto& ch : lower) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                if (lower.starts_with(header)) length = std::strtoull(line.c_str() + header.size(), nullptr, 10);
                line.clear();
            }
            std::string body(length, '\0');
            if (length && std::fread(body.data(), 1, length, _from) != length) return std::nullopt;
            return body;
        }

    private:
        std::FILE* _from;
    };

    void Write(const std::string& body)
    {
        const auto header = "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n";
        std::fwrite(header.data(), 1, header.size(), stdout);
        std::fwrite(body.data(), 1, body.size(), stdout);
        std::fflush(stdout);
    }
}

int main(int argc, char** argv)
{
    const auto slangd = ExecutableDirectory() / (std::string("slangd") +
#ifdef _WIN32
        ".exe"
#else
        ""
#endif
    );
    g_slangd = slangd;
    if (!fs::is_regular_file(slangd)) {
        std::fprintf(stderr, "koral-slangd: no slangd beside it (%s)\n", slangd.string().c_str());
        return 1;
    }

#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);

    SECURITY_ATTRIBUTES inherit{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE readEnd = nullptr, writeEnd = nullptr;
    if (!CreatePipe(&readEnd, &writeEnd, &inherit, 0)) return 1;
    SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);

    std::wstring command = L"\"" + slangd.wstring() + L"\"";
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        command += L" \"" + std::wstring(arg.begin(), arg.end()) + L"\"";
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);   // the editor speaks to slangd directly
    startup.hStdOutput = writeEnd;
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup, &process)) {
        std::fprintf(stderr, "koral-slangd: cannot start %s\n", slangd.string().c_str());
        return 1;
    }
    CloseHandle(writeEnd);
    std::FILE* from = _fdopen(_open_osfhandle(reinterpret_cast<intptr_t>(readEnd), _O_RDONLY | _O_BINARY), "rb");
#else
    int pipeFds[2];
    if (pipe(pipeFds) != 0) return 1;
    const pid_t child = fork();
    if (child < 0) return 1;
    if (child == 0) {
        // slangd reads the editor (this process's stdin) and writes into the pipe.
        dup2(pipeFds[1], STDOUT_FILENO);
        close(pipeFds[0]);
        close(pipeFds[1]);
        std::vector<char*> args;
        const auto path = slangd.string();
        args.push_back(const_cast<char*>(path.c_str()));
        for (int i = 1; i < argc; ++i) args.push_back(argv[i]);
        args.push_back(nullptr);
        execv(path.c_str(), args.data());
        _exit(127);
    }
    close(pipeFds[1]);
    // Nothing of the editor's is read here: leave stdin to slangd alone.
    close(STDIN_FILENO);
    std::FILE* from = fdopen(pipeFds[0], "rb");
#endif

    Reader reader(from);
    while (const auto body = reader.Next()) Write(Normalise(*body));

#ifdef _WIN32
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(process.hProcess, &code);
    return static_cast<int>(code);
#else
    int status = 0;
    waitpid(child, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
#endif
}
