//
// Created by eduard on 10.03.2026.
//

export module gfx:meshType;

namespace gfx
{
    export template<typename Derived>
    concept MeshType = requires {
        Derived::DefineMesh();
        Derived::VertexBindingDescription();
        Derived::VertexAttributeDescription();
    };
}