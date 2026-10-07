//
// Created by radue on 2/23/2026.
//

/**
 * @file koralModelImport.h
 * @brief The model import module: reading a 3D model file into plain CPU-side data.
 *
 * glTF, FBX, OBJ and everything else Assimp reads. What comes back is arrays and a node hierarchy —
 * geometry, materials that *name* their textures, lights, transforms — and it stops there
 * deliberately: which vertex format that geometry is poured into, and which of those textures are
 * worth loading, are the project's decisions.
 *
 * @code
 * // CMakeLists.txt:  target_link_libraries(MyScene PRIVATE Koral::Koral Koral::koral-model-import)
 *
 * auto importer = kmdl::Importer::Load("models/sponza.gltf");
 * const auto scene = importer->LoadScene();
 * for (const auto& mesh : scene.meshes)
 *     meshes.push_back(kmdl::LoadMesh<MyMesh>(mesh));      // <koralModelMesh.h>
 * for (const auto& material : scene.materials)
 *     if (material.albedoTexturePath) textures.push_back(kimg::LoadImage(*material.albedoTexturePath));
 * @endcode
 *
 * Turning imported geometry into a mesh of a particular vertex format is @ref koralModelMesh.h, which
 * is where this module's dependency on koral-mesh lives. Loading the textures a material names is the
 * image import module's business, and this module does not link it — a project that wants both links
 * both.
 */

#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <kmath/matrix.h>

#include <buffer.h>
#include <context.h>
#include <resource.h>
#include <task.h>

/**
 * @brief Marks what crosses out of the module's library.
 *
 * Koral and its modules build with hidden visibility, so a class a consumer *calls* has to say so.
 * The module's own build defines KORAL_MODEL_IMPORT_EXPORTS to pick the export branch.
 */
#if defined(_WIN32)
#  if defined(KORAL_MODEL_IMPORT_EXPORTS)
#    define KMDL_API __declspec(dllexport)
#  else
#    define KMDL_API __declspec(dllimport)
#  endif
#else
#  define KMDL_API __attribute__((visibility("default")))
#endif

namespace kmdl
{
    /** @brief This module's identity, for another module that declares a dependency on it. */
    inline constexpr std::string_view ModuleId = "koral.model.import";
    inline constexpr std::uint32_t    ModuleVersion = 1;

    /** @brief An axis-aligned bounding box, in the space of whatever it bounds. */
    struct KMDL_API AABB {
        kor::Vec3 min;  ///< Lowest corner.
        kor::Vec3 max;  ///< Highest corner.
    };

    /**
     * @brief Loads models and scenes from disk.
     *
     * @code
     * auto importer = kmdl::Importer::Load("models/sponza.gltf");
     * const auto scene = importer->LoadScene();
     * for (const auto& mesh : scene.meshes)
     *     meshes.push_back(kmdl::LoadMesh<MyMesh>(mesh));   // koralModelMesh.h, with koral-mesh linked
     * @endcode
     *
     * A relative path is resolved against the asset search roots, so a project refers to its files
     * by name and not by where the build happened to put them. What comes back from a model is
     * plain CPU-side data, and it stops there: which vertex format that data is poured into is not
     * something the importer knows about — and this module does not link the mesh module to find out.
     * `<koralModelMesh.h>` is the bridge that turns one into the other, taking only the attributes the
     * target format asks for; it needs `koral-mesh` linked as well, and says so if it is not.
     *
     * *Images* are not here at all — reading them is the image import module's
     * (`koral-image-import`, `<koralImageImport.h>`), including the textures a material below names.
     */
    class KMDL_API Importer
    {
    public:
        /**
         * @brief A material as the file declares it: factors, and paths to its textures.
         *
         * Texture paths are resolved but not loaded — pass them to kimg::LoadImage for the ones
         * the renderer actually uses. Which fields are filled depends on the format: glTF populates the
         * physically-based set (base colour, roughness, metallic), older formats the classic one
         * (diffuse, specular, shininess).
         */
        struct KMDL_API Material {
            std::string name;

            float alphaCutoff = 1.f;
            kor::Vec4 baseColorFactor = kor::Vec4(1.0f);
            kor::Vec4 emissiveFactor = kor::Vec4(1.0f);
            float roughness = 1.0f;
            float metallic = 1.0f;
            int doubleSided = false;

            std::optional<std::filesystem::path> albedoTexturePath;
            std::optional<std::filesystem::path> normalTexturePath;
            std::optional<std::filesystem::path> roughnessTexturePath;
            std::optional<std::filesystem::path> metallicTexturePath;
            std::optional<std::filesystem::path> ambientOcclusionTexturePath;
            std::optional<std::filesystem::path> emissiveTexturePath;

            std::optional<std::filesystem::path> ambientTexturePath;
            std::optional<std::filesystem::path> diffuseTexturePath;
            std::optional<std::filesystem::path> specularTexturePath;
            std::optional<std::filesystem::path> shininessTexturePath;

            std::optional<std::filesystem::path> displacementTexturePath;
            std::optional<std::filesystem::path> alphaTexturePath;
            std::optional<std::filesystem::path> heightTexturePath;
        };

