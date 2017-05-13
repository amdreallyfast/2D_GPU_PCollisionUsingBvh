#include "Include/ShaderControllers/ParallelSort.h"

#include "Shaders/ShaderStorage.h"
#include "ThirdParty/glload/include/glload/gl_4_4.h"

#include "Include/Buffers/SSBOs/PrefixSumSsbo.h"
#include "Include/Particles/Particle.h"     // for copying data back and verifying 

#include "Shaders/ShaderHeaders/ComputeShaderWorkGroupSizes.comp"
#include "Shaders/ShaderHeaders/CrossShaderUniformLocations.comp"

#include <iostream>
#include <fstream>

#include <chrono>
#include <iostream>
using std::cout;
using std::endl;


namespace ShaderControllers
{
    /*--------------------------------------------------------------------------------------------
    Description:
        Generates multiple compute shaders for the different stages of the parallel sort, and
        allocates various buffers for the sorting.  Buffer sizes are highly dependent on the 
        size of the original data.  They are expected to remain constant after class creation.

        Note: The argument is a copy, not a reference.  A const pointer in the land of shared
        pointers is a different type than a non-const pointer (see the definition of the type)
        and there is no conversion from a shared_ptr<class> reference to a
        shared_ptr<const class> reference, but there is a copy constructor for it.  I want to
        use shared pointers and I want to use const pointers, so I have to use the copy
        constructor.  Shared pointer construction is cheap though, so its fine.

        Also Note: This SSBO must be passed into the constructor because part of the sorting
        algorithm is copying the contents of the ParticleSsbo into a copy SSBO of the same
        size.  That size must be determined in this constructor (I don't want to create the copy
        SSBO anew on every Sort() call).  A simple unsigned integer could be passed in instead,
        but the size of the particle buffer is specific to the SSBO that needs to be sorted.
        Besides, the sorted particle data needs to be copied into the copy SSBO and then put
        back into the original SSBO in its sorted order (this is the final stage of Sort()).

        So the options are either
        (1) Constructor is blank and particle SSBO is passed into the Sort() method, then the
        copy SSBO is created anew on every Sort() call (don't know if it's the same
        SSBO).  This is a performance concern.
        (2) Constructor takes the particle SSBO and Sort() takes no arguments.  The copy SSBO
        is created on startup, but a copy of the SSBO must be kept around so that the
        original data can be copied at the end of Sort().  This increases coupling between a
        ParallelSort object and the SSBO that is being sorted, but it is not a performance
        concern.

        I'll take option (2).
    Parameters:
        dataToSort  See Description.
    Returns:    None
    Creator:    John Cox, 3/2017
    --------------------------------------------------------------------------------------------*/
    ParallelSort::ParallelSort(const ParticleSsbo::CONST_SHARED_PTR dataToSort) :
        _particleDataToIntermediateDataProgramId(0),
        _getBitForPrefixScansProgramId(0),
        _parallelPrefixScanProgramId(0),
        _sortIntermediateDataProgramId(0),
        _sortParticlesProgramId(0),
        _particleCopySsbo(nullptr),
        _intermediateDataSsbo(nullptr),
        _prefixSumSsbo(nullptr),
        _particleSsbo(dataToSort)
    {
        ShaderStorage &shaderStorageRef = ShaderStorage::GetInstance();
        std::string shaderKey;

        // take a data structure that needs to be sorted by a value (must be unsigned int for 
        // radix sort to work) and put it into an intermediate structure that has the value and 
        // the index of the original data structure in the ParticleBuffer
        shaderKey = "particle data to intermediate data";
        shaderStorageRef.NewCompositeShader(shaderKey);
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/ShaderHeaders/Version.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/ShaderHeaders/SsboBufferBindings.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/ShaderHeaders/CrossShaderUniformLocations.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ParticleBuffer.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/ShaderHeaders/ComputeShaderWorkGroupSizes.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ParallelSort/IntermediateSortBuffers.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ParticleRegionBoundaries.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/PositionToMortonCode.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ParallelSort/ParticleDataToIntermediateData.comp");
        shaderStorageRef.CompileCompositeShader(shaderKey, GL_COMPUTE_SHADER);
        shaderStorageRef.LinkShader(shaderKey);
        _particleDataToIntermediateDataProgramId = shaderStorageRef.GetShaderProgram(shaderKey);

        // on each loop in Sort(), pluck out a single bit and add it to the 
        // PrefixScanBuffer::PrefixSumsPerWorkGroup array
        shaderKey = "get bit for prefix sums";
        shaderStorageRef.NewCompositeShader(shaderKey);
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/ShaderHeaders/Version.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/ShaderHeaders/SsboBufferBindings.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/ShaderHeaders/CrossShaderUniformLocations.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/ShaderHeaders/ComputeShaderWorkGroupSizes.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ParallelSort/IntermediateSortBuffers.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ParallelSort/PrefixScanBuffer.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ParallelSort/GetBitForPrefixScan.comp");
        shaderStorageRef.CompileCompositeShader(shaderKey, GL_COMPUTE_SHADER);
        shaderStorageRef.LinkShader(shaderKey);
        _getBitForPrefixScansProgramId = shaderStorageRef.GetShaderProgram(shaderKey);

        // run the prefix scan over PrefixScanBuffer::PrefixSumsPerWorkGroup, and after that run 
        // the scan again over PrefixScanBuffer::PrefixSumsOfWorkGroupSums
        shaderKey = "parallel prefix scan";
        shaderStorageRef.NewCompositeShader(shaderKey);
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/ShaderHeaders/Version.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/ShaderHeaders/CrossShaderUniformLocations.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/ShaderHeaders/SsboBufferBindings.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/ShaderHeaders/ComputeShaderWorkGroupSizes.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ParallelSort/PrefixScanBuffer.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ParallelSort/ParallelPrefixScan.comp");
        shaderStorageRef.CompileCompositeShader(shaderKey, GL_COMPUTE_SHADER);
        shaderStorageRef.LinkShader(shaderKey);
        _parallelPrefixScanProgramId = shaderStorageRef.GetShaderProgram(shaderKey);

        // and finally sort the "read" array from IntermediateSortBuffers into the "write" array
        shaderKey = "sort intermediate data";
        shaderStorageRef.NewCompositeShader(shaderKey);
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/ShaderHeaders/Version.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/ShaderHeaders/CrossShaderUniformLocations.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/ShaderHeaders/SsboBufferBindings.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/ShaderHeaders/ComputeShaderWorkGroupSizes.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ParallelSort/PrefixScanBuffer.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ParallelSort/IntermediateSortBuffers.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ParallelSort/SortIntermediateData.comp");
        shaderStorageRef.CompileCompositeShader(shaderKey, GL_COMPUTE_SHADER);
        shaderStorageRef.LinkShader(shaderKey);
        _sortIntermediateDataProgramId = shaderStorageRef.GetShaderProgram(shaderKey);

        // after the loop, sort the original data according to the sorted intermediate data
        shaderKey = "sort original data";
        shaderStorageRef.NewCompositeShader(shaderKey);
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/ShaderHeaders/Version.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/ShaderHeaders/SsboBufferBindings.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/ShaderHeaders/CrossShaderUniformLocations.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ParticleBuffer.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/ShaderHeaders/ComputeShaderWorkGroupSizes.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ParallelSort/IntermediateSortBuffers.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ParallelSort/SortParticleData.comp");
        shaderStorageRef.CompileCompositeShader(shaderKey, GL_COMPUTE_SHADER);
        shaderStorageRef.LinkShader(shaderKey);
        _sortParticlesProgramId = shaderStorageRef.GetShaderProgram(shaderKey);

        // the size of the ParticleBuffer is needed by these shaders, and it is known (as 
        // per my design) only by the OriginalDataSsbo object
        dataToSort->ConfigureConstantUniforms(_particleDataToIntermediateDataProgramId);
        dataToSort->ConfigureConstantUniforms(_sortParticlesProgramId);

        unsigned int numParticles = dataToSort->NumVertices();
        _particleCopySsbo = std::make_unique<ParticleCopySsbo>(numParticles);
        _prefixSumSsbo = std::make_unique<PrefixSumSsbo>(numParticles);

        // the PrefixScanBuffer is used in three shaders
        _prefixSumSsbo->ConfigureConstantUniforms(_getBitForPrefixScansProgramId);
        _prefixSumSsbo->ConfigureConstantUniforms(_parallelPrefixScanProgramId);
        _prefixSumSsbo->ConfigureConstantUniforms(_sortIntermediateDataProgramId);

        // see explanation in the PrefixSumSsbo constructor for why there are likely more 
        // entries in PrefixScanBuffer::PrefixSumsPerWorkGroup than the requested number of items 
        // that need sorting
        unsigned int numEntriesInPrefixSumBuffer = _prefixSumSsbo->NumDataEntries();
        _intermediateDataSsbo = std::make_unique<IntermediateDataSsbo>(numEntriesInPrefixSumBuffer);
        _intermediateDataSsbo->ConfigureConstantUniforms(_particleDataToIntermediateDataProgramId);
        _intermediateDataSsbo->ConfigureConstantUniforms(_getBitForPrefixScansProgramId);
        _intermediateDataSsbo->ConfigureConstantUniforms(_sortIntermediateDataProgramId);

    }

