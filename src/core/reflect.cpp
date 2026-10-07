//
// Run-time type descriptions, and JSON through them.
//

#include "reflect.h"

#include <cstring>
#include <format>
#include <map>
#include <mutex>

#include <yyjson.h>

namespace kor
{
    // ---- the registry ---------------------------------------------------------------------------------

    namespace {
        struct Registry {
            std::mutex mutex;
            std::map<std::string, const TypeInfo*, std::less<>> byName;
        };
        Registry& registry() {
            static Registry instance;
            return instance;
        }
    }

    void detail::RegisterType(const TypeInfo* type)
    {
        auto& r = registry();
        std::lock_guard lock(r.mutex);
        // The first description of a name wins: a scene library's own copy of a type describes the
        // same thing, and a lookup by name should not change answer when one is loaded.
        r.byName.try_emplace(type->name, type);
    }

    const TypeInfo* FindType(const std::string_view name)
    {
        auto& r = registry();
        std::lock_guard lock(r.mutex);
        const auto it = r.byName.find(name);
        return it == r.byName.end() ? nullptr : it->second;
    }

    std::string_view detail::KindName(const TypeKind kind)
    {
        switch (kind) {
        case TypeKind::eBool: return "bool";
        case TypeKind::eInt8: return "int8";
        case TypeKind::eInt16: return "int16";
        case TypeKind::eInt32: return "int32";
        case TypeKind::eInt64: return "int64";
        case TypeKind::eUInt8: return "uint8";
        case TypeKind::eUInt16: return "uint16";
        case TypeKind::eUInt32: return "uint32";
        case TypeKind::eUInt64: return "uint64";
        case TypeKind::eFloat: return "float";
        case TypeKind::eDouble: return "double";
        case TypeKind::eString: return "string";
        case TypeKind::eVec2: return "vec2";
        case TypeKind::eVec3: return "vec3";
        case TypeKind::eVec4: return "vec4";
        case TypeKind::eIVec2: return "ivec2";
        case TypeKind::eIVec3: return "ivec3";
        case TypeKind::eIVec4: return "ivec4";
        case TypeKind::eUVec2: return "uvec2";
        case TypeKind::eUVec3: return "uvec3";
        case TypeKind::eUVec4: return "uvec4";
        case TypeKind::eQuat: return "quat";
        case TypeKind::eMat4: return "mat4";
        case TypeKind::eEnum: return "enum";
        case TypeKind::eStruct: return "struct";
        case TypeKind::eArray: return "array";
        }
        return "?";
    }

    // ---- descriptions ---------------------------------------------------------------------------------

    const FieldInfo* TypeInfo::Field(const std::string_view name) const
    {
        for (const auto& field : fields) if (field.name == name) return &field;
        return nullptr;
    }

    std::optional<std::int64_t> TypeInfo::EnumValue(const std::string_view name) const
    {
        for (const auto& [enumerator, value] : enumerators) if (enumerator == name) return value;
        return std::nullopt;
    }

    std::string_view TypeInfo::EnumName(const std::int64_t value) const
    {
        for (const auto& [enumerator, v] : enumerators) if (v == value) return enumerator;
        return {};
    }

    // ---- references -----------------------------------------------------------------------------------

    Ref Ref::Field(const std::string_view name) const
    {
        if (!Valid() || _type->kind != TypeKind::eStruct) return {};
        const auto* field = _type->Field(name);
        if (!field) return {};
        return Ref(field->address(_object), *field->type);
    }

    Ref Ref::At(const std::size_t index) const
    {
        if (!Valid() || _type->kind != TypeKind::eArray || index >= Size()) return {};
        return Ref(_type->arrayAt(_object, index), *_type->element);
    }

    std::size_t Ref::Size() const
    {
        return Valid() && _type->kind == TypeKind::eArray ? _type->arraySize(_object) : 0;
    }

