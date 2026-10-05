//
// The file browser: a folder's contents, read when it is opened, and a name picked or typed at the foot.
//

#include "koralFileBrowser.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <optional>
#include <system_error>

#include "kgui/layout.h"

namespace kgui
{
    namespace fs = std::filesystem;

    namespace {
        std::string lower(std::string text)
        {
            std::ranges::transform(text, text.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return text;
        }

        class FileBrowserWidget final : public kui::StatefulWidget {
        public:
            FileBrowserWidget(FileBrowserOptions options, std::function<void(const fs::path&)> onChosen, std::function<void()> onCancelled)
                : _options(std::move(options)), _onChosen(std::move(onChosen)), _onCancelled(std::move(onCancelled)) {}

            void InitState() override
            {
                std::error_code ec;
                const fs::path start = !_options.start.empty() && fs::is_directory(_options.start, ec) ? _options.start : fs::current_path(ec);
                Open(start);
            }

            void DidUpdateWidget(const kui::StatefulWidget& newer) override
            {
                const auto& other = static_cast<const FileBrowserWidget&>(newer);
                _onChosen = other._onChosen;
                _onCancelled = other._onCancelled;
            }

            kui::Widget Build() override
            {
                kui::TextFieldOptions where;
                where.text = _typedPath;
                where.controlled = true;
                where.onChanged = [this](const std::string& text) { SetState([&] { _typedPath = text; }); };
                where.onSubmitted = [this](const std::string& text) { SetState([&] { Open(text); }); };

                std::vector<kui::Widget> top {
                    kui::Button("Up", [this] { SetState([&] { Open(_folder.parent_path()); }); },
                                kui::ButtonOptions {}.SetStyle(kui::ButtonStyle::eSecondary).SetEnabled(_folder.has_parent_path() && _folder.parent_path() != _folder)),
                    kui::Expanded(kui::TextField(std::move(where))),
                };
                if (_options.createDirectories && _options.directories)
                    top.push_back(kui::Button("New folder", [this] { SetState([&] { _naming = true; _newName.clear(); }); },
                                              kui::ButtonOptions {}.SetStyle(kui::ButtonStyle::eSecondary)));

                std::vector<kui::Widget> rows { kui::Row(std::move(top), kui::FlexOptions {}.SetGap(6.f)) };
                if (_naming) rows.push_back(NewFolder());
                rows.push_back(kui::Expanded(Entries()));
                if (!_problem.empty()) rows.push_back(kui::Text(_problem, kui::TextStyle {}.SetColor({ 1.f, 0.4f, 0.4f, 1.f })));

                kui::TextFieldOptions name;
                name.text = _name;
                name.controlled = true;
                name.placeholder = _options.directories ? "folder (empty: this one)" : "file name";
                name.onChanged = [this](const std::string& text) { SetState([&] { _name = text; }); };
                name.onSubmitted = [this](const std::string&) { Confirm(); };
                rows.push_back(kui::Row({
                    kui::Expanded(kui::TextField(std::move(name))),
                    kui::Button("Cancel", [this] { if (_onCancelled) _onCancelled(); }, kui::ButtonOptions {}.SetStyle(kui::ButtonStyle::eSecondary)),
                    kui::Button(_options.confirm, [this] { Confirm(); }),
                }, kui::FlexOptions {}.SetGap(6.f)));

                return kui::SizedBox(_options.width, _options.height, kui::Padding(kui::EdgeInsets::All(10.f),
                    kui::Column(std::move(rows), kui::FlexOptions {}.SetGap(8.f).SetCrossAxisAlignment(kui::CrossAxisAlignment::eStretch))));
            }

        private:
            struct Entry {
                std::string name;
                bool folder = false;
            };

            /** Reads @p folder: folders first, then files, each by name; what the options leave out left out. */
            void Open(const fs::path& folder)
            {
                std::error_code ec;
                fs::path target = fs::weakly_canonical(folder, ec);
                if (ec || !fs::is_directory(target, ec)) {
                    _problem = "'" + folder.string() + "' is not a folder";
                    _typedPath = _folder.string();
                    return;
                }
                std::vector<Entry> entries;
                for (fs::directory_iterator it(target, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
                    const std::string name = it->path().filename().string();
                    if (!_options.showHidden && name.starts_with('.')) continue;
                    std::error_code typeError;
                    const bool folderEntry = it->is_directory(typeError);
                    if (!folderEntry) {
                        if (_options.directories) continue;
                        if (!_options.extensions.empty()) {
                            const std::string extension = lower(it->path().extension().string());
                            if (std::ranges::none_of(_options.extensions, [&](const std::string& e) { return lower(e) == extension; })) continue;
                        }
                    }
                    entries.push_back({ name, folderEntry });
                }
                std::ranges::sort(entries, [](const Entry& a, const Entry& b) {
                    return a.folder != b.folder ? a.folder : lower(a.name) < lower(b.name);
                });
                _folder = target;
                _typedPath = target.string();
                _entries = std::move(entries);
                _selected.reset();
                _name.clear();
                _problem.clear();
            }

            kui::Widget Entries()
            {
                if (_entries.empty()) return Muted(_options.directories ? "No folders here." : "Nothing here.");
                return kui::ListView(_entries.size(), 30.f, [this](const std::size_t i) {
                    const Entry& entry = _entries[i];
                    return kui::Selectable(entry.folder ? entry.name + "/" : entry.name, _selected == i, [this, i] { Tapped(i); });
                });
            }

            /** A tap picks; a second one on the same entry soon after opens a folder, or picks a file and is done. */
            void Tapped(const std::size_t index)
            {
                const auto now = std::chrono::steady_clock::now();
                const bool twice = _selected == index && now - _tappedAt < std::chrono::milliseconds(400);
                _tappedAt = now;
                const Entry entry = _entries[index];
                if (twice && entry.folder) {
                    SetState([&] { Open(_folder / entry.name); });
                    return;
                }
                SetState([&] {
                    _selected = index;
                    if (!entry.folder || _options.directories) _name = entry.name;
                });
                if (twice) Confirm();
            }

            kui::Widget NewFolder()
            {
                kui::TextFieldOptions name;
                name.text = _newName;
                name.controlled = true;
                name.placeholder = "new folder's name";
                name.focus = 1;
                name.onChanged = [this](const std::string& text) { SetState([&] { _newName = text; }); };
                name.onSubmitted = [this](const std::string&) { Create(); };
                return kui::Row({
                    kui::Expanded(kui::TextField(std::move(name))),
                    kui::Button("Create", [this] { Create(); }),
                    kui::Button("Cancel", [this] { SetState([&] { _naming = false; }); }, kui::ButtonOptions {}.SetStyle(kui::ButtonStyle::eSecondary)),
                }, kui::FlexOptions {}.SetGap(6.f));
            }

            void Create()
            {
                SetState([&] {
                    if (_newName.empty()) return;
                    std::error_code ec;
                    if (!fs::create_directory(_folder / _newName, ec) && ec) {
                        _problem = "could not make '" + _newName + "': " + ec.message();
                        return;
                    }
                    const std::string made = _newName;
                    _naming = false;
                    Open(_folder);
                    _name = made;
                    for (std::size_t i = 0; i < _entries.size(); ++i) if (_entries[i].name == made) _selected = i;
                });
            }

            void Confirm()
            {
                std::error_code ec;
                if (_options.directories) {
                    const fs::path chosen = _name.empty() ? _folder : _folder / _name;
                    if (!fs::is_directory(chosen, ec)) { SetState([&] { _problem = "'" + _name + "' is not a folder here"; }); return; }
                    if (_onChosen) _onChosen(chosen);
                    return;
                }
                if (_name.empty()) { SetState([&] { _problem = "Pick a file, or type its name."; }); return; }
                const fs::path chosen = _folder / _name;
                // A folder typed or picked is gone into, rather than handed back as a file.
                if (fs::is_directory(chosen, ec)) { SetState([&] { Open(chosen); }); return; }
                if (_onChosen) _onChosen(chosen);
            }

            FileBrowserOptions _options;
            std::function<void(const fs::path&)> _onChosen;
            std::function<void()> _onCancelled;
            fs::path _folder;
            std::string _typedPath, _name, _newName, _problem;
            std::vector<Entry> _entries;
            std::optional<std::size_t> _selected;
            std::chrono::steady_clock::time_point _tappedAt {};
            bool _naming = false;
        };
    }

    kui::Widget FileBrowser(FileBrowserOptions options, std::function<void(const fs::path&)> onChosen, std::function<void()> onCancelled)
    {
        return kui::Make<FileBrowserWidget>(std::move(options), std::move(onChosen), std::move(onCancelled));
    }
}