    /*--------------------------------------------------------------------------------------------
    Description:
        Cleans up shader programs that were created for this shader controller.  The temporary 
        SSBOs clean themselves up.
    Parameters: None
    Returns:    None
    Creator:    John Cox, 4/2017
    --------------------------------------------------------------------------------------------*/
    ParallelSort::~ParallelSort()
    {
        glDeleteProgram(_particleDataToIntermediateDataProgramId);
        glDeleteProgram(_getBitForPrefixScansProgramId);
        glDeleteProgram(_parallelPrefixScanProgramId);
        glDeleteProgram(_sortIntermediateDataProgramId);
        glDeleteProgram(_sortParticlesProgramId);
    }

        /*--------------------------------------------------------------------------------------------
    Description:
        This function is the main show of this demo.  It summons shaders to do the following:
        - Copy original data to intermediate data structures 
            Note: If you want to sort your OriginalData structure over a particular value, this 
            is where you decide that.  The rest of the sorting works blindly, bit by bit, on the 
            IntermediateData::_data value.

        - Loop through all 32 bits in an unsigned integer
            - Get bits one at a time from the values in the intermediate data structures
            - Run the parallel prefix scan algorithm on those bit values by work group
            - Run the parallel prefix scan over each work group's sum
            - Sort the IntermediateData structures using the resulting prefix sums
        - Sort the OriginalData items into a copy buffer using sorted IntermediateData objects
        - Copy the sorted copy buffer back into ParticleBuffer

        The ParticleBuffer is now sorted.
    Parameters: None
    Returns:    None
    Creator:    John Cox, 4/2017
    --------------------------------------------------------------------------------------------*/
    void ParallelSort::SortWithoutProfiling() const
    {
        unsigned int numItemsInPrefixScanBuffer = _prefixSumSsbo->NumDataEntries();
        
        // for ParallelPrefixScan.comp, which works on 2 items per thread
        int numWorkGroupsXByItemsPerWorkGroup = numItemsInPrefixScanBuffer / PARALLEL_SORT_ITEMS_PER_WORK_GROUP;
        int remainder = numItemsInPrefixScanBuffer % PARALLEL_SORT_ITEMS_PER_WORK_GROUP;
        numWorkGroupsXByItemsPerWorkGroup += (remainder == 0) ? 0 : 1;

        // for other shaders, which work on 1 item per thread
        int numWorkGroupsXByWorkGroupSize = numItemsInPrefixScanBuffer / PARALLEL_SORT_WORK_GROUP_SIZE_X;
        remainder = numItemsInPrefixScanBuffer % PARALLEL_SORT_WORK_GROUP_SIZE_X;
        numWorkGroupsXByWorkGroupSize += (remainder == 0) ? 0 : 1;

        // working on a 1D array (X dimension), so these are always 1
        int numWorkGroupsY = 1;
        int numWorkGroupsZ = 1;

        // moving original data to intermediate data is 1 item per thread
        glUseProgram(_particleDataToIntermediateDataProgramId);
        glDispatchCompute(numWorkGroupsXByWorkGroupSize, numWorkGroupsY, numWorkGroupsZ);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    
        // for 32bit unsigned integers, make 32 passes, one for each bit
        bool writeToSecondBuffer = true;
        for (unsigned int bitNumber = 0; bitNumber < 32; bitNumber++)
        {
            // this will either be 0 or half the size of IntermediateDataBuffer
            unsigned int intermediateDataReadBufferOffset = (unsigned int)!writeToSecondBuffer * numItemsInPrefixScanBuffer;
            unsigned int intermediateDataWriteBufferOffset = (unsigned int)writeToSecondBuffer * numItemsInPrefixScanBuffer;

            // getting 1 bit value from intermediate data to prefix sum is 1 item per thread
            glUseProgram(_getBitForPrefixScansProgramId);
            glUniform1ui(UNIFORM_LOCATION_INTERMEDIATE_BUFFER_READ_OFFSET, intermediateDataReadBufferOffset);
            glUniform1ui(UNIFORM_LOCATION_INTERMEDIATE_BUFFER_WRITE_OFFSET, intermediateDataWriteBufferOffset);
            glUniform1ui(UNIFORM_LOCATION_BIT_NUMBER, bitNumber);
            glDispatchCompute(numWorkGroupsXByWorkGroupSize, numWorkGroupsY, numWorkGroupsZ);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

            // prefix scan over all values
            // Note: Parallel prefix scan is 2 items per thread.
            glUseProgram(_parallelPrefixScanProgramId);
            glUniform1ui(UNIFORM_LOCATION_CALCULATE_ALL, 1);
            glDispatchCompute(numWorkGroupsXByItemsPerWorkGroup, numWorkGroupsY, numWorkGroupsZ);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

            // prefix scan over per-work-group sums
            // Note: The PrefixSumsOfWorkGroupSums array is sized to be exactly enough for 1 work group.  
            // It makes the prefix sum easier than trying to eliminate excess threads.
            glUniform1ui(UNIFORM_LOCATION_CALCULATE_ALL, 0);
            glDispatchCompute(1, numWorkGroupsY, numWorkGroupsZ);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

            // and sort the intermediate data with the scanned values
            glUseProgram(_sortIntermediateDataProgramId);
            glUniform1ui(UNIFORM_LOCATION_INTERMEDIATE_BUFFER_READ_OFFSET, intermediateDataReadBufferOffset);
            glUniform1ui(UNIFORM_LOCATION_INTERMEDIATE_BUFFER_WRITE_OFFSET, intermediateDataWriteBufferOffset);
            glUniform1ui(UNIFORM_LOCATION_BIT_NUMBER, bitNumber);
            glDispatchCompute(numWorkGroupsXByWorkGroupSize, numWorkGroupsY, numWorkGroupsZ);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

            // now switch intermediate buffers and do it again
            writeToSecondBuffer = !writeToSecondBuffer;
        }

        // now use the sorted IntermediateData objects to sort the original data objects into a 
        // copy buffer (there is no "swap" in parallel sorting, so must write to a dedicated 
        // copy buffer
        glUseProgram(_sortParticlesProgramId);
        unsigned int intermediateDataReadBufferOffset = (unsigned int)!writeToSecondBuffer * numItemsInPrefixScanBuffer;
        glUniform1ui(UNIFORM_LOCATION_INTERMEDIATE_BUFFER_READ_OFFSET, intermediateDataReadBufferOffset);
        glDispatchCompute(numWorkGroupsXByWorkGroupSize, numWorkGroupsY, numWorkGroupsZ);

        // make the results of the last one available for rendering
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);