    void Ref::Resize(const std::size_t size) const
    {
        if (Valid() && _type->kind == TypeKind::eArray) _type->arrayResize(_object, size);
    }

    // ---- JSON ---------------------------------------------------------------------------------------------

    namespace {
        template<typename V>
        constexpr int componentsOf() { return V::Size; }

        std::size_t componentCount(const TypeKind kind)
        {
            switch (kind) {
            case TypeKind::eVec2: case TypeKind::eIVec2: case TypeKind::eUVec2: return 2;
            case TypeKind::eVec3: case TypeKind::eIVec3: case TypeKind::eUVec3: return 3;
            case TypeKind::eVec4: case TypeKind::eIVec4: case TypeKind::eUVec4: case TypeKind::eQuat: return 4;
            case TypeKind::eMat4: return 16;
            default: return 0;
            }
        }

        yyjson_mut_val* write(yyjson_mut_doc* doc, const void* object, const TypeInfo& type)
        {
            switch (type.kind) {
            case TypeKind::eBool: return yyjson_mut_bool(doc, *static_cast<const bool*>(object));
            case TypeKind::eInt8: return yyjson_mut_sint(doc, *static_cast<const std::int8_t*>(object));
            case TypeKind::eInt16: return yyjson_mut_sint(doc, *static_cast<const std::int16_t*>(object));
            case TypeKind::eInt32: return yyjson_mut_sint(doc, *static_cast<const std::int32_t*>(object));
            case TypeKind::eInt64: return yyjson_mut_sint(doc, *static_cast<const std::int64_t*>(object));
            case TypeKind::eUInt8: return yyjson_mut_uint(doc, *static_cast<const std::uint8_t*>(object));
            case TypeKind::eUInt16: return yyjson_mut_uint(doc, *static_cast<const std::uint16_t*>(object));
            case TypeKind::eUInt32: return yyjson_mut_uint(doc, *static_cast<const std::uint32_t*>(object));
            case TypeKind::eUInt64: return yyjson_mut_uint(doc, *static_cast<const std::uint64_t*>(object));
            case TypeKind::eFloat: return yyjson_mut_real(doc, *static_cast<const float*>(object));
            case TypeKind::eDouble: return yyjson_mut_real(doc, *static_cast<const double*>(object));
            case TypeKind::eString: {
                const auto& s = *static_cast<const std::string*>(object);
                return yyjson_mut_strncpy(doc, s.data(), s.size());
            }
            case TypeKind::eVec2: case TypeKind::eVec3: case TypeKind::eVec4: case TypeKind::eQuat: case TypeKind::eMat4: {
                auto* array = yyjson_mut_arr(doc);
                const auto* components = static_cast<const float*>(object);
                for (std::size_t i = 0; i < componentCount(type.kind); ++i) yyjson_mut_arr_add_real(doc, array, components[i]);
                return array;
            }
            case TypeKind::eIVec2: case TypeKind::eIVec3: case TypeKind::eIVec4: {
                auto* array = yyjson_mut_arr(doc);
                const auto* components = static_cast<const std::int32_t*>(object);
                for (std::size_t i = 0; i < componentCount(type.kind); ++i) yyjson_mut_arr_add_sint(doc, array, components[i]);
                return array;
            }
            case TypeKind::eUVec2: case TypeKind::eUVec3: case TypeKind::eUVec4: {
                auto* array = yyjson_mut_arr(doc);
                const auto* components = static_cast<const std::uint32_t*>(object);
                for (std::size_t i = 0; i < componentCount(type.kind); ++i) yyjson_mut_arr_add_uint(doc, array, components[i]);
                return array;
            }
            case TypeKind::eEnum: {
                const auto value = type.enumRead(object);
                if (const auto name = type.EnumName(value); !name.empty()) return yyjson_mut_strncpy(doc, name.data(), name.size());
                return yyjson_mut_sint(doc, value);
            }
            case TypeKind::eArray: {
                auto* array = yyjson_mut_arr(doc);
                auto* mutableObject = const_cast<void*>(object);
                for (std::size_t i = 0; i < type.arraySize(object); ++i)
                    yyjson_mut_arr_append(array, write(doc, type.arrayAt(mutableObject, i), *type.element));
                return array;
            }
            case TypeKind::eStruct: {
                auto* map = yyjson_mut_obj(doc);
                auto* mutableObject = const_cast<void*>(object);
                for (const auto& field : type.fields) {
                    if (field.transient) continue;
                    yyjson_mut_obj_add(map, yyjson_mut_strncpy(doc, field.name.data(), field.name.size()),
                                       write(doc, field.address(mutableObject), *field.type));
                }
                return map;
            }
            }
            return yyjson_mut_null(doc);
        }

