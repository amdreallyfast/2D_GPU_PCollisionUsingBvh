#pragma once

#include "Include/Buffers/SSBOs/PolygonSsbo.h"

namespace ShaderControllers
{
    /*--------------------------------------------------------------------------------------------
    Description:
        Encapsulates the rendering of geometry.

    Creator:    John Cox, 5/2017
    --------------------------------------------------------------------------------------------*/
    class RenderGeometry
    {
    public:
        RenderGeometry();
        ~RenderGeometry();

        void Render(const PolygonSsbo::SHARED_PTR &ssboToRender) const;

    private:
        unsigned int _renderProgramId;
    };
}
