#pragma once

#include "Include/Buffers/SSBOs/ParticleSsbo.h"

namespace ShaderControllers
{
    /*--------------------------------------------------------------------------------------------
    Description:
        Encapsulates the rendering of particles.

    Creator:    John Cox, 4/2017
    --------------------------------------------------------------------------------------------*/
    class RenderParticles
    {
    public:
        RenderParticles(ParticleSsbo::SHARED_PTR &particleSsboToRender);
        ~RenderParticles();

        void Render() const;

    private:
        unsigned int _renderProgramId;

        // these will be pulled from the SSBO that is passed on construction
        unsigned int _vaoId;
        unsigned int _drawStyle;
        unsigned int _numVertices;
    };
}
