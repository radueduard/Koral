//
// Created by eduard on 10.03.2026.
//

#pragma once

namespace kor
{
    template<typename Derived>
    concept MeshType = requires {
        Derived::DefineMesh();
        Derived::VertexBindingDescription();
        Derived::VertexAttributeDescription();
    };
}