#pragma once

#include <memory>
#include <string>

#include "Include/Buffers/SSBOs/SsboBase.h"
#include "Include/Buffers/SSBOs/PrefixSumSsbo.h"
#include "Include/Buffers/SSBOs/IntermediateDataSsbo.h"
#include "Include/Buffers/SSBOs/ParticleSsbo.h"
#include "Include/Buffers/SSBOs/ParticleCopySsbo.h"

namespace ShaderControllers
{
    /*--------------------------------------------------------------------------------------------
    Description:
        This compute controller is responsible for performing a parallel Radix sort of an SSBO
        according to a structure-specific element.  For example, suppose there is a Particle
        structure with position, velocity, mass, etc.  If I want to sort the particles in 3D 
        space according to a Z-order curve, then I will want to sort the particles by Morton 
        codes.

        Sorting by parallel Radix sort requires going over all the bits in the data to be sorted
        one by one, each time:
            (1) Getting a single bit value
            (2) Performing a parallel prefix scan by work group
            (3) Performing a parallel prefix scan over all the work group sums
            (4) Sorting the data according to the prefix sums.

        If I want to sort the original structures, then I can't just sort by some integer.  I 
        need to associate the data that is being sorted with the original structure.  Enter the
        IntermediateData structure, which stores a uint (data to sort over, such as a
        3D-position-derived Morton code) and the index into the buffer that the structure 
        originally came from.

        This class handles the multiple compute shaders that need to be called at each step of 
        the sorting process.  The sorting process requires knowing how big the original buffer 
        is and exactly which buffer is being sorted, so an instance of this class will only be 
        useful for a single OriginalDataSsbo.  Compute shaders are not as flexible as CPU-bound 
        shaders, so you have to hold their hand, and the consequence is high coupling.

        The benefit is that it can sort 1,000,000 Particle structures in ~127 milliseconds on my 
        GeForce GTX 560M.  In my GpuRadixSort demo I claimed that the sort happened in <5ms, but 
        I wasn't properly waiting for the GPU to finish before I used std::chrono to record the 
        end time.  This is not acceptable for real time (somewhere north of 30fps), but it isn't 
        bad for a 2011-era laptop GPU.  Fifty thousand particles (50,000) is better, running at 
        ~50fps (with other shaders alongside it), and 100,000 drops fps down to ~30fps.
    Creator:    John Cox, 3/2017
    --------------------------------------------------------------------------------------------*/
    class ParallelSort
    {
    public:
        ParallelSort(const ParticleSsbo::SharedConstPtr dataToSort);
        ~ParallelSort();

        void SortWithoutProfiling() const;
        void SortWithProfiling() const;

    private:
        unsigned int _particleDataToIntermediateDataProgramId;
        unsigned int _getBitForPrefixScansProgramId;
        unsigned int _PrefixScanProgramId;
        unsigned int _sortIntermediateDataProgramId;
        unsigned int _sortParticlesProgramId;

        // these are unique to this class and are needed for sorting
        ParticleCopySsbo::SharedPtr _particleCopySsbo;
        IntermediateDataSsbo::SharedPtr _intermediateDataSsbo;
        PrefixSumSsbo::SharedPtr _prefixSumSsbo;

        // need to keep this around until the end of Sort() in order to copy the sorted data 
        // back to the original buffer
        ParticleSsbo::SharedConstPtr _particleSsbo;

    };
}
