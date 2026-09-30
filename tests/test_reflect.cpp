// Unit tests for reflection and JSON (reflect.h): descriptions, references, saving and loading.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "reflect.h"

namespace game {
    enum class Shape : int { eSphere, eBox, eCapsule };
    KORAL_REFLECT_ENUM(Shape, eSphere, eBox, eCapsule)

    struct Material {
        glm::vec4 albedo { 1.f };
        float roughness = 0.5f;
    };
    KORAL_REFLECT(Material, albedo, roughness)

    struct Body {
        std::string name = "body";
        bool dynamic = true;
        std::int32_t layer = 0;
        std::uint64_t id = 0;
        double mass = 1.0;
        glm::vec3 position { 0.f };
        glm::quat rotation { 1.f, 0.f, 0.f, 0.f };
        glm::ivec2 cell { 0 };
        glm::mat4 transform { 1.f };
        Shape shape = Shape::eSphere;
        Material material;
        std::vector<Material> layers;
        std::vector<std::string> tags;
        float cachedSpeed = 0.f;   // derived: not saved
    };

    inline void KoralReflect(kor::TypeBuilder<Body>& type) {
        type.Name("Body");
        type.Field("name", &Body::name);
        type.Field("dynamic", &Body::dynamic);
        type.Field("layer", &Body::layer).Range(0, 31).Tooltip("Collision layer");
        type.Field("id", &Body::id).ReadOnly();
        type.Field("mass", &Body::mass);
        type.Field("position", &Body::position);
        type.Field("rotation", &Body::rotation);
        type.Field("cell", &Body::cell);
        type.Field("transform", &Body::transform);
        type.Field("shape", &Body::shape);
        type.Field("material", &Body::material);
        type.Field("layers", &Body::layers);
        type.Field("tags", &Body::tags);
        type.Field("cachedSpeed", &Body::cachedSpeed).Transient();
    }

    // Private fields, through the friend declaration.
    class Counter {
    public:
        void Bump() { ++_count; }
        [[nodiscard]] int Count() const { return _count; }
    private:
        KORAL_REFLECT_FRIEND(Counter);
        int _count = 0;
    };
    KORAL_REFLECT(Counter, _count)
}

TEST(Reflect, DescribesAStructsFieldsWithTheirTypesAndAttributes) {
    const auto& type = kor::TypeOf<game::Body>();
    EXPECT_EQ(type.name, "Body");
    EXPECT_EQ(type.kind, kor::TypeKind::eStruct);
    ASSERT_EQ(type.fields.size(), 14u);
    EXPECT_EQ(type.Field("position")->type->kind, kor::TypeKind::eVec3);
    EXPECT_EQ(type.Field("rotation")->type->kind, kor::TypeKind::eQuat);
    EXPECT_EQ(type.Field("shape")->type->kind, kor::TypeKind::eEnum);
    EXPECT_EQ(type.Field("shape")->type->name, "Shape");
    EXPECT_EQ(type.Field("layers")->type->kind, kor::TypeKind::eArray);
    EXPECT_EQ(type.Field("layers")->type->element->name, "Material");
    EXPECT_EQ(type.Field("layer")->max, 31.0);
    EXPECT_EQ(type.Field("layer")->tooltip, "Collision layer");
    EXPECT_TRUE(type.Field("id")->readOnly);
    EXPECT_TRUE(type.Field("cachedSpeed")->transient);
    EXPECT_EQ(type.Field("missing"), nullptr);
    EXPECT_EQ(&kor::TypeOf<game::Body>(), &type) << "described once";
}

TEST(Reflect, FindsATypeByName) {
    (void)kor::TypeOf<game::Body>();
    EXPECT_EQ(kor::FindType("Body"), &kor::TypeOf<game::Body>());
    EXPECT_EQ(kor::FindType("Material"), &kor::TypeOf<game::Material>());
    EXPECT_EQ(kor::FindType("NoSuchType"), nullptr);
}

TEST(Reflect, AReferenceReachesFieldsAndElementsOfAnyObject) {
    game::Body body;
    body.layers.resize(2);
    kor::Ref ref(body);
    ref.Field("mass").As<double>() = 4.0;
    ref.Field("material").Field("roughness").As<float>() = 0.9f;
    ref.Field("layers").At(1).Field("albedo").As<glm::vec4>() = glm::vec4(0.f, 1.f, 0.f, 1.f);
    EXPECT_EQ(body.mass, 4.0);
    EXPECT_EQ(body.material.roughness, 0.9f);
    EXPECT_EQ(body.layers[1].albedo, glm::vec4(0.f, 1.f, 0.f, 1.f));
    EXPECT_EQ(ref.Field("layers").Size(), 2u);
    ref.Field("tags").Resize(3);
    EXPECT_EQ(body.tags.size(), 3u);
    EXPECT_FALSE(ref.Field("nope").Valid());
    EXPECT_FALSE(ref.Field("layers").At(5).Valid());
    EXPECT_THROW((void)ref.Field("mass").As<float>(), std::bad_cast);
}

