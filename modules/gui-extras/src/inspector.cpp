//
// The inspector: a reflected object walked field by field, each given the editor its kind calls for, and
// built again when the object changes under it.
//

#include "koralInspector.h"

#include <cmath>
#include <format>
#include <map>

#include <glm/gtc/quaternion.hpp>

#include "kgui/layout.h"

namespace kgui
{
    namespace {
        using kor::TypeKind;
        /** Where a value is now: asked again every time it is read or written, since the object may move. */
        using Getter = std::function<kor::Ref()>;

        class InspectorWidget final : public Live {
        public:
            InspectorWidget(Getter value, std::function<void()> onChanged, std::string label)
                : _value(std::move(value)), _onChanged(std::move(onChanged)), _label(std::move(label)) {}

            void DidUpdateWidget(const kui::StatefulWidget& newer) override
            {
                const auto& other = static_cast<const InspectorWidget&>(newer);
                _value = other._value;
                _onChanged = other._onChanged;
                _label = other._label;
            }

            kui::Widget Build() override
            {
                const kor::Ref value = _value ? _value() : kor::Ref {};
                _shownAt = value.Address();
                if (!value.Valid()) return Muted("nothing to inspect");
                _snapshot = kor::ToJson(value);
                std::vector<kui::Widget> rows;
                if (!_label.empty()) {
                    kui::TextStyle heading;
                    heading.weight = 600.f;
                    rows.push_back(kui::Text(_label, heading));
                    rows.push_back(kui::Separator());
                }
                // A struct is shown as its fields directly, the label heading them; anything else as the one
                // editor its kind calls for.
                if (value.Type().kind == TypeKind::eStruct) {
                    for (auto& row : Fields(value, _value, "")) rows.push_back(std::move(row));
                } else {
                    rows.push_back(Value(_label.empty() ? "value" : _label, value, _value, nullptr, "value"));
                }
                return kui::Column(std::move(rows), kui::FlexOptions {}.SetGap(6.f).SetCrossAxisAlignment(kui::CrossAxisAlignment::eStretch));
            }

        protected:
            /** Changed by the program rather than here: read again, ten times a second. */
            bool Poll(const float dt) override
            {
                const kor::Ref value = _value ? _value() : kor::Ref {};
                // Moved, or gone: shown again at once, before anything reads where it was.
                if (value.Address() != _shownAt) return true;
                if (!value.Valid() || !Every(0.1f, dt)) return false;
                return kor::ToJson(value) != _snapshot;
            }

        private:
            /** An edit made: told, and the editors built again from what the object now holds. */
            void Changed()
            {
                if (_onChanged) _onChanged();
                SetState();
            }

            std::vector<kui::Widget> Fields(const kor::Ref value, const Getter& get, const std::string& path)
            {
                std::vector<kui::Widget> rows;
                for (const auto& field : value.Type().fields) {
                    const std::string name = field.name;
                    rows.push_back(Value(name, value.Field(name), [get, name] { return get().Field(name); }, &field, path + "/" + name));
                }
                return rows;
            }

            /** Where @p get says the value is now, as a @p T — or null, gone. */
            template<typename T>
            static T* At(const Getter& get)
            {
                const kor::Ref ref = get();
                return ref.Valid() ? static_cast<T*>(ref.Address()) : nullptr;
            }

            kui::Widget Tree(std::string label, const std::string& path, std::vector<kui::Widget> children)
            {
                return kui::TreeNode(std::move(label), _open.contains(path), [this, path](const bool open) {
                    SetState([&] { if (open) _open[path] = true; else _open.erase(path); });
                }, std::move(children));
            }

            template<typename T>
            kui::Widget Number(const std::string& label, void* address, const Getter& get, const kor::FieldInfo* field, const bool whole)
            {
                auto options = kui::DragValueOptions {}.SetSpeed(whole ? 0.2f : 0.01f).SetDecimals(whole ? 0 : 3);
                if (field && field->min && field->max) options.SetRange(static_cast<float>(*field->min), static_cast<float>(*field->max));
                return Labeled(label, kui::DragValue(static_cast<float>(*static_cast<T*>(address)), [this, get, whole](const float v) {
                    if (T* target = At<T>(get)) *target = static_cast<T>(whole ? std::round(v) : v);
                    Changed();
                }, options));
            }