        // and finally, move the sorted original data from the copy buffer back to the 
        // ParticleBuffer
        glBindBuffer(GL_COPY_READ_BUFFER, _particleCopySsbo->BufferId());
        glBindBuffer(GL_COPY_WRITE_BUFFER, _particleSsbo->BufferId());
        unsigned int ParticleBufferSizeBytes = _particleSsbo->NumVertices() * sizeof(Particle);
        glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, 0, ParticleBufferSizeBytes);
        glBindBuffer(GL_COPY_READ_BUFFER, 0);
        glBindBuffer(GL_COPY_WRITE_BUFFER, 0);

        // end sorting
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        glUseProgram(0);
    }

    /*--------------------------------------------------------------------------------------------
    Description:
        The same sorting algorithm, but with:
        (1) std::chrono calls scattered everywhere
        (2) sorted data verification on the CPU (takes ~1sec, so it's terrible for frame rate)
        (3) writing the profiled duration results to stdout and to a tab-delimited text file
    Parameters: None
    Returns:    None
    Creator:    John Cox, 3/2017
    --------------------------------------------------------------------------------------------*/
    void ParallelSort::SortWithProfiling() const
    {
        unsigned int numItemsInPrefixScanBuffer = _prefixSumSsbo->NumDataEntries();

        cout << "sorting " << numItemsInPrefixScanBuffer << " items" << endl;

        // for profiling
        using namespace std::chrono;
        steady_clock::time_point parallelSortStart;
        steady_clock::time_point start;
        steady_clock::time_point end;
        long long durationOriginalDataToIntermediateData = 0;
        long long durationDataVerification = 0;
        std::vector<long long> durationsGetBitForPrefixScan(32);
        std::vector<long long> durationsPrefixScanAll(32);
        std::vector<long long> durationsPrefixScanWorkGroupSums(32);
        std::vector<long long> durationsSortIntermediateData(32);

        // begin
        parallelSortStart = high_resolution_clock::now();

        // for ParallelPrefixScan.comp, which works on 2 items per thread
        int numWorkGroupsXByItemsPerWorkGroup = numItemsInPrefixScanBuffer / PARALLEL_SORT_ITEMS_PER_WORK_GROUP;
        int remainder = numItemsInPrefixScanBuffer % PARALLEL_SORT_ITEMS_PER_WORK_GROUP;
        numWorkGroupsXByItemsPerWorkGroup += (remainder == 0) ? 0 : 1;

        // for other shaders, which work on 1 item per thread
        int numWorkGroupsXByWorkGroupSize = numItemsInPrefixScanBuffer / PARALLEL_SORT_WORK_GROUP_SIZE_X;
        remainder = numItemsInPrefixScanBuffer % PARALLEL_SORT_WORK_GROUP_SIZE_X;
        numWorkGroupsXByWorkGroupSize += (remainder == 0) ? 0 : 1;

        // working on a 1D array (X dimension), so these are always 1
        int numWorkGroupsY = 1;
        int numWorkGroupsZ = 1;

        // moving original data to intermediate data is 1 item per thread
        start = high_resolution_clock::now();
        glUseProgram(_particleDataToIntermediateDataProgramId);
        glDispatchCompute(numWorkGroupsXByWorkGroupSize, numWorkGroupsY, numWorkGroupsZ);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        WaitForComputeToFinish();
        end = high_resolution_clock::now();
        durationOriginalDataToIntermediateData = duration_cast<microseconds>(end - start).count();
    
        // for 32bit unsigned integers, make 32 passes
        bool writeToSecondBuffer = true;
        for (unsigned int bitNumber = 0; bitNumber < 32; bitNumber++)
        {
            // this will either be 0 or half the size of IntermediateDataBuffer
            unsigned int intermediateDataReadBufferOffset = (unsigned int)!writeToSecondBuffer * numItemsInPrefixScanBuffer;
            unsigned int intermediateDataWriteBufferOffset = (unsigned int)writeToSecondBuffer * numItemsInPrefixScanBuffer;

            // getting 1 bit value from intermediate data to prefix sum is 1 item per thread
            start = high_resolution_clock::now();
            glUseProgram(_getBitForPrefixScansProgramId);
            glUniform1ui(UNIFORM_LOCATION_INTERMEDIATE_BUFFER_READ_OFFSET, intermediateDataReadBufferOffset);
            glUniform1ui(UNIFORM_LOCATION_INTERMEDIATE_BUFFER_WRITE_OFFSET, intermediateDataWriteBufferOffset);
            glUniform1ui(UNIFORM_LOCATION_BIT_NUMBER, bitNumber);
            glDispatchCompute(numWorkGroupsXByWorkGroupSize, numWorkGroupsY, numWorkGroupsZ);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
            WaitForComputeToFinish();
            end = high_resolution_clock::now();
            durationsGetBitForPrefixScan[bitNumber] = (duration_cast<microseconds>(end - start).count());

            // prefix scan over all values
            // Note: Parallel prefix scan is 2 items per thread.
            start = high_resolution_clock::now();
            glUseProgram(_parallelPrefixScanProgramId);
            glUniform1ui(UNIFORM_LOCATION_CALCULATE_ALL, 1);
            glDispatchCompute(numWorkGroupsXByItemsPerWorkGroup, numWorkGroupsY, numWorkGroupsZ);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
            WaitForComputeToFinish();
            end = high_resolution_clock::now();
            durationsPrefixScanAll[bitNumber] = (duration_cast<microseconds>(end - start).count());

            // prefix scan over per-work-group sums
            // Note: The PrefixSumsOfWorkGroupSums array is sized to be exactly enough for 1 work group.  
            // It makes the prefix sum easier than trying to eliminate excess threads.
            start = high_resolution_clock::now();
            glUniform1ui(UNIFORM_LOCATION_CALCULATE_ALL, 0);
            glDispatchCompute(1, numWorkGroupsY, numWorkGroupsZ);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
            WaitForComputeToFinish();
            end = high_resolution_clock::now();
            durationsPrefixScanWorkGroupSums[bitNumber] = (duration_cast<microseconds>(end - start).count());

            // and sort the intermediate data with the scanned values
            start = high_resolution_clock::now();
            glUseProgram(_sortIntermediateDataProgramId);
            glUniform1ui(UNIFORM_LOCATION_INTERMEDIATE_BUFFER_READ_OFFSET, intermediateDataReadBufferOffset);
            glUniform1ui(UNIFORM_LOCATION_INTERMEDIATE_BUFFER_WRITE_OFFSET, intermediateDataWriteBufferOffset);
            glUniform1ui(UNIFORM_LOCATION_BIT_NUMBER, bitNumber);
            glDispatchCompute(numWorkGroupsXByWorkGroupSize, numWorkGroupsY, numWorkGroupsZ);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
            WaitForComputeToFinish();
            end = high_resolution_clock::now();
            durationsSortIntermediateData[bitNumber] = (duration_cast<microseconds>(end - start).count());

            // now switch intermediate buffers and do it again
            writeToSecondBuffer = !writeToSecondBuffer;
        }

        // now use the sorted IntermediateData objects to sort the original data objects into a 
        // copy buffer (there is no "swap" in parallel sorting, so must write to a dedicated 
        // copy buffer
        start = high_resolution_clock::now();
        glUseProgram(_sortParticlesProgramId);
        unsigned int intermediateDataReadBufferOffset = (unsigned int)!writeToSecondBuffer * numItemsInPrefixScanBuffer;
        glUniform1ui(UNIFORM_LOCATION_INTERMEDIATE_BUFFER_READ_OFFSET, intermediateDataReadBufferOffset);
        glDispatchCompute(numWorkGroupsXByWorkGroupSize, numWorkGroupsY, numWorkGroupsZ);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        WaitForComputeToFinish();
        end = high_resolution_clock::now();
        long long durationSortParticleData = duration_cast<microseconds>(end - start).count();

        // and finally, move the sorted original data from the copy buffer back to the 
        // ParticleBuffer
        start = high_resolution_clock::now();
        glBindBuffer(GL_COPY_READ_BUFFER, _particleCopySsbo->BufferId());
        glBindBuffer(GL_COPY_WRITE_BUFFER, _particleSsbo->BufferId());
        unsigned int ParticleBufferSizeBytes = _particleSsbo->NumVertices() * sizeof(Particle);
        glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, 0, ParticleBufferSizeBytes);
        glBindBuffer(GL_COPY_READ_BUFFER, 0);
        glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
        WaitForComputeToFinish();
        end = high_resolution_clock::now();
        long long durationCopySortedOriginalData = duration_cast<microseconds>(end - start).count();

        // end sorting
        steady_clock::time_point parallelSortEnd = high_resolution_clock::now();

        // verify sorted data
        start = high_resolution_clock::now();
        unsigned int startingIndex = 0;
        std::vector<Particle> checkOriginalData(_particleSsbo->NumVertices());
        unsigned int bufferSizeBytes = checkOriginalData.size() * sizeof(Particle);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, _particleSsbo->BufferId());
        void *bufferPtr = glMapBufferRange(GL_SHADER_STORAGE_BUFFER, startingIndex, bufferSizeBytes, GL_MAP_READ_BIT);
        memcpy(checkOriginalData.data(), bufferPtr, bufferSizeBytes);
        glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);

        // check
        for (unsigned int i = 1; i < checkOriginalData.size(); i++)
        {
            unsigned int thisIndex = i;
            unsigned int prevIndex = i - 1;
            unsigned int val = checkOriginalData[thisIndex]._mortonCode;
            unsigned int prevVal = checkOriginalData[prevIndex]._mortonCode;

            if (checkOriginalData[thisIndex]._isActive == 0)
            {
                continue;
            }
            else if (val == 0xffffffff)
            {
                // this was extra data that was padded on
                continue;
            }

            // the original data is 0 - N-1, 1 value at a time, so it's ok to hard code 
            if (val < prevVal)
            {
                printf("value %u at index %u is >= previous value %u and index %u\n", val, i, prevVal, i - 1);
            }
        }

        end = high_resolution_clock::now();
        durationDataVerification = duration_cast<microseconds>(end - start).count();

        // write the results to stdout and to a text file so that I can dump them into an Excel spreadsheet
        std::ofstream outFile("durations.txt");
        if (outFile.is_open())
        {
            long long totalParallelSortTime = duration_cast<microseconds>(parallelSortEnd - parallelSortStart).count();
            cout << "total sort time: " << totalParallelSortTime << "\tmicroseconds" << endl;
            outFile << "total sort time: " << totalParallelSortTime << "\tmicroseconds" << endl;

            cout << "original data to intermediate data: " << durationOriginalDataToIntermediateData << "\tmicroseconds" << endl;
            outFile << "original data to intermediate data: " << durationOriginalDataToIntermediateData << "\tmicroseconds" << endl;
        
            cout << "duration sort original data into copy buffer: " << durationSortParticleData << "\tmicroseconds" << endl;
            outFile << "duration sort original data into copy buffer: " << durationSortParticleData << "\tmicroseconds" << endl;

            cout << "duration copy sorted original data: " << durationCopySortedOriginalData << "\tmicroseconds" << endl;
            outFile << "duration copy sorted original data: " << durationCopySortedOriginalData << "\tmicroseconds" << endl;

            cout << "verifying data: " << durationDataVerification << "\tmicroseconds" << endl;
            outFile << "verifying data: " << durationDataVerification << "\tmicroseconds" << endl;

            cout << "getting bits for prefix scan:" << endl;
            outFile << "getting bits for prefix scan:" << endl;
            for (size_t i = 0; i < durationsGetBitForPrefixScan.size(); i++)
            {
                cout << i << "\t" << durationsGetBitForPrefixScan[i] << "\tmicroseconds" << endl;
                outFile << i << "\t" << durationsGetBitForPrefixScan[i] << "\tmicroseconds" << endl;
            }
            cout << endl;
            outFile << endl;

            cout << "times for prefix scan over all data:" << endl;
            outFile << "times for prefix scan over all data:" << endl;
            for (size_t i = 0; i < durationsPrefixScanAll.size(); i++)
            {
                cout << i << "\t" << durationsPrefixScanAll[i] << "\tmicroseconds" << endl;
                outFile << i << "\t" << durationsPrefixScanAll[i] << "\tmicroseconds" << endl;
            }
            cout << endl;
            outFile << endl;

            cout << "times for prefix scan over work group sums:" << endl;
            outFile << "times for prefix scan over work group sums:" << endl;
            for (size_t i = 0; i < durationsPrefixScanWorkGroupSums.size(); i++)
            {
                cout << i << "\t" << durationsPrefixScanWorkGroupSums[i] << "\tmicroseconds" << endl;
                outFile << i << "\t" << durationsPrefixScanWorkGroupSums[i] << "\tmicroseconds" << endl;
            }
            cout << endl;
            outFile << endl;

            cout << "times for sorting intermediate data:" << endl;
            outFile << "times for sorting intermediate data:" << endl;
            for (size_t i = 0; i < durationsSortIntermediateData.size(); i++)
            {
                cout << i << "\t" << durationsSortIntermediateData[i] << "\tmicroseconds" << endl;
                outFile << i << "\t" << durationsSortIntermediateData[i] << "\tmicroseconds" << endl;
            }
            cout << endl;
            outFile << endl;
        }
        outFile.close();

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        glUseProgram(0);

    }

    /*------------------------------------------------------------------------------------------------
    Description:
        This function is used to wait for the GPU to finish its commands.  It is useful during 
        profiling.  It was recommended to me by Osbios on the OpenGL subbreddit question 
        https://www.reddit.com/r/opengl/comments/69ri6l/increasing_buffer_size_for_compute_operations/.  
        It was recommended over glFinish() because that function needs to make a round-trip to the 
        GPU and back (??glClientWaitSync(...) doesn't??) and that apparently eats up some extra 
        cycles that can throw off my profiling.
    Parameters: None
    Returns:    None
    Creator:    John Cox, 5/2017
    ------------------------------------------------------------------------------------------------*/
    void ParallelSort::WaitForComputeToFinish() const
    {
        GLsync waitSync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        GLenum waitReturn = GL_UNSIGNALED;
        while (waitReturn != GL_ALREADY_SIGNALED && waitReturn != GL_CONDITION_SATISFIED)
        {
            waitReturn = glClientWaitSync(waitSync, GL_SYNC_FLUSH_COMMANDS_BIT, GL_TIMEOUT_IGNORED);
        }
        glDeleteSync(waitSync);
    }

}
