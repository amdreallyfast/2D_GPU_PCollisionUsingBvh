#pragma once

#include <memory>
#include <string>

#include "Include/Buffers/SSBOs/ParticleSsbo.h"

namespace ShaderControllers
{
    /*--------------------------------------------------------------------------------------------
    Description:
        This compute controller is responsible for having each particle count how many particles 
        are "nearby" (some multiple of the collision radius).  The ParticleRender shader will 
        then analyze this number as a rough approximation of "pressure" to determine the 
        particle color.  
        
        Lots of nearby particles -> red.  
        Very few nearby particles -> blue.  
        Somewhere in between -> green.
    Creator:    John Cox, 4/2017
    --------------------------------------------------------------------------------------------*/
    class CountNearbyParticles
    {
    public:
        CountNearbyParticles(const ParticleSsbo::CONST_SHARED_PTR particlesToAnalyze);
        ~CountNearbyParticles();

        void Count() const;

    private:
        unsigned int _totalParticleCount;
        unsigned int _computeProgramId;
    };
}
