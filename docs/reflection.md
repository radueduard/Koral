# Reflection and serialization

A type says once what it is made of, and everything that walks objects generically works with it:
`kgui::Inspect` draws an editor for it, `ToJson` / `FromJson` save and load it, a scene's `State()` is
carried across a hot reload with it, and a binding to another language reads the same description.

```cpp
struct Light {
    glm::vec3 position;
    glm::vec3 color {1.f};
    float intensity = 1.f;
    std::vector<std::string> tags;
};
KORAL_REFLECT(Light, position, color, intensity, tags)
```

`KORAL_REFLECT` goes next to the type, in its namespace (up to 16 fields). For private fields, add
`KORAL_REFLECT_FRIEND(Light);` inside the class. For more than a list of fields, write the function the
macro would have written:

```cpp
inline void KoralReflect(kor::TypeBuilder<Light>& type) {
    type.Name("Light");
    type.Field("position", &Light::position);
    type.Field("color", &Light::color).Color();                       // a colour picker
    type.Field("intensity", &Light::intensity).Range(0.f, 100.f).Tooltip("In candela");
    type.Field("cache", &Light::cache).Transient();                   // not saved
}
```

Enums are named with `KORAL_REFLECT_ENUM(Shape, eSphere, eBox)`, and saved by name.

Built in, with nothing to write: `bool`, the integer and floating-point types, `std::string`, glm's
vectors (float, int and uint), `glm::quat`, `glm::mat4`, and `std::vector` of anything reflectable.

## Reaching into objects

`kor::TypeOf<T>()` is the description — a `TypeInfo` with its fields, their types and attributes — and
`kor::FindType("Light")` finds one by name. A `kor::Ref` is any reflected object by address, for code
that does not know its type:

```cpp
kor::Ref light(myLight);
light.Field("intensity").As<float>() = 2.f;
light.Field("tags").Resize(3);
```

## JSON

```cpp
std::string saved = kor::ToJson(light);            // {"position":[0,0,0],"color":[1,1,1],...}
if (auto loaded = kor::FromJson(light, saved); !loaded) log(loaded.error().message);
```

Loading keeps what the JSON leaves out and skips what the type does not have, so a file from before a
field was added — or after one was removed — still loads. A value of the wrong kind is reported by its
path (`lights[2].color`). `CopyFields(from, to)` copies one object into another of the same type.

## An editor

```cpp
kui::Ui _ui { kgui::Inspector(myLight, [this] { lightsChanged = true; }, "Light") };
```

Every field gets the editor its kind calls for — a checkbox, a drag (kept to the field's range where it has
one), a text box, a colour picker, a dropdown for an enum, a tree for a struct, a tree with + and − for an
array. The object is read again as it changes, so a value the program sets shows straight away.
