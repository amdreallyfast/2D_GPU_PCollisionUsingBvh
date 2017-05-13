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
        This compute controller is responsible for generative a BVH from a sorted Particle SSBO.
    Creator:    John Cox, 3/2017
    --------------------------------------------------------------------------------------------*/
    class GenerateBoundingVolumeHierarchy
    {
    public:
        //??is the particle SSBO really necessary??
        GenerateBoundingVolumeHierarchy(const ParticleSsbo::CONST_SHARED_PTR particleSsbo, const BvhNodeSsbo::SharedConstPtr bvhSsbo);
        ~GenerateBoundingVolumeHierarchy();

        void GenerateBvhWithoutProfiling() const;
        void GenerateBvhWithProfiling() const;

    private:
        unsigned int _numLeaves;
        unsigned int _generateBinaryRadixTreeProgramId;
        unsigned int _generateBoundingBoxesProgramId;

        //// kept around for the sake of the *WithProfiling these are unique to this class and are needed for sorting
        //BvhNodeSsbo::SharedPtr _bvhNodeSsbo;
    };
}