        /**
         * @brief One mesh's geometry as CPU-side arrays, before it reaches the GPU.
         *
         * Only positions are guaranteed. Everything else is present when the file supplied it, so
         * check before reading — LoadMesh does that for you and fills any attribute the file lacks
         * with zeroes.
         */
        struct KMDL_API Mesh {
            std::string name;                                                       ///< Name in the file.

            std::vector<kor::Vec3> positions;                                       ///< Vertex positions. Always present.
            std::optional<std::vector<kor::Vec3>> normals;                          ///< Surface normals, if the file has them.
            std::optional<std::vector<kor::Vec3>> tangents;                         ///< Tangents, for normal mapping.
            std::optional<std::vector<kor::Vec3>> bitangents;                       ///< Bitangents.
            std::unordered_map<kor::u32, std::vector<kor::Vec3>> vertexColors;      ///< Vertex colour sets, keyed by channel.
            std::unordered_map<kor::u32, std::vector<kor::Vec2>> vertexUVs;         ///< Texture coordinate sets, keyed by channel.
            std::optional<std::pair<std::vector<kor::Vec4>, std::vector<kor::UVec4>>> boneData;  ///< Skinning weights and the bone ids they apply to.
            std::optional<std::vector<kor::u32>> indices;                           ///< Triangle indices, if the mesh is indexed.
        };

        /** @brief A node of the scene graph: a transform, the meshes it draws, and its children. */
        struct KMDL_API Node {
            kor::i32 id = -1;                       ///< Index of this node, or -1 if it has none.
            std::string name;                       ///< Name in the file.
            std::vector<kor::u32> childIndices;     ///< Indices into Scene::nodes.

            std::vector<kor::u32> meshIndices;      ///< Indices into Scene::meshes.
            std::vector<kor::u32> materialIndices;  ///< Indices into Scene::materials, one per mesh above.

            kor::Vec3 position = kor::Vec3(0.f, 0.f, 0.f);  ///< Translation relative to the parent.
            kor::Vec3 rotation = kor::Vec3(0.f, 0.f, 0.f);  ///< Euler rotation relative to the parent, in **degrees** (X, Y, Z).
            kor::Vec3 scale = kor::Vec3(1.f, 1.f, 1.f);     ///< Scale relative to the parent.
            AABB aabb;                                      ///< Bounds of the meshes under this node.
        };

        /** @brief A light the file declares. */
        struct KMDL_API Light {
            /** @brief What shape the light emits in. */
            enum class Type : std::uint8_t {
                ePoint,         ///< Emits in every direction from a point.
                eDirectional,   ///< Parallel rays from infinitely far away — the sun.
                eSpot           ///< A cone from a point, along a direction.
            };

            std::string name;                                   ///< Name in the file.
            Type      type      = Type::ePoint;                 ///< What shape it emits in.
            kor::Vec3 position  = kor::Vec3(0.0f);              ///< World-space position. Unused by directional lights.
            kor::Vec3 direction = kor::Vec3(0.0f, -1.0f, 0.0f); ///< World-space direction. Used by spot and directional lights.
            kor::Vec3 color     = kor::Vec3(1.0f);              ///< Emitted colour, without the intensity in it.
            /**
             * Brightness, in the file's own units — for glTF, candela for point and spot lights and
             * lux for directional ones. 0 is a light the file switched off.
             */
            float     intensity = 1.0f;
            float     range          = 0.0f;                    ///< Distance at which it stops contributing; 0 means no limit.
            float     innerConeAngle = 0.0f;                    ///< Spot: the fully lit cone, in radians.
            float     outerConeAngle = 0.0f;                    ///< Spot: where the falloff reaches zero, in radians.
        };

        /** @brief Everything a model file contains. */
        struct KMDL_API Scene {
            std::vector<Mesh> meshes;           ///< Geometry, referenced by Node::meshIndices.
            std::vector<Material> materials;    ///< Materials, referenced by Node::materialIndices.
            std::vector<Node> nodes;            ///< The scene graph. Node 0 is the root.
            std::vector<Light> lights;          ///< Lights the file declares.
        };

        virtual ~Importer() = default;

        /**
         * @brief Opens a model file.
         * @param relativePath The model, resolved against the asset search roots.
         * @return An importer for it, or nullptr if it cannot be read.
         *
         * The model's material textures are then looked for beside the model itself, and failing
         * that, across the same asset roots.
         */
        static std::unique_ptr<Importer> Load(const std::filesystem::path& relativePath);

        /** @brief The names of every mesh in the file, for GetMesh. */
        virtual std::vector<std::string> GetMeshNames() = 0;

        /** @brief The names of every material in the file, for GetMaterial. */
        virtual std::vector<std::string> GetMaterialNames() = 0;

        /** @brief One mesh's geometry by name. */
        virtual Mesh GetMesh(const std::string& name) = 0;

        /** @brief One material by name. */
        virtual Material GetMaterial(const std::string& name) = 0;

        /** @brief Everything in the file at once: meshes, materials, the node hierarchy and lights. */
        virtual Scene LoadScene() = 0;

        /**
         * @brief The skeleton's bind-pose transforms, for a skinned model.
         * @return One matrix per bone, or a message when the file carries no skeleton.
         */
        virtual std::expected<std::vector<kor::Mat4>, std::string> GetBoneTransformationMatrices() = 0;

    protected:
        std::filesystem::path _path;
    };
} // namespace kmdl