TEST(Reflect, ReachesPrivateFieldsThroughTheFriendDeclaration) {
    game::Counter counter;
    counter.Bump();
    EXPECT_EQ(kor::ConstRef(counter).Field("_count").As<int>(), 1);
    ASSERT_TRUE(kor::FromJson(counter, R"({"_count": 41})"));
    counter.Bump();
    EXPECT_EQ(counter.Count(), 42);
}

TEST(Reflect, SavesAndLoadsEveryKindOfValueThroughJson) {
    game::Body body;
    body.name = "crate";
    body.dynamic = false;
    body.layer = 3;
    body.id = 12345678901234ull;
    body.mass = 2.5;
    body.position = {1.f, 2.f, 3.f};
    body.rotation = glm::quat(0.f, 1.f, 0.f, 0.f);
    body.cell = {-4, 7};
    body.transform[3] = glm::vec4(5.f, 6.f, 7.f, 1.f);
    body.shape = game::Shape::eCapsule;
    body.material.roughness = 0.25f;
    body.layers = { {glm::vec4(0.5f), 0.1f}, {glm::vec4(0.2f), 0.9f} };
    body.tags = {"crate", "wood"};
    body.cachedSpeed = 99.f;

    const std::string json = kor::ToJson(body);
    EXPECT_NE(json.find("\"shape\":\"eCapsule\""), std::string::npos) << json;
    EXPECT_EQ(json.find("cachedSpeed"), std::string::npos) << "transient fields are not saved";

    game::Body loaded;
    const auto result = kor::FromJson(loaded, json);
    ASSERT_TRUE(result) << result.error().message;
    EXPECT_EQ(loaded.name, "crate");
    EXPECT_FALSE(loaded.dynamic);
    EXPECT_EQ(loaded.layer, 3);
    EXPECT_EQ(loaded.id, 12345678901234ull);
    EXPECT_EQ(loaded.mass, 2.5);
    EXPECT_EQ(loaded.position, glm::vec3(1.f, 2.f, 3.f));
    EXPECT_EQ(loaded.rotation, glm::quat(0.f, 1.f, 0.f, 0.f));
    EXPECT_EQ(loaded.cell, glm::ivec2(-4, 7));
    EXPECT_EQ(loaded.transform[3], glm::vec4(5.f, 6.f, 7.f, 1.f));
    EXPECT_EQ(loaded.shape, game::Shape::eCapsule);
    EXPECT_EQ(loaded.material.roughness, 0.25f);
    ASSERT_EQ(loaded.layers.size(), 2u);
    EXPECT_EQ(loaded.layers[1].roughness, 0.9f);
    EXPECT_EQ(loaded.tags, (std::vector<std::string>{"crate", "wood"}));
    EXPECT_EQ(loaded.cachedSpeed, 0.f);
}

// A file from before a field existed, or after one was removed, still loads.
TEST(Reflect, LoadingKeepsWhatTheJsonLeavesOutAndSkipsWhatTheTypeLacks) {
    game::Body body;
    body.mass = 7.0;
    const auto result = kor::FromJson(body, R"({"name": "old", "retired": 1, "material": {"roughness": 1}})");
    ASSERT_TRUE(result) << result.error().message;
    EXPECT_EQ(body.name, "old");
    EXPECT_EQ(body.mass, 7.0) << "not in the JSON: unchanged";
    EXPECT_EQ(body.material.roughness, 1.f);
    EXPECT_EQ(body.material.albedo, glm::vec4(1.f));
}

TEST(Reflect, AValueOfTheWrongKindIsReportedByItsPath) {
    game::Body body;
    const auto wrong = kor::FromJson(body, R"({"layers": [{"roughness": 0.1}, {"albedo": [1, 2]}]})");
    ASSERT_FALSE(wrong);
    EXPECT_NE(wrong.error().message.find("layers[1].albedo"), std::string::npos) << wrong.error().message;

    const auto enumName = kor::FromJson(body, R"({"shape": "eCone"})");
    ASSERT_FALSE(enumName);
    EXPECT_NE(enumName.error().message.find("eCone"), std::string::npos);

    EXPECT_FALSE(kor::FromJson(body, "{not json"));
}

TEST(Reflect, CopiesEveryFieldOfOneObjectIntoAnother) {
    game::Material from{ glm::vec4(0.3f), 0.7f }, to;
    ASSERT_TRUE(kor::CopyFields(from, to));
    EXPECT_EQ(to.albedo, glm::vec4(0.3f));
    EXPECT_EQ(to.roughness, 0.7f);
    game::Body body;
    EXPECT_FALSE(kor::CopyFields(from, body)) << "only between objects of one type";
}

TEST(Reflect, MakesADefaultObjectOfAType) {
    const auto made = kor::TypeOf<game::Material>().make();
    ASSERT_NE(made, nullptr);
    EXPECT_EQ(static_cast<game::Material*>(made.get())->roughness, 0.5f);
}