            template<typename V>
            kui::Widget Vector(const std::string& label, void* address, const Getter& get, const bool whole, const float speed = 0.01f)
            {
                const auto& now = *static_cast<V*>(address);
                std::vector<float> values;
                for (int i = 0; i < V::length(); ++i) values.push_back(static_cast<float>(now[i]));
                return Labeled(label, DragFloats(std::move(values), [this, get, whole](const std::vector<float>& v) {
                    if (V* target = At<V>(get))
                        for (int i = 0; i < V::length(); ++i)
                            (*target)[i] = static_cast<typename V::value_type>(whole ? std::round(v[i]) : v[i]);
                    Changed();
                }, whole ? 0.2f : speed, whole ? 0 : 3));
            }

            template<typename V>
            kui::Widget Colour(const std::string& label, void* address, const Getter& get)
            {
                const auto& value = *static_cast<V*>(address);
                const kui::Color now { value[0], value[1], value[2], V::length() == 4 ? value[3] : 1.f };
                kui::ColorPickerOptions options;
                options.alpha = V::length() == 4;
                return Labeled(label, kui::ColorEdit(now, [this, get](const kui::Color c) {
                    if (V* target = At<V>(get)) {
                        (*target)[0] = c.r; (*target)[1] = c.g; (*target)[2] = c.b;
                        if constexpr (V::length() == 4) (*target)[3] = c.a;
                    }
                    Changed();
                }, {}, options));
            }

