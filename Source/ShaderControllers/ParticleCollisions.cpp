#include "Include/ShaderControllers/ParticleCollisions.h"

#include "Shaders/ShaderStorage.h"
#include "ThirdParty/glload/include/glload/gl_4_4.h"

#include "Include/ShaderControllers/ProfilingWaitToFinish.h"

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
        Gives members initial values.  Generates compute shaders for the different stages of the 
        BVH generation.  

        Note: Two SSBOs are provided as constructor arguments because they are both externally 
        generated SSBOs and both of their corresponding compute header files have buffer size 
        uniforms that need to set their values with any shader programs that this shader 
        controller generates.
    Parameters:
        leafData    Passed in so that it can have its uniforms set for the shaders.
        bvhSsbo     Contains info on the number of leaves.  
    Returns:    None
    Creator:    John Cox, 3/2017
    --------------------------------------------------------------------------------------------*/
    ParticleCollisions::ParticleCollisions(const ParticleSsbo::SharedConstPtr particleSsbo) :
        _numLeaves(particleSsbo->NumParticles()),
        _generateBinaryRadixTreeProgramId(0),
        _generateBoundingVolumesProgramId(0),
        _bvhNodeSsbo(nullptr)
    {
        ShaderStorage &shaderStorageRef = ShaderStorage::GetInstance();
        std::string shaderKey;

        // copy the particle's Morton code and generate a bounding box for the leaf node, then 
        // generate all the tree's internal nodes
        shaderKey = "generate binary radix tree";
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
        _generateBinaryRadixTreeProgramId = shaderStorageRef.GetShaderProgram(shaderKey);

        // take the generated tree and merge the bounding boxes from the leaves up to the root, 
        // thus finishing the hierarchy of bounding volumes
        shaderKey = "generate bounding volumes";
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
        _generateBoundingVolumesProgramId = shaderStorageRef.GetShaderProgram(shaderKey);

        // TODO: create particle collision detection and resolution shader

        // generate the BVH that will used for all this collision detection
        _bvhNodeSsbo = std::make_shared<BvhNodeSsbo>(particleSsbo->NumParticles());


        // set buffer sizes for each of the programs
        particleSsbo->ConfigureConstantUniforms(_generateBinaryRadixTreeProgramId);
        particleSsbo->ConfigureConstantUniforms(_generateBoundingVolumesProgramId);
        _bvhNodeSsbo->ConfigureConstantUniforms(_generateBinaryRadixTreeProgramId);
        _bvhNodeSsbo->ConfigureConstantUniforms(_generateBoundingVolumesProgramId);
        // TODO: configure for particle detection and resolution
    }

    /*--------------------------------------------------------------------------------------------
    Description:
        Cleans up shader programs that were created for this shader controller.  The SSBOs clean 
        themselves up.
    Parameters: None
    Returns:    None
    Creator:    John Cox, 5/2017
    --------------------------------------------------------------------------------------------*/
    ParticleCollisions::~ParticleCollisions()
    {
        glDeleteProgram(_generateBinaryRadixTreeProgramId);
        glDeleteProgram(_generateBoundingVolumesProgramId);
    }

    /*--------------------------------------------------------------------------------------------
    Description:
        (1) Constructs a binary radix tree using an algorithm that generates all internal nodes 
        on one pass.  
        (2) Then constructs the bounding volumes for each node in the tree from the leaves to 
        the root.
        (3) Each particle navigates the tree from the root and collects a list of all particles 
        whose bounding boxes overlap, then calculates a collision between the most overlapped 
        particles.
    Parameters: None
    Returns:    None
    Creator:    John Cox, 5/2017
    --------------------------------------------------------------------------------------------*/
    void ParticleCollisions::DetectAndResolveWithoutProfiling() const
    {
        int numWorkGroupsX = (_numLeaves / PARTICLE_OPERATIONS_WORK_GROUP_SIZE_X) + 1;
        int numWorkGroupsY = 1;
        int numWorkGroupsZ = 1;

        // construct the hierarchy
        glUseProgram(_generateBinaryRadixTreeProgramId);
        glDispatchCompute(numWorkGroupsX, numWorkGroupsY, numWorkGroupsZ);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        // merge the bounding boxes of individual leaves (particles) up to the root
        glUseProgram(_generateBoundingVolumesProgramId);
        glDispatchCompute(numWorkGroupsX, numWorkGroupsY, numWorkGroupsZ);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        //TODO: collision detection and resolution

        // end collision detection and resolution
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        glUseProgram(0);
    }

    /*--------------------------------------------------------------------------------------------
    Description:
        The same sorting algorithm, but with:
        (1) std::chrono calls scattered everywhere
        (2) writing the profiled duration results to stdout and to a tab-delimited text file
    Parameters: None
    Returns:    None
    Creator:    John Cox, 3/2017
    --------------------------------------------------------------------------------------------*/
    void ParticleCollisions::DetectAndResolveWithProfiling() const
    {
        cout << "Generating BVH for " << _numLeaves << " particle leaves" << endl;

        // for profiling
        using namespace std::chrono;
        steady_clock::time_point generateBvhStart;
        steady_clock::time_point start;
        steady_clock::time_point end;
        long long durationGenerateTree = 0;
        long long durationMergeBoundingBoxes = 0;

        // begin
        generateBvhStart = high_resolution_clock::now();

        int numWorkGroupsX = (_numLeaves / PARTICLE_OPERATIONS_WORK_GROUP_SIZE_X) + 1;
        int numWorkGroupsY = 1;
        int numWorkGroupsZ = 1;

        // construct the hierarchy
        start = high_resolution_clock::now();
        glUseProgram(_generateBinaryRadixTreeProgramId);
        glDispatchCompute(numWorkGroupsX, numWorkGroupsY, numWorkGroupsZ);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        WaitForComputeToFinish();
        end = high_resolution_clock::now();
        durationGenerateTree = (duration_cast<microseconds>(end - start).count());

        // merge the bounding boxes of individual leaves (particles) up to the root
        start = high_resolution_clock::now();
        glUseProgram(_generateBoundingVolumesProgramId);
        glDispatchCompute(numWorkGroupsX, numWorkGroupsY, numWorkGroupsZ);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        WaitForComputeToFinish();
        end = high_resolution_clock::now();
        durationMergeBoundingBoxes = (duration_cast<microseconds>(end - start).count());

        glUseProgram(0);

        // end 
        steady_clock::time_point generateBvhEnd = high_resolution_clock::now();


        // write the results to stdout and to a text file so that I can dump them into an Excel spreadsheet
        std::ofstream outFile("GenerateBvhDurations.txt");
        if (outFile.is_open())
        {
            long long totalParallelSortTime = duration_cast<microseconds>(generateBvhEnd - generateBvhStart).count();
            cout << "total sort time: " << totalParallelSortTime << "\tmicroseconds" << endl;
            outFile << "total sort time: " << totalParallelSortTime << "\tmicroseconds" << endl;

            cout << "generate BVH tree: " << durationGenerateTree << "\tmicroseconds" << endl;
            outFile << "generate BVH tree: " << durationGenerateTree << "\tmicroseconds" << endl;

            cout << "generate BVH bounding boxes: " << durationMergeBoundingBoxes << "\tmicroseconds" << endl;
            outFile << "generate BVH bounding boxes: " << durationMergeBoundingBoxes << "\tmicroseconds" << endl;

            cout << endl;
            outFile << endl;

        }
        outFile.close();
    }
    
}