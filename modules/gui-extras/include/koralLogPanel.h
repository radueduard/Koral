//
// Created by radue on 30.07.2026.
//

/**
 * @file koralLogPanel.h
 * @brief A window showing what the engine, the scene and every module have logged.
 *
 * @code
 * // in a scene
 * kgui::LogPanel _log;
 *
 * void MyScene::RenderUI() { _log.Draw(); }
 * @endcode
 *
 * It reads kor::log's history, which every image in the process records into — so a message written
 * by the engine, by a scene and by a module all land here, in order, whichever thread wrote them.
 * Nothing has to be routed to it.
 *
 * Lines wrap rather than run off the right edge, each one is a row that highlights under the pointer
 * and can be selected, hovering it tells you its level, its sequence number and when it was logged,
 * and right-clicking one copies it or narrows the view to its level.
 */

#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <imgui.h>

#include <log.h>

namespace kgui
{
    /// Implementation details of the panel below, out here only so they can be tested on their own.
    namespace log_detail
    {
        /** @brief A half-open range of lines, [first, last). */
        struct LineRange { std::size_t first = 0; std::size_t last = 0; };

        /**
         * @brief Which lines a view covers, given where each one starts.
         * @param offsets Running offsets: offsets[i] is where line i starts, and there is one extra
         *        entry at the end holding the total height. n+1 entries describe n lines.
         * @param from The top of the view, in the same coordinates.
         * @param to Its bottom.
         * @return The lines that intersect [from, to), clamped to the ones that exist.
         *
         * Two binary searches over a sorted array, which is what lets a panel with thousands of lines
         * cost the same as one with ten. Split out from the drawing because the boundaries are where
         * this sort of thing goes wrong — and a wrong answer here is an empty list, not a crash.
         */
        [[nodiscard]] inline LineRange linesIn(const std::span<const float> offsets,
                                               const float from, const float to)
        {
            if (offsets.size() < 2) return { };
            const std::size_t count = offsets.size() - 1;

            // The line containing `from` starts at or before it: one before the first offset past it.
            // begin() means the view starts above the first line, which is the first line.
            const auto after = std::ranges::upper_bound(offsets, from);
            const std::size_t first = after == offsets.begin()
                ? 0u : static_cast<std::size_t>(after - offsets.begin()) - 1u;

            // The first line starting at or after the bottom is the first one not to draw.
            const std::size_t last = static_cast<std::size_t>(
                std::ranges::lower_bound(offsets, to) - offsets.begin());

            return { std::min(first, count), std::min(last, count) };
        }
    }

    /**
     * @brief A log window: levels to filter by, a text filter, and follow-the-tail.
     *
     * Holds no messages of its own — the history belongs to kor::log, and this reads a snapshot of it
     * each time it draws. Cheap because it draws only while it is open, and because the list is
     * clipped to the lines actually on screen.
     */
    class LogPanel
    {
    public:
        /**
         * @brief Draws the window.
         * @param title Its title, which is also its ImGui id.
         * @param open Optional flag the window's close button clears, as ImGui::Begin takes.
         *
         * Call it from Scene::RenderUI, once a frame.
         */
        void Draw(const char* title = "Log", bool* open = nullptr)
        {
            // Sync before the early return: a panel that is closed for a while must not come back
            // having missed everything logged in the meantime.
            sync();

            if (!ImGui::Begin(title, open)) { ImGui::End(); return; }

            // Before the toolbar, so the "shown of total" it prints is this frame's answer and not
            // last frame's. Doing it again inside drawLines is what picks up a toggle made just now,
            // and costs nothing when nothing changed.
            refilter();

            drawToolbar(_records);
            ImGui::Separator();
            drawLines(_records);

            ImGui::End();
        }

        /** @brief Whether messages of @p level are shown. */
        [[nodiscard]] bool shows(const kor::log::Level level) const { return _show[index(level)]; }
        /** @brief Shows or hides messages of @p level. */
        void setShows(const kor::log::Level level, const bool shown) { _show[index(level)] = shown; }

        /** @brief Whether the view follows new messages as they arrive. */
        [[nodiscard]] bool followsTail() const { return _followTail; }
        void setFollowsTail(const bool follow) { _followTail = follow; }

        /** @brief Whether each line is prefixed with the time it was logged. */
        [[nodiscard]] bool showsTimes() const { return _showTimes; }
        void setShowsTimes(const bool show) { _showTimes = show; }