        std::string describe(yyjson_val* value)
        {
            if (yyjson_is_obj(value)) return "an object";
            if (yyjson_is_arr(value)) return std::format("an array of {}", yyjson_arr_size(value));
            if (yyjson_is_str(value)) return std::format("the string \"{}\"", yyjson_get_str(value));
            if (yyjson_is_bool(value)) return "a boolean";
            if (yyjson_is_num(value)) return "a number";
            return "null";
        }

        VoidResult mismatch(const std::string& path, const TypeInfo& type, yyjson_val* value)
        {
            return std::unexpected(Error{ .code = ErrorCode::eInvalidArgument, .message = std::format(
                "'{}' is {} in the JSON, where a {} was expected", path.empty() ? "the value" : path, describe(value), type.name) });
        }

        template<typename T>
        VoidResult readNumber(void* object, yyjson_val* value, const std::string& path, const TypeInfo& type)
        {
            if (std::is_same_v<T, bool>) {
                if (!yyjson_is_bool(value)) return mismatch(path, type, value);
                *static_cast<bool*>(object) = yyjson_get_bool(value);
                return {};
            }
            if (!yyjson_is_num(value)) return mismatch(path, type, value);
            *static_cast<T*>(object) = static_cast<T>(yyjson_get_num(value));
            return {};
        }

        template<typename C>
        VoidResult readComponents(void* object, yyjson_val* value, const std::string& path, const TypeInfo& type)
        {
            const auto count = componentCount(type.kind);
            if (!yyjson_is_arr(value) || yyjson_arr_size(value) != count) return mismatch(path, type, value);
            auto* components = static_cast<C*>(object);
            for (std::size_t i = 0; i < count; ++i) {
                auto* component = yyjson_arr_get(value, i);
                if (!yyjson_is_num(component)) return mismatch(std::format("{}[{}]", path, i), type, component);
                components[i] = static_cast<C>(yyjson_get_num(component));
            }
            return {};
        }

