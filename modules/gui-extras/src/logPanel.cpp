//
// The log panel: kor::log's history, copied a little at a time, filtered, and shown a line at a time in a
// list that only builds the lines in view.
//

#include "koralLogPanel.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <format>

#include <log.h>
#include <window.h>

#include "kgui/layout.h"

namespace kgui
{
    namespace {
        constexpr std::array Levels { kor::log::Level::eInfo, kor::log::Level::eWarn, kor::log::Level::eError };

        std::size_t indexOf(const kor::log::Level level) { return static_cast<std::size_t>(level); }

        kui::Color colorOf(const kor::log::Level level)
        {
            switch (level) {
            case kor::log::Level::eWarn:  return { 1.00f, 0.78f, 0.24f, 1.f };
            case kor::log::Level::eError: return { 1.00f, 0.38f, 0.35f, 1.f };
            default:                      return kui::Theme::Current().text;
            }
        }

        const char* nameOf(const kor::log::Level level)
        {
            switch (level) {
            case kor::log::Level::eWarn:  return "warn";
            case kor::log::Level::eError: return "error";
            default:                      return "info";
            }
        }

        /** Case-insensitive substring, which is what a filter box is taken to mean. */
        bool contains(const std::string& text, const std::string& needle)
        {
            if (needle.empty()) return true;
            return !std::ranges::search(text, needle, [](const char a, const char b) {
                return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
            }).empty();
        }

        class LogPanelWidget final : public Live {
        public:
            explicit LogPanelWidget(const LogPanelOptions& options) : _followTail(options.followTail), _showTimes(options.showTimes) {}

            kui::Widget Build() override
            {
                Refilter();
                std::array<std::size_t, Levels.size()> counts {};
                for (const auto& record : _records) ++counts[indexOf(record.level)];

                // The toolbar: clear, copy, follow, times — and the levels, each with how many there are on
                // the toggle itself: "are there errors?" is the question a log panel is opened to answer.
                std::vector<kui::Widget> tools {
                    kui::Button("Clear", [this] { SetState([&] { kor::log::ClearHistory(); _records.clear(); _lastSequence = kor::log::LastSequence(); }); },
                                kui::ButtonOptions {}.SetStyle(kui::ButtonStyle::eSecondary)),
                    kui::Button("Copy", [this] { Copy(); }, kui::ButtonOptions {}.SetStyle(kui::ButtonStyle::eSecondary)),
                    kui::Checkbox(_followTail, [this](const bool on) { SetState([&] { _followTail = on; }); }, "Follow"),
                    kui::Checkbox(_showTimes, [this](const bool on) { SetState([&] { _showTimes = on; }); }, "Times"),
                };
                for (const auto level : Levels)
                    tools.push_back(kui::Checkbox(_show[indexOf(level)], [this, level](const bool on) { SetState([&] { _show[indexOf(level)] = on; }); },
                                                  std::format("{} ({})", nameOf(level), counts[indexOf(level)])));
                // What the filters left, against what there is: without it a filter that matches nothing
                // looks exactly like a log that has nothing in it.
                tools.push_back(Muted(std::format("{} / {}", _visible.size(), _records.size())));

                kui::TextFieldOptions filter;
                filter.text = _filter;
                filter.placeholder = "filter";
                filter.controlled = true;
                filter.onChanged = [this](const std::string& text) { SetState([&] { _filter = text; }); };

                return kui::Padding(kui::EdgeInsets::All(8.f), kui::Column({
                    kui::Row(std::move(tools), kui::FlexOptions {}.SetGap(8.f)),
                    kui::TextField(std::move(filter)),
                    kui::Separator(),
                    kui::Expanded(Lines()),
                }, kui::FlexOptions {}.SetGap(6.f).SetCrossAxisAlignment(kui::CrossAxisAlignment::eStretch)));
            }

        protected:
            bool Poll(float) override { return Sync(); }

