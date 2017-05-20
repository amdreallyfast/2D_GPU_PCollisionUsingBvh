#pragma once

#include <memory>
#include <string>

#include "Include/Buffers/SSBOs/SsboBase.h"
#include "Include/Buffers/SSBOs/ParticleSsbo.h"
#include "Include/Buffers/SSBOs/BvhNodeSsbo.h"
#include "Include/Buffers/SSBOs/PolygonSsbo.h"

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

        void DetectAndResolveWithoutProfiling(unsigned int numActiveParticles) const;
        void DetectAndResolveWithProfiling(unsigned int numActiveParticles) const;
        const PolygonSsbo::SharedConstPtr BvhVerticesSsbo() const;

    private:
        unsigned int _numLeaves;
        unsigned int _populateLeavesWithDataProgramId;
        unsigned int _generateBinaryRadixTreeProgramId;
        unsigned int _generateBoundingVolumesProgramId;
        unsigned int _generateVerticesProgramId;
        unsigned int _detectCollisionsProgramId;
        unsigned int _resolveCollisionsProgramId;

        BvhNodeSsbo::SharedPtr _bvhNodeSsbo;
        PolygonSsbo::SharedPtr _bvhGeometrySsbo;


    };
}