        /**
         * @brief The sequence number of the selected entry, or 0 when none is.
         *
         * A selection is a reading aid — it keeps one entry highlighted while the log scrolls past it —
         * but it is also how a scene can tell which line the user pointed at, since kor::log::Record
         * carries the same number.
         */
        [[nodiscard]] std::uint64_t selected() const { return _selected; }
        /** @brief Selects the entry with this sequence number, or clears the selection with 0. */
        void select(const std::uint64_t sequence) { _selected = sequence; }

        /** @brief How far an entry's text sits inside its highlight, horizontally and vertically. */
        [[nodiscard]] ImVec2 padding() const { return _padding; }
        /** @brief Sets that inset. It is part of a line's height, so the list re-measures. */
        void setPadding(const ImVec2 padding) { _padding = padding; }

        /**
         * @brief The corner radius of an entry's highlight. Negative — the default — follows
         *        ImGuiStyle::FrameRounding, so the panel looks like the rest of the application.
         */
        [[nodiscard]] float rounding() const { return _rounding; }
        void setRounding(const float rounding) { _rounding = rounding; }

    private:
        /**
         * @brief Brings the panel's copy of the log up to date, cheaply.
         *
         * Only what has arrived since the last call is copied — usually nothing. The whole history was
         * copied here once a frame, strings and all, and at a few hundred messages that alone cost most
         * of a frame. @see kor::log::historySince
         */
        void sync()
        {
            // The log was cleared (or is shorter than what we hold): start again rather than keep
            // showing records that are no longer in it.
            if (const auto last = kor::log::lastSequence(); last < _lastSequence) {
                _records.clear();
                _lastSequence = 0;
            }

            auto arrived = kor::log::historySince(_lastSequence);
            if (!arrived.empty()) {
                _lastSequence = arrived.back().sequence;
                _records.insert(_records.end(), std::make_move_iterator(arrived.begin()),
                                std::make_move_iterator(arrived.end()));
            }

            // Bounded by what the log itself keeps, so a panel left open all run does not grow for ever.
            if (const auto limit = kor::log::historyLimit(); limit > 0 && _records.size() > limit) {
                _records.erase(_records.begin(),
                               _records.begin() + static_cast<std::ptrdiff_t>(_records.size() - limit));
            }
        }

        static constexpr std::array kLevels {
            kor::log::Level::eInfo, kor::log::Level::eWarn, kor::log::Level::eError };

        static std::size_t index(const kor::log::Level level) { return static_cast<std::size_t>(level); }

        static ImVec4 colorFor(const kor::log::Level level)
        {
            switch (level) {
            case kor::log::Level::eWarn:  return ImVec4(1.00f, 0.78f, 0.24f, 1.f);
            case kor::log::Level::eError: return ImVec4(1.00f, 0.38f, 0.35f, 1.f);
            default:                      return ImGui::GetStyleColorVec4(ImGuiCol_Text);
            }
        }

        static const char* labelFor(const kor::log::Level level)
        {
            switch (level) {
            case kor::log::Level::eWarn:  return "warn";
            case kor::log::Level::eError: return "error";
            default:                      return "info";
            }
        }

        void drawToolbar(const std::vector<kor::log::Record>& records)
        {
            if (ImGui::Button("Clear")) {
                kor::log::clearHistory();
                _records.clear();   // what this panel holds is its own copy; the log's clear does not reach it
            }
            ImGui::SameLine();
            _copying = ImGui::Button("Copy");
            ImGui::SameLine();
            ImGui::Checkbox("Follow", &_followTail);
            ImGui::SameLine();
            ImGui::Checkbox("Times", &_showTimes);

            // How many of each level there are, on the toggle itself: "are there errors?" is the
            // question a log panel is opened to answer, and it costs one pass over a snapshot that
            // has to be taken anyway.
            std::array<std::size_t, kLevels.size()> counts { };
            for (const auto& record : records) ++counts[index(record.level)];

            for (const auto level : kLevels) {
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Text, colorFor(level));
                char label[32];
                std::snprintf(label, sizeof(label), "%s (%zu)", labelFor(level), counts[index(level)]);
                ImGui::Checkbox(label, &_show[index(level)]);
                ImGui::PopStyleColor();
            }

            // What the filters left, against what there is. Without it a text filter that matches
            // nothing looks exactly like a log that has nothing in it.
            ImGui::SameLine();
            ImGui::TextDisabled("%zu / %zu", _visible.size(), records.size());

            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputTextWithHint("##filter", "filter", _filter.data(), _filter.size());
        }