        private:
            /**
             * Brings the panel's copy of the log up to date: only what has arrived since the last frame is
             * copied — usually nothing. Whether anything changed.
             */
            bool Sync()
            {
                bool changed = false;
                // The log was cleared (or is shorter than what we hold): start again.
                if (kor::log::LastSequence() < _lastSequence) {
                    _records.clear();
                    _lastSequence = 0;
                    changed = true;
                }
                auto arrived = kor::log::HistorySince(_lastSequence);
                if (!arrived.empty()) {
                    _lastSequence = arrived.back().sequence;
                    _records.insert(_records.end(), std::make_move_iterator(arrived.begin()), std::make_move_iterator(arrived.end()));
                    changed = true;
                    // The newest in view, while the view is at the bottom: scrolled back to read something,
                    // it is not yanked away by the next message.
                    if (_followTail && _atBottom) ++_jump;
                }
                // Bounded by what the log itself keeps, so a panel left open all run does not grow for ever.
                if (const auto limit = kor::log::HistoryLimit(); limit > 0 && _records.size() > limit)
                    _records.erase(_records.begin(), _records.begin() + static_cast<std::ptrdiff_t>(_records.size() - limit));
                return changed;
            }

            void Refilter()
            {
                _visible.clear();
                for (std::size_t i = 0; i < _records.size(); ++i)
                    if (_show[indexOf(_records[i].level)] && contains(_records[i].message, _filter)) _visible.push_back(i);
            }

            /** The text of one entry as the list shows it: the time, if times are on, then the message. */
            [[nodiscard]] std::string Compose(const kor::log::Record& record) const
            {
                return _showTimes ? std::format("[{:8.3f}] {}", record.time, record.message) : record.message;
            }

            /** Copying wants the whole log the filters let through, not the part on screen. */
            void Copy() const
            {
                std::string text;
                for (const auto index : _visible) text += Compose(_records[index]) + "\n";
                kor::Window::SetClipboardText(text);
            }

            kui::Widget Lines()
            {
                if (_visible.empty()) return Muted(_records.empty() ? "Nothing has been logged." : "Nothing matches the filter.");
                kui::LazyListOptions options;
                options.count = _visible.size();
                options.estimatedExtent = 22.f;
                options.gap = 1.f;
                options.onRange = [this](std::size_t, const std::size_t last) { _atBottom = last >= _visible.size(); };
                options.jump = _jump;
                options.jumpIndex = _visible.size() - 1;
                return kui::LazyList(std::move(options), [this](const std::size_t slot) { return Entry(_records[_visible[slot]]); });
            }

            /** One entry: its wrapped text, highlighted while selected, its details on hover and a menu on the right button. */
            kui::Widget Entry(const kor::log::Record& record)
            {
                const auto sequence = record.sequence;
                const bool selected = sequence == _selected;
                const kui::Theme& theme = kui::Theme::Current();
                kui::ContainerOptions box;
                box.padding = kui::EdgeInsets::Symmetric(6.f, 2.f);
                box.decoration.color = selected ? theme.surfacePressed : kui::colors::Transparent;
                box.decoration.radius = kui::Radii(6.f);
                auto row = kui::GestureDetector(kui::GestureOptions {}.OnTap([this, sequence] {
                    // Clicking the selected line lets it go.
                    SetState([&] { _selected = _selected == sequence ? 0 : sequence; });
                }), kui::Container(box, kui::Text(Compose(record), kui::TextStyle {}.SetColor(colorOf(record.level)))));

                // The sequence number is worth showing: it is the one kor::log::HistorySince takes, and it says
                // how many messages were dropped between two lines.
                auto tip = kui::Tooltip(std::format("{}  #{}  at {:.3f} s", nameOf(record.level), sequence, record.time), std::move(row));
                const auto level = record.level;
                const std::string message = record.message, line = Compose(record);
                return kui::ContextMenu({
                    { .label = "Copy message", .onSelected = [message] { kor::Window::SetClipboardText(message); } },
                    { .label = "Copy line", .onSelected = [line] { kor::Window::SetClipboardText(line); } },
                    { .separator = true },
                    { .label = "Show only this level", .onSelected = [this, level] {
                        SetState([&] { for (const auto l : Levels) _show[indexOf(l)] = l == level; }); } },
                    { .label = "Show all levels", .onSelected = [this] { SetState([&] { _show.fill(true); }); } },
                }, std::move(tip)).Key(std::to_string(sequence));
            }

            std::vector<kor::log::Record> _records;
            std::uint64_t _lastSequence = 0;
            std::vector<std::size_t> _visible;      // indices of the records the filters let through
            std::array<bool, Levels.size()> _show { true, true, true };
            std::string _filter;
            bool _followTail, _showTimes;
            bool _atBottom = true;
            std::uint32_t _jump = 1;                // starts at the newest
            std::uint64_t _selected = 0;
        };
    }

    kui::Widget LogPanel(const LogPanelOptions options) { return kui::Make<LogPanelWidget>(options); }
}
