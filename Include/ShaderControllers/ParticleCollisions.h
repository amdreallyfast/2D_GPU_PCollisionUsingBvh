#pragma once

#include <memory>
#include <string>

#include "Include/Buffers/SSBOs/BvhNodeSsbo.h"
#include "Include/Buffers/SSBOs/ParticlePropertiesSsbo.h"
#include "Include/Buffers/SSBOs/ParticleSortingDataSsbo.h"
#include "Include/Buffers/SSBOs/ParticleSsbo.h"
#include "Include/Buffers/SSBOs/PrefixSumSsbo.h"
#include "Include/Buffers/SSBOs/ParticlePotentialCollisionsSsbo.h"
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
        ParticleCollisions(const ParticleSsbo::SharedConstPtr particleSsbo, const ParticlePropertiesSsbo::SharedConstPtr particlePropertiesSsbo);
        ~ParticleCollisions();

        void DetectAndResolve(bool withProfiling) const;
        const PolygonSsbo &BvhVerticesSsbo() const;

    private:
        unsigned int _numParticles;

        // lots of programs for sorting
        unsigned int _programIdCopyParticlesToCopyBuffer;
        unsigned int _programIdGenerateSortingData;
        unsigned int _programIdClearWorkGroupSums;
        unsigned int _programIdGetBitForPrefixScan;
        unsigned int _programIdPrefixScanOverAllData;
        unsigned int _programIdPrefixScanOverWorkGroupSums;
        unsigned int _programIdSortSortingDataWithPrefixSums;
        unsigned int _programIdSortParticles;

        // and a few more for collisions
        unsigned int _programIdGuaranteeSortingDataUniqueness;
        unsigned int _programIdGenerateLeafNodeBoundingBoxes;
        unsigned int _programIdGenerateBinaryRadixTree;
        unsigned int _programIdMergeBoundingVolumes;

        // all that for the coup de grace
        unsigned int _programIdDetectCollisions;
        //unsigned int _programIdResolveCollisions;

        // ??should this shader controller be split into more manageable parts? it's a lot of programs...??
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
        void AssembleProgramGenerateLeafNodeBoundingBoxes();
        void AssembleProgramGenerateBinaryRadixTree();
        void AssembleProgramMergeBoundingVolumes();
        void AssembleProgramDetectCollisions();

        void SortParticlesWithoutProfiling(unsigned int numWorkGroupsX, unsigned int numWorkGroupsXPrefixScan) const;
        void SortParticlesWithProfiling(unsigned int numWorkGroupsX, unsigned int numWorkGroupsXPrefixScan) const;

        void GenerateBvhWithoutProfiling(unsigned int numWorkGroupsX) const;
        void GenerateBvhWithProfiling(unsigned int numWorkGroupsX) const;

        void DetectAndResolveCollisionsWithoutProfiling(unsigned int numWorkGroupsX) const;
        void DetectAndResolveCollisionsWithProfiling(unsigned int numWorkGroupsX) const;

        // the "without profiling" and "with profiling" go through these same steps
        void PrepareToSortParticles(unsigned int numWorkGroupsX) const;
        void PrepareForPrefixScan(unsigned int bitNumber, unsigned int sortingDataReadOffset) const;
        void PrefixScanOverParticleSortingData(unsigned int numWorkGroupsX) const;
        void SortSortingDataWithPrefixScan(unsigned int numWorkGroupsX, unsigned int bitNumber, unsigned int sortingDataReadOffset, unsigned int sortingDataWriteOffset) const;
        void SortParticlesWithSortedData(unsigned int numWorkGroupsX, unsigned int sortingDataReadOffset) const;
        void PrepareForBinaryTree(unsigned int numWorkGroupsX) const;
        void GenerateBinaryRadixTree(unsigned int numWorkGroupsX) const;
        void MergeNodesIntoBvh(unsigned int numWorkGroupsX) const;
        void DetectCollisions(unsigned int numWorkGroupsX) const;
        void ResolveCollisions(unsigned int numWorkGroupsX) const;





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
        // GuaranteeSortingDataUniqueness.comp

        // TODO: verify uniqueness!

        // memory barrier
        // GenerateLeafNodeBoundingBoxes.comp
        // GenerateBinaryRadixTree.comp
        // memory barrier
        // MergeBoundingVolumes.comp
        // memory barrier



        ParticleSortingDataSsbo _particleSortingDataSsbo;
        PrefixSumSsbo _prefixSumSsbo;
        BvhNodeSsbo _bvhNodeSsbo;
        ParticlePotentialCollisionsSsbo _particlePotentialCollisionsSsbo;
        PolygonSsbo _bvhGeometrySsbo;

        // used for verifying that particle sorting is working
        const ParticleSsbo::SharedConstPtr _originalParticleSsbo; 
    };
}