        [[nodiscard]] bool passes(const kor::log::Record& record) const
        {
            if (!_show[index(record.level)]) return false;
            if (_filter[0] == '\0') return true;

            // Case-insensitive substring, which is what a filter box is taken to mean.
            const std::string_view needle(_filter.data());
            const auto found = std::ranges::search(record.message, needle,
                [](const char a, const char b) {
                    return std::tolower(static_cast<unsigned char>(a)) ==
                           std::tolower(static_cast<unsigned char>(b));
                });
            return !found.empty();
        }

        /**
         * @brief Recomputes which records the filters let through, when something has changed.
         *
         * Kept as a list of indices so drawing can jump straight to the n-th *visible* line without
         * walking the ones it hides.
         */
        void refilter()
        {
            // The oldest record's sequence, not just how many there are: sync() drops from the front
            // once the log is full, and a frame that drops one and gains one leaves the count alone
            // while shifting every index by one — which would leave _visible pointing at the wrong
            // lines, silently.
            const std::uint64_t oldest = _records.empty() ? 0 : _records.front().sequence;

            const std::string_view filter(_filter.data());
            const bool unchanged = _records.size() == _filteredAt
                && oldest == _filteredFrom
                && filter == _filteredBy
                && _show[0] == _filteredShow[0] && _show[1] == _filteredShow[1] && _show[2] == _filteredShow[2];
            if (unchanged) return;

            _filteredAt = _records.size();
            _filteredFrom = oldest;
            _filteredBy = filter;
            _filteredShow[0] = _show[0]; _filteredShow[1] = _show[1]; _filteredShow[2] = _show[2];

            _visible.clear();
            for (std::size_t i = 0; i < _records.size(); ++i) {
                if (passes(_records[i])) _visible.push_back(i);
            }
            // Every cached height belongs to the old list of lines. Bumping this is what tells
            // measure() so, since two different filters can leave the same *number* of lines.
            ++_generation;
        }

        /** @brief The text of one entry as the list shows it: the time, if times are on, then the message. */
        void composeInto(const kor::log::Record& record, std::string& out) const
        {
            out.clear();
            if (_showTimes) {
                char stamp[24];
                std::snprintf(stamp, sizeof(stamp), "[%8.3f] ", record.time);
                out += stamp;
            }
            out += record.message;
        }

        /** @brief The same text, as a string of its own — for the clipboard, which outlives the scratch. */
        [[nodiscard]] std::string compose(const kor::log::Record& record) const
        {
            std::string line;
            composeInto(record, line);
            return line;
        }

        /**
         * @brief Measures every visible line, so the list can be scrolled without drawing all of it.
         *
         * Wrapped lines are not all the same height, which is the one thing ImGuiListClipper needs, so
         * the panel keeps the heights itself as running offsets: _offsets[i] is where line i starts and
         * _offsets.back() is how tall the whole list is. A binary search over that turns "what is on
         * screen" into two lookups, and the frame draws only those lines.
         *
         * Measuring is the expensive part — it renders every line's metrics — so it happens only when
         * the answer would differ: a new or re-filtered list, a resized window, times switched on or
         * off. Scrolling, which is what actually happens, costs nothing.
         */
        void measure(const std::vector<kor::log::Record>& records, const float width)
        {
            const float spacing = ImGui::GetStyle().ItemSpacing.y;
            if (_measuredGeneration == _generation && _measuredWidth == width
                && _measuredTimes == _showTimes && _measuredSpacing == spacing
                && _measuredPadding.x == _padding.x && _measuredPadding.y == _padding.y) return;

            _measuredGeneration = _generation;
            _measuredWidth = width;
            _measuredTimes = _showTimes;
            _measuredSpacing = spacing;
            _measuredPadding = _padding;

            // The text is inset on both sides, so it has that much less room to wrap in — measuring
            // against the full width would let a line run under the padding and out of its own box.
            const float wrapWidth = std::max(width - 2.f * _padding.x, 1.f);

            _offsets.clear();
            _offsets.reserve(_visible.size() + 1);
            _offsets.push_back(0.f);

            float y = 0.f;
            for (const auto record : _visible) {
                composeInto(records[record], _scratch);
                const ImVec2 size = ImGui::CalcTextSize(
                    _scratch.c_str(), _scratch.c_str() + _scratch.size(), false, wrapWidth);
                y += size.y + 2.f * _padding.y + spacing;
                _offsets.push_back(y);
            }
        }

