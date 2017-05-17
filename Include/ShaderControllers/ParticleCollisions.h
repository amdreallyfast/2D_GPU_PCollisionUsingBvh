#pragma once

#include <memory>
#include <string>

#include "Include/Buffers/SSBOs/SsboBase.h"
#include "Include/Buffers/SSBOs/ParticleSsbo.h"
#include "Include/Buffers/SSBOs/BvhNodeSsbo.h"

namespace ShaderControllers
{
    /*--------------------------------------------------------------------------------------------
    Description:
        This compute controller is responsible for generating a BVH from a sorted Particle SSBO, 
        then having each particle check for possible collisions and resolve as necessary.
    Creator:    John Cox, 3/2017
    --------------------------------------------------------------------------------------------*/
    class ParticleCollisions
    {
    public:
        //??is the particle SSBO really necessary??
        ParticleCollisions(const ParticleSsbo::SharedConstPtr particleSsbo);
        ~ParticleCollisions();

        void DetectAndResolveWithoutProfiling() const;
        void DetectAndResolveWithProfiling() const;

    private:
        unsigned int _numLeaves;
        unsigned int _generateBinaryRadixTreeProgramId;
        unsigned int _generateBoundingVolumesProgramId;
        unsigned int _detectAndResolveCollisionsProgramId;

        // TODO: collision detection and resolution program ID

        // kept around for the sake of the *WithProfiling these are unique to this class and are needed for sorting
        BvhNodeSsbo::SharedPtr _bvhNodeSsbo;
    };
}
