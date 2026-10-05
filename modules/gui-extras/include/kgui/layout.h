//
// koral-gui-extras: the few shapes every panel here is made of — a label beside its editor, muted text —
// and a widget that keeps up with something that changes outside it.
//

#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <kui/widgets.h>

namespace kgui
{
    /** @brief Text in the theme's quieter colour: what says what something is rather than what it holds. */
    inline kui::Widget Muted(std::string text, const float size = 13.f)
    {
        return kui::Text(std::move(text), kui::TextStyle {}.SetColor(kui::Theme::Current().textMuted).SetSize(size));
    }

    /** @brief A line of text that never wraps, cut short with an ellipsis where it does not fit. */
    inline kui::Widget Line(std::string text, kui::TextStyle style = {})
    {
        return kui::Text(std::move(text), style, kui::TextAlign::eStart, false, 1, true);
    }

    /** @brief @p editor after @p label, the labels of a column of them lining up at @p labelWidth. */
    inline kui::Widget Labeled(std::string label, kui::Widget editor, const float labelWidth = 110.f)
    {
        return kui::Row({
            kui::SizedBox(labelWidth, -1.f, Line(std::move(label), kui::TextStyle {}.SetColor(kui::Theme::Current().textMuted))),
            kui::Expanded(std::move(editor)),
        }, kui::FlexOptions {}.SetGap(8.f));
    }

    /** @brief Drags side by side, one a component: a vector edited as its numbers. @p set gets them all back, one changed. */
    inline kui::Widget DragFloats(std::vector<float> values, std::function<void(std::vector<float>)> set, const float speed = 0.01f,
                                  const int decimals = 2, const float min = -kui::Infinity, const float max = kui::Infinity)
    {
        static constexpr const char* axes[] { "X", "Y", "Z", "W" };
        std::vector<kui::Widget> parts;
        for (std::size_t i = 0; i < values.size(); ++i) {
            auto options = kui::DragValueOptions {}.SetSpeed(speed).SetDecimals(decimals).SetRange(min, max).SetWidth(48.f);
            if (values.size() > 1 && i < 4) options.SetLabel(axes[i]);
            parts.push_back(kui::Expanded(kui::DragValue(values[i], [values, set, i](const float v) {
                auto changed = values;
                changed[i] = v;
                if (set) set(changed);
            }, options)));
        }
        return kui::Row(std::move(parts), kui::FlexOptions {}.SetGap(4.f));
    }

    /**
     * @brief A widget that keeps up with something outside it: Poll is asked every frame whether what it
     *        shows changed, and it is built again when it did. Poll throttles itself where it likes.
     */
    class Live : public kui::StatefulWidget {
    public:
        void InitState() override
        {
            Animate([this](const float dt) {
                if (Poll(dt)) SetState();
                return true;
            });
        }

    protected:
        /** @brief Whether to build again: what it shows changed. Every frame, with the seconds since the last. */
        virtual bool Poll(float dt) = 0;

        /** @brief True once every @p seconds: for a panel that refreshes at a steady rate. */
        bool Every(const float seconds, const float dt)
        {
            _since += dt;
            if (_since < seconds) return false;
            _since = 0.f;
            return true;
        }

    private:
        float _since = 0.f;
    };
}