        /** @brief Level, sequence number, when — and the message in full. Shown while an entry is hovered. */
        void drawTooltip(const kor::log::Record& record) const
        {
            if (!ImGui::BeginTooltip()) return;

            ImGui::PushStyleColor(ImGuiCol_Text, colorFor(record.level));
            ImGui::TextUnformatted(labelFor(record.level));
            ImGui::PopStyleColor();
            ImGui::SameLine();
            // The sequence number is worth showing: it is the same one kor::log::historySince takes,
            // and it says how many messages were suppressed or dropped between two lines.
            ImGui::TextDisabled("#%llu  at %.3f s",
                static_cast<unsigned long long>(record.sequence), record.time);

            ImGui::Separator();
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 40.f);
            ImGui::TextUnformatted(record.message.c_str());
            ImGui::PopTextWrapPos();

            ImGui::EndTooltip();
        }

        /** @brief Right-click on an entry: take it away with you, or narrow the list down to its kind. */
        void drawContextMenu(const kor::log::Record& record)
        {
            if (!ImGui::BeginPopupContextItem("##entry")) return;

            // Right-clicking selects too, so the entry the menu belongs to is the highlighted one.
            _selected = record.sequence;

            if (ImGui::MenuItem("Copy message")) ImGui::SetClipboardText(record.message.c_str());
            if (ImGui::MenuItem("Copy line"))    ImGui::SetClipboardText(compose(record).c_str());
            ImGui::Separator();
            if (ImGui::MenuItem("Show only this level"))
                for (const auto level : kLevels) _show[index(level)] = level == record.level;
            if (ImGui::MenuItem("Show all levels"))
                for (const auto level : kLevels) _show[index(level)] = true;

            ImGui::EndPopup();
        }

        /**
         * @brief One entry: a row that highlights under the pointer, with the wrapped text drawn over it.
         * @param record The entry.
         * @param slot Its position in the visible list, which is where its measured height lives.
         *
         * The row and the text are two items in the same place: an invisible Selectable as tall as the
         * wrapped text plus its padding gives the whole block one hover rectangle — hovering the second
         * line of a three-line message must highlight the message, not a gap — and the text is then
         * drawn back over it, inset by the padding. The highlight itself is drawn by hand, because
         * ImGui's own is square-cornered with no way to ask for anything else.
         */
        void drawEntry(const kor::log::Record& record, const std::size_t slot)
        {
            const ImGuiStyle& style = ImGui::GetStyle();
            const float spacing = style.ItemSpacing.y;
            const float height = _offsets[slot + 1] - _offsets[slot] - spacing;
            const float top = ImGui::GetCursorPosY();
            const float left = ImGui::GetCursorPosX();

            // Keyed on the record, not on where it currently sits: a line's position shifts every time
            // the log grows, and an id that shifted with it would close the context menu opened on it.
            ImGui::PushID(static_cast<int>(record.sequence));

            // The Selectable is for behaviour only — hit-testing, clicking, keyboard navigation and an
            // id for the context menu. Its own highlight is turned off because ImGui draws that one
            // with square corners, hard-coded, and there is no style variable that rounds it.
            ImGui::PushStyleColor(ImGuiCol_Header,        ImVec4(0.f, 0.f, 0.f, 0.f));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.f, 0.f, 0.f, 0.f));
            ImGui::PushStyleColor(ImGuiCol_HeaderActive,  ImVec4(0.f, 0.f, 0.f, 0.f));
            const bool wasSelected = record.sequence == _selected;
            if (ImGui::Selectable("##row", wasSelected, ImGuiSelectableFlags_None, ImVec2(0.f, height)))
                _selected = wasSelected ? 0 : record.sequence;   // clicking the selected line lets it go
            ImGui::PopStyleColor(3);

            const bool hovered = ImGui::IsItemHovered();
            const bool held = ImGui::IsItemActive();

            // The highlight, drawn here instead: same colours ImGui would have used, rounded corners,
            // and exactly the rectangle the Selectable claimed — so what is lit is what is clickable.
            if (hovered || held || wasSelected) {
                const ImU32 fill = ImGui::GetColorU32(held ? ImGuiCol_HeaderActive
                                                   : hovered ? ImGuiCol_HeaderHovered
                                                             : ImGuiCol_Header);
                ImGui::GetWindowDrawList()->AddRectFilled(
                    ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), fill,
                    _rounding < 0.f ? style.FrameRounding : _rounding);
            }

            drawContextMenu(record);

            // Inside the box by the padding, on both axes, and wrapping the same distance short of the
            // right edge — which is what measure() sized this row for.
            ImGui::SetCursorPos(ImVec2(left + _padding.x, top + _padding.y));
            composeInto(record, _scratch);
            ImGui::PushStyleColor(ImGuiCol_Text, colorFor(record.level));
            ImGui::PushTextWrapPos(left + _measuredWidth - _padding.x);
            ImGui::TextUnformatted(_scratch.c_str(), _scratch.c_str() + _scratch.size());
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();

            // After the text, so the tooltip is not what the text is drawn over.
            if (hovered) drawTooltip(record);

            ImGui::PopID();

            // The text left the cursor a padding short of where the row ends, so put it where the next
            // row starts: the offsets are what the culling promised, and drawing has to keep to them.
            ImGui::SetCursorPosY(top + height + spacing);
        }

        void drawLines(const std::vector<kor::log::Record>& records)
        {
            refilter();

            // No horizontal scrollbar: lines wrap instead of running off the edge, so there is nothing
            // to scroll sideways to.
            if (ImGui::BeginChild("##lines", ImVec2(0, 0), ImGuiChildFlags_None)) {
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 1));

                measure(records, ImGui::GetContentRegionAvail().x);

                if (_copying) {
                    // Copying wants the whole log, not the part that happens to be on screen, and it
                    // wants the text — not the rows the text is drawn on.
                    ImGui::LogToClipboard();
                    for (const auto record : _visible) {
                        composeInto(records[record], _scratch);
                        ImGui::TextUnformatted(_scratch.c_str(), _scratch.c_str() + _scratch.size());
                    }
                    ImGui::LogFinish();
                } else {
                    drawVisibleLines(records);
                }

                ImGui::PopStyleVar();

                // Follow the tail only when the view is already at the bottom, so scrolling back to
                // read something is not yanked away by the next message.
                if (_followTail && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.f)
                    ImGui::SetScrollHereY(1.f);
            }
            ImGui::EndChild();
        }

        /**
         * @brief Draws the lines the view actually shows, and reserves the space of the ones it does not.
         *
         * The same trick ImGuiListClipper plays, over the measured offsets: skip to where the first
         * on-screen line starts, draw down to the last one, then jump the cursor to the full height so
         * the scrollbar still describes the whole list. Everything above and below costs a comparison.
         */
        void drawVisibleLines(const std::vector<kor::log::Record>& records)
        {
            if (_visible.empty()) {
                ImGui::TextDisabled(_records.empty() ? "Nothing has been logged."
                                                     : "Nothing matches the filter.");
                return;
            }

            const float top = ImGui::GetCursorPosY();
            const float from = ImGui::GetScrollY() - top;
            const auto [first, last] = log_detail::linesIn(_offsets, from, from + ImGui::GetWindowSize().y);

            ImGui::SetCursorPosY(top + _offsets[first]);
            for (std::size_t slot = first; slot < last; ++slot)
                drawEntry(records[_visible[slot]], slot);

            // The height of everything that was not drawn, so the scrollbar is the whole log's.
            ImGui::SetCursorPosY(top + _offsets.back());
            ImGui::Dummy(ImVec2(0.f, 0.f));
        }

        /// What the panel has been given so far, kept rather than re-copied each frame. @see sync
        std::vector<kor::log::Record> _records;
        std::uint64_t _lastSequence = 0;

        /// Indices of the records the filters let through, and what they were computed from. @see refilter
        std::vector<std::size_t> _visible;
        std::size_t _filteredAt = static_cast<std::size_t>(-1);
        std::uint64_t _filteredFrom = 0;
        std::string _filteredBy;
        bool _filteredShow[3] { true, true, true };
        /// Bumped whenever _visible is rebuilt, so measured heights know they are stale. @see measure
        std::uint64_t _generation = 0;

        /// Where each visible line starts, and what those offsets were measured against. @see measure
        std::vector<float> _offsets;
        std::uint64_t _measuredGeneration = static_cast<std::uint64_t>(-1);
        float _measuredWidth = -1.f;
        float _measuredSpacing = -1.f;
        bool _measuredTimes = false;

        ImVec2 _measuredPadding { -1.f, -1.f };

        /// One line's text, reused: composing is per visible line per frame, and allocating there shows.
        mutable std::string _scratch;

        ImVec2 _padding { 6.f, 2.f };
        float _rounding = -1.f;

        bool _show[3] { true, true, true };
        bool _followTail = true;
        bool _showTimes = true;
        bool _copying = false;
        std::uint64_t _selected = 0;
        std::array<char, 128> _filter { };
    };
}
