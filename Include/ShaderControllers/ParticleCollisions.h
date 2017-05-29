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

        // lots of programs
        // TODO: init to 0 in constructor
        unsigned int _programIdCopyParticlesToCopyBuffer;
        unsigned int _programIdGenerateSortingData;
        unsigned int _programIdClearWorkGroupSums;
        unsigned int _programIdGetBitForPrefixScan;
        unsigned int _programIdPrefixScanOverAllData;
        unsigned int _programIdPrefixScanOverWorkGroupSums;
        unsigned int _programIdSortSortingDataWithPrefixSums;
        unsigned int _programIdSortParticles;
        unsigned int _programIdGuaranteeSortingDataUniqueness;
        
        void AssembleProgramHeader(const std::string &shaderKey) const;
        void AssembleProgramCopyParticlesToCopyBuffer();
        void AssembleProgramGenerateSortingData();
        void AssembleProgramClearWorkGroupSums();
        void AssembleProgramGetBitForPrefixScan();
        void AssembleProgramPrefixScanOverAllData();
        void AssembleProgramPrefixScanOverWorkGroupSums();
        void AssembleProgramSortSortingDataWithPrefixSums();
        void AssembleProgramSortParticles();
        void AssembleProgramGuaranteeSortingDataUniqueness();



        // CopyParticlesToCopyBuffer.comp
        // GenerateSortingData.comp
        // memory barrier
        // loop
        // - ClearWorkGroupSums.comp
        // - GetBitForPrefixScan.comp
        // - memory barrier
        // - PrefixScanOverAllData.comp
        // - memory barrier
        // - PrefixScanOverWorkGroupSums.comp
        // - memory barrier
        // - SortSortingDataWithPrefixSums.comp
        // - memory barrier
        // SortParticles.comp
        // GuaranteeMortonCodeUniqueness.comp


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