            kui::Widget Value(const std::string& label, const kor::Ref value, const Getter& get, const kor::FieldInfo* field, const std::string& path)
            {
                const auto& type = value.Type();
                void* address = value.Address();
                kui::Widget editor;
                switch (type.kind) {
                case TypeKind::eBool: {
                    editor = kui::Checkbox(*static_cast<bool*>(address), [this, get](const bool on) {
                        if (bool* target = At<bool>(get)) *target = on;
                        Changed();
                    }, label);
                    break;
                }
                case TypeKind::eInt8: editor = Number<std::int8_t>(label, address, get, field, true); break;
                case TypeKind::eInt16: editor = Number<std::int16_t>(label, address, get, field, true); break;
                case TypeKind::eInt32: editor = Number<std::int32_t>(label, address, get, field, true); break;
                case TypeKind::eInt64: editor = Number<std::int64_t>(label, address, get, field, true); break;
                case TypeKind::eUInt8: editor = Number<std::uint8_t>(label, address, get, field, true); break;
                case TypeKind::eUInt16: editor = Number<std::uint16_t>(label, address, get, field, true); break;
                case TypeKind::eUInt32: editor = Number<std::uint32_t>(label, address, get, field, true); break;
                case TypeKind::eUInt64: editor = Number<std::uint64_t>(label, address, get, field, true); break;
                case TypeKind::eFloat: editor = Number<float>(label, address, get, field, false); break;
                case TypeKind::eDouble: editor = Number<double>(label, address, get, field, false); break;
                case TypeKind::eString: {
                    kui::TextFieldOptions options;
                    options.text = *static_cast<std::string*>(address);
                    options.controlled = true;
                    options.onChanged = [this, get](const std::string& text) {
                        if (auto* target = At<std::string>(get)) *target = text;
                        Changed();
                    };
                    editor = Labeled(label, kui::TextField(std::move(options)));
                    break;
                }
                case TypeKind::eVec2: editor = Vector<glm::vec2>(label, address, get, false); break;
                case TypeKind::eVec3: editor = field && field->color ? Colour<glm::vec3>(label, address, get) : Vector<glm::vec3>(label, address, get, false); break;
                case TypeKind::eVec4: editor = field && field->color ? Colour<glm::vec4>(label, address, get) : Vector<glm::vec4>(label, address, get, false); break;
                case TypeKind::eIVec2: editor = Vector<glm::ivec2>(label, address, get, true); break;
                case TypeKind::eIVec3: editor = Vector<glm::ivec3>(label, address, get, true); break;
                case TypeKind::eIVec4: editor = Vector<glm::ivec4>(label, address, get, true); break;
                case TypeKind::eUVec2: editor = Vector<glm::uvec2>(label, address, get, true); break;
                case TypeKind::eUVec3: editor = Vector<glm::uvec3>(label, address, get, true); break;
                case TypeKind::eUVec4: editor = Vector<glm::uvec4>(label, address, get, true); break;
                case TypeKind::eQuat: {
                    // As Euler angles in degrees: what a person thinks in.
                    const glm::vec3 degrees = glm::degrees(glm::eulerAngles(*static_cast<glm::quat*>(address)));
                    editor = Labeled(label, DragFloats({ degrees.x, degrees.y, degrees.z }, [this, get](const std::vector<float>& v) {
                        if (auto* rotation = At<glm::quat>(get)) *rotation = glm::quat(glm::radians(glm::vec3(v[0], v[1], v[2])));
                        Changed();
                    }, 0.5f, 1));
                    break;
                }
                case TypeKind::eMat4: {
                    const auto& matrix = *static_cast<glm::mat4*>(address);
                    std::vector<kui::Widget> columns;
                    for (int column = 0; column < 4; ++column) {
                        const glm::vec4 c = matrix[column];
                        columns.push_back(DragFloats({ c.x, c.y, c.z, c.w }, [this, get, column](const std::vector<float>& v) {
                            if (auto* target = At<glm::mat4>(get)) (*target)[column] = { v[0], v[1], v[2], v[3] };
                            Changed();
                        }));
                    }
                    editor = Tree(label, path, std::move(columns));
                    break;
                }
                case TypeKind::eEnum: {
                    const auto current = type.enumRead(address);
                    if (type.enumerators.empty()) {
                        editor = Labeled(label, kui::DragValue(static_cast<float>(current), [this, &type, get](const float v) {
                            if (const kor::Ref ref = get(); ref.Valid()) type.enumWrite(ref.Address(), static_cast<std::int64_t>(std::round(v)));
                            Changed();
                        }, kui::DragValueOptions {}.SetDecimals(0).SetSpeed(0.2f)));
                        break;
                    }
                    std::vector<std::string> names;
                    int selected = -1;
                    for (std::size_t i = 0; i < type.enumerators.size(); ++i) {
                        names.push_back(type.enumerators[i].first);
                        if (type.enumerators[i].second == current) selected = static_cast<int>(i);
                    }
                    editor = Labeled(label, kui::Dropdown(std::move(names), selected, [this, &type, get](const int index) {
                        if (const kor::Ref ref = get(); ref.Valid()) type.enumWrite(ref.Address(), type.enumerators[static_cast<std::size_t>(index)].second);
                        Changed();
                    }));
                    break;
                }
                case TypeKind::eStruct:
                    editor = Tree(label, path, Fields(value, get, path));
                    break;
                case TypeKind::eArray: {
                    // + and − for the end of it, then each element by its index.
                    std::vector<kui::Widget> elements {
                        kui::Row({
                            kui::Button("+", [this, get] { if (const kor::Ref a = get(); a.Valid()) a.Resize(a.Size() + 1); Changed(); },
                                        kui::ButtonOptions {}.SetStyle(kui::ButtonStyle::eSecondary)),
                            kui::Button("-", [this, get] { if (const kor::Ref a = get(); a.Valid() && a.Size() > 0) { a.Resize(a.Size() - 1); Changed(); } },
                                        kui::ButtonOptions {}.SetStyle(kui::ButtonStyle::eSecondary)),
                        }, kui::FlexOptions {}.SetGap(6.f)),
                    };
                    for (std::size_t i = 0; i < value.Size(); ++i)
                        elements.push_back(Value(std::format("[{}]", i), value.At(i), [get, i] { return get().At(i); }, nullptr, std::format("{}/{}", path, i)));
                    editor = Tree(std::format("{} [{}]", label, value.Size()), path, std::move(elements));
                    break;
                }
                }
                if (field && field->readOnly) editor = kui::Disabled(std::move(editor));
                if (field && !field->tooltip.empty()) editor = kui::Tooltip(field->tooltip, std::move(editor));
                return editor;
            }

            Getter _value;
            const void* _shownAt = nullptr;         // where the object was when it was last shown
            std::function<void()> _onChanged;
            std::string _label;
            std::string _snapshot;                  // the object as it was last shown, to see it change
            std::map<std::string, bool> _open;      // the trees open, by where they are in the object
        };
    }

    kui::Widget Inspector(const kor::Ref value, std::function<void()> onChanged, std::string label)
    {
        return kui::Make<InspectorWidget>([value] { return value; }, std::move(onChanged), std::move(label));
    }

    kui::Widget Inspector(std::function<kor::Ref()> value, std::function<void()> onChanged, std::string label)
    {
        return kui::Make<InspectorWidget>(std::move(value), std::move(onChanged), std::move(label));
    }
}