        VoidResult read(void* object, const TypeInfo& type, yyjson_val* value, const std::string& path)
        {
            switch (type.kind) {
            case TypeKind::eBool: return readNumber<bool>(object, value, path, type);
            case TypeKind::eInt8: return readNumber<std::int8_t>(object, value, path, type);
            case TypeKind::eInt16: return readNumber<std::int16_t>(object, value, path, type);
            case TypeKind::eInt32: return readNumber<std::int32_t>(object, value, path, type);
            case TypeKind::eInt64: return readNumber<std::int64_t>(object, value, path, type);
            case TypeKind::eUInt8: return readNumber<std::uint8_t>(object, value, path, type);
            case TypeKind::eUInt16: return readNumber<std::uint16_t>(object, value, path, type);
            case TypeKind::eUInt32: return readNumber<std::uint32_t>(object, value, path, type);
            case TypeKind::eUInt64: return readNumber<std::uint64_t>(object, value, path, type);
            case TypeKind::eFloat: return readNumber<float>(object, value, path, type);
            case TypeKind::eDouble: return readNumber<double>(object, value, path, type);
            case TypeKind::eString:
                if (!yyjson_is_str(value)) return mismatch(path, type, value);
                *static_cast<std::string*>(object) = std::string(yyjson_get_str(value), yyjson_get_len(value));
                return {};
            case TypeKind::eVec2: case TypeKind::eVec3: case TypeKind::eVec4: case TypeKind::eQuat: case TypeKind::eMat4:
                return readComponents<float>(object, value, path, type);
            case TypeKind::eIVec2: case TypeKind::eIVec3: case TypeKind::eIVec4:
                return readComponents<std::int32_t>(object, value, path, type);
            case TypeKind::eUVec2: case TypeKind::eUVec3: case TypeKind::eUVec4:
                return readComponents<std::uint32_t>(object, value, path, type);
            case TypeKind::eEnum:
                if (yyjson_is_str(value)) {
                    const auto named = type.EnumValue(yyjson_get_str(value));
                    if (!named) return std::unexpected(Error{ .code = ErrorCode::eInvalidArgument, .message = std::format(
                        "'{}' is \"{}\", which is not a {}", path, yyjson_get_str(value), type.name) });
                    type.enumWrite(object, *named);
                    return {};
                }
                if (!yyjson_is_int(value)) return mismatch(path, type, value);
                type.enumWrite(object, yyjson_get_sint(value));
                return {};
            case TypeKind::eArray: {
                if (!yyjson_is_arr(value)) return mismatch(path, type, value);
                const auto size = yyjson_arr_size(value);
                type.arrayResize(object, size);
                for (std::size_t i = 0; i < size; ++i) {
                    if (auto r = read(type.arrayAt(object, i), *type.element, yyjson_arr_get(value, i), std::format("{}[{}]", path, i)); !r)
                        return r;
                }
                return {};
            }
            case TypeKind::eStruct: {
                if (!yyjson_is_obj(value)) return mismatch(path, type, value);
                // What the JSON leaves out keeps its value; what it has that the type does not is skipped.
                for (const auto& field : type.fields) {
                    if (field.transient) continue;
                    auto* member = yyjson_obj_getn(value, field.name.data(), field.name.size());
                    if (!member) continue;
                    const std::string at = path.empty() ? field.name : path + "." + field.name;
                    if (auto r = read(field.address(object), *field.type, member, at); !r) return r;
                }
                return {};
            }
            }
            return {};
        }
    }

    std::string ToJson(const ConstRef value, const bool pretty)
    {
        if (!value.Valid()) return "null";
        yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
        yyjson_mut_doc_set_root(doc, write(doc, value.Address(), value.Type()));
        std::size_t length = 0;
        char* text = yyjson_mut_write(doc, pretty ? YYJSON_WRITE_PRETTY : 0, &length);
        std::string out = text ? std::string(text, length) : std::string("null");
        std::free(text);
        yyjson_mut_doc_free(doc);
        return out;
    }

    VoidResult FromJson(const Ref value, const std::string_view json)
    {
        if (!value.Valid())
            return std::unexpected(Error{ .code = ErrorCode::eInvalidArgument, .message = "FromJson into nothing" });
        yyjson_read_err error {};
        yyjson_doc* doc = yyjson_read_opts(const_cast<char*>(json.data()), json.size(), 0, nullptr, &error);
        if (!doc)
            return std::unexpected(Error{ .code = ErrorCode::eInvalidArgument, .message = std::format(
                "not JSON: {} at character {}", error.msg ? error.msg : "unreadable", error.pos) });
        auto result = read(value.Address(), value.Type(), yyjson_doc_get_root(doc), "");
        yyjson_doc_free(doc);
        return result;
    }

    VoidResult CopyFields(const ConstRef from, const Ref to)
    {
        if (!from.Valid() || !to.Valid() || from.Type().name != to.Type().name)
            return std::unexpected(Error{ .code = ErrorCode::eInvalidArgument, .message = "CopyFields between different types" });
        return FromJson(to, ToJson(from));
    }
}
