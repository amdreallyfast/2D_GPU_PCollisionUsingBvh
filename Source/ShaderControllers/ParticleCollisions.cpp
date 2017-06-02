#include "Include/ShaderControllers/ParticleCollisions.h"

#include "Shaders/ShaderStorage.h"
#include "ThirdParty/glload/include/glload/gl_4_4.h"

// for profiling and checking results
#include "Include/ShaderControllers/ProfilingWaitToFinish.h"
#include "Include/Buffers/BvhNode.h"

#include "Shaders/ShaderHeaders/ComputeShaderWorkGroupSizes.comp"
#include "Shaders/ShaderHeaders/CrossShaderUniformLocations.comp"

#include <iostream>
#include <fstream>

#include <chrono>
#include <iostream>
using std::cout;
using std::endl;


//void GenerateTestBvh(std::vector<BvhNode> &bvh)
//{
//    bvh.clear();
//
//    int numLeaves = 16;
//    for (int leafCounter = 0; leafCounter < numLeaves; leafCounter++)
//    {
//        bvh.push_back(BvhNode());
//        bvh[leafCounter]._isLeaf = true;
//    }
//    
//    for (int  internalNodeCounter = 0; internalNodeCounter < numLeaves - 1; internalNodeCounter++)
//    {
//        bvh.push_back(BvhNode());
//    }
//
//    //// Test BVH #1
//    //bvh[0]._data = 1;
//    //bvh[1]._data = 2;
//    //bvh[2]._data = 3;
//    //bvh[3]._data = 9;
//    //bvh[4]._data = 9;
//    //bvh[5]._data = 14;
//    //bvh[6]._data = 15;
//    //bvh[7]._data = 16;
//    //bvh[8]._data = 17;
//    //bvh[9]._data = 18;
//    //bvh[10]._data = 20;
//    //bvh[11]._data = 22;
//    //bvh[12]._data = 22;
//    //bvh[13]._data = 26;
//    //bvh[14]._data = 26;
//    //bvh[15]._data = 28;
//
//    // Test BVH #2
//    bvh[0]._data = 1271858;
//    bvh[1]._data = 34211986;
//    bvh[2]._data = 47408388;
//    bvh[3]._data = 75516948;
//    bvh[4]._data = 114443670;
//    bvh[5]._data = 276973568;
//    bvh[6]._data = 306777138;
//    bvh[7]._data = 345188406;
//    bvh[8]._data = 538667040;
//    bvh[9]._data = 549996564;
//    bvh[10]._data = 575677734;
//    bvh[11]._data = 584191158;
//    bvh[12]._data = 637668368;
//    bvh[13]._data = 643326102;
//    bvh[14]._data = 806428982;
//    bvh[15]._data = 815474724;
//}




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
    ParticleCollisions::ParticleCollisions(const ParticleSsbo::SharedConstPtr particleSsbo,
        const ParticlePropertiesSsbo::SharedConstPtr particlePropertiesSsbo) :
        _numParticles(particleSsbo->NumParticles()),

        _programIdCopyParticlesToCopyBuffer(0),
        _programIdGenerateSortingData(0),
        _programIdClearWorkGroupSums(0),
        _programIdGetBitForPrefixScan(0),
        _programIdPrefixScanOverAllData(0),
        _programIdPrefixScanOverWorkGroupSums(0),
        _programIdSortSortingDataWithPrefixSums(0),
        _programIdSortParticles(0),
        _programIdGuaranteeSortingDataUniqueness(0),
        _programIdGenerateLeafNodeBoundingBoxes(0),
        _programIdGenerateBinaryRadixTree(0),
        _programIdMergeBoundingVolumes(0),

        // generate buffers
        _particleSortingDataSsbo(particleSsbo->NumParticles()),
        _prefixSumSsbo(particleSsbo->NumParticles()),
        _bvhNodeSsbo(particleSsbo->NumParticles()),
        
        // Note: For N particles there are N leaves and N-1 internal nodes in the tree, and each 
        // node's bounding box has 4 faces.  
        _bvhGeometrySsbo(((particleSsbo->NumParticles() * 2) - 1) * 4),
        
        // kept around for debugging purposes
        _originalParticleSsbo(particleSsbo)
    {
        // the programs used during the parallel sort
        AssembleProgramCopyParticlesToCopyBuffer();
        AssembleProgramGenerateSortingData();
        AssembleProgramClearWorkGroupSums();
        AssembleProgramGetBitForPrefixScan();
        AssembleProgramPrefixScanOverAllData();
        AssembleProgramPrefixScanOverWorkGroupSums();
        AssembleProgramSortSortingDataWithPrefixSums();
        AssembleProgramSortParticles();

        // the programs used during BVH construction
        AssembleProgramGuaranteeSortingDataUniqueness();
        AssembleProgramGenerateLeafNodeBoundingBoxes();
        AssembleProgramGenerateBinaryRadixTree();
        AssembleProgramMergeBoundingVolumes();

        // load the buffer size uniforms where the SSBOs will be used
        particleSsbo->ConfigureConstantUniforms(_programIdCopyParticlesToCopyBuffer);
        particleSsbo->ConfigureConstantUniforms(_programIdGenerateSortingData);
        particleSsbo->ConfigureConstantUniforms(_programIdSortParticles);
        particleSsbo->ConfigureConstantUniforms(_programIdGenerateLeafNodeBoundingBoxes);
        // TODO: generate bvh vertices.comp
        // TODO: collision detection.comp

        particlePropertiesSsbo->ConfigureConstantUniforms(_programIdGenerateLeafNodeBoundingBoxes);

        _particleSortingDataSsbo.ConfigureConstantUniforms(_programIdGenerateSortingData);
        _particleSortingDataSsbo.ConfigureConstantUniforms(_programIdSortSortingDataWithPrefixSums);
        _particleSortingDataSsbo.ConfigureConstantUniforms(_programIdSortParticles);
        _particleSortingDataSsbo.ConfigureConstantUniforms(_programIdGuaranteeSortingDataUniqueness);
        _particleSortingDataSsbo.ConfigureConstantUniforms(_programIdGenerateBinaryRadixTree);

        _prefixSumSsbo.ConfigureConstantUniforms(_programIdGetBitForPrefixScan);
        _prefixSumSsbo.ConfigureConstantUniforms(_programIdPrefixScanOverAllData);
        _prefixSumSsbo.ConfigureConstantUniforms(_programIdPrefixScanOverWorkGroupSums);
        _prefixSumSsbo.ConfigureConstantUniforms(_programIdSortSortingDataWithPrefixSums);

        _bvhNodeSsbo.ConfigureConstantUniforms(_programIdGenerateLeafNodeBoundingBoxes);
        _bvhNodeSsbo.ConfigureConstantUniforms(_programIdGenerateBinaryRadixTree);
        _bvhNodeSsbo.ConfigureConstantUniforms(_programIdMergeBoundingVolumes);
        // TODO: collision detection.comp
        
        //_bvhGeometrySsbo.ConfigureConstantUniforms(_programidgen)
        // TODO: generate bvh vertices.comp



        //// test buffer
        //_bvhNodeSsbo = std::make_shared<BvhNodeSsbo>(16);
        //std::vector<BvhNode> testBvh;
        //GenerateTestBvh(testBvh);
        //glBindBuffer(GL_SHADER_STORAGE_BUFFER, _bvhNodeSsbo->BufferId());
        //glBufferData(GL_SHADER_STORAGE_BUFFER, testBvh.size() * sizeof(BvhNode), testBvh.data(), GL_DYNAMIC_DRAW);
        //glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

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
        glDeleteProgram(_programIdCopyParticlesToCopyBuffer);
        glDeleteProgram(_programIdGenerateSortingData);
        glDeleteProgram(_programIdClearWorkGroupSums);
        glDeleteProgram(_programIdGetBitForPrefixScan);
        glDeleteProgram(_programIdPrefixScanOverAllData);
        glDeleteProgram(_programIdPrefixScanOverWorkGroupSums);
        glDeleteProgram(_programIdSortSortingDataWithPrefixSums);
        glDeleteProgram(_programIdSortParticles);
        glDeleteProgram(_programIdGuaranteeSortingDataUniqueness);
        glDeleteProgram(_programIdGenerateLeafNodeBoundingBoxes);
        glDeleteProgram(_programIdGenerateBinaryRadixTree);
        glDeleteProgram(_programIdMergeBoundingVolumes);
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
    void ParticleCollisions::DetectAndResolveWithoutProfiling(unsigned int numActiveParticles) const
    {
        //int numWorkGroupsX = (_numLeaves / PARTICLE_OPERATIONS_WORK_GROUP_SIZE_X) + 1;
        //int numWorkGroupsY = 1;
        //int numWorkGroupsZ = 1;

        //// populate leaves with the particles' Morton Codes
        //glUseProgram(_populateLeavesWithDataProgramId);
        //glUniform1ui(UNIFORM_LOCATION_NUMBER_ACTIVE_PARTICLES, numActiveParticles);
        //glDispatchCompute(numWorkGroupsX, numWorkGroupsY, numWorkGroupsZ);
        //glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        //// construct the hierarchy
        //glUseProgram(_generateBinaryRadixTreeProgramId);
        //glUniform1ui(UNIFORM_LOCATION_NUMBER_ACTIVE_PARTICLES, numActiveParticles);
        //glDispatchCompute(numWorkGroupsX, numWorkGroupsY, numWorkGroupsZ);
        //glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        //// merge the bounding boxes of individual leaves (particles) up to the root
        //glUseProgram(_generateBoundingVolumesProgramId);
        //glUniform1ui(UNIFORM_LOCATION_NUMBER_ACTIVE_PARTICLES, numActiveParticles);
        //glDispatchCompute(numWorkGroupsX, numWorkGroupsY, numWorkGroupsZ);
        //glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        //// generate BVH vertices (optional)
        //glUseProgram(_generateVerticesProgramId);
        //glUniform1ui(UNIFORM_LOCATION_NUMBER_ACTIVE_PARTICLES, numActiveParticles);
        //glDispatchCompute(numWorkGroupsX, numWorkGroupsY, numWorkGroupsZ);
        //glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);

        //// traverse the tree and detect collisions
        //glUseProgram(_detectCollisionsProgramId);
        //glUniform1ui(UNIFORM_LOCATION_NUMBER_ACTIVE_PARTICLES, numActiveParticles);
        //glDispatchCompute(numWorkGroupsX, numWorkGroupsY, numWorkGroupsZ);
        //glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);

        //// resolve any detected collisions
        //glUseProgram(_resolveCollisionsProgramId);
        //glUniform1ui(UNIFORM_LOCATION_NUMBER_ACTIVE_PARTICLES, numActiveParticles);
        //glDispatchCompute(numWorkGroupsX, numWorkGroupsY, numWorkGroupsZ);
        //glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);


        //// end collision detection and resolution
        //glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        //glUseProgram(0);
    }

    /*--------------------------------------------------------------------------------------------
    Description:
        The same BVH generation and traversal algorithms, but with:
        (1) std::chrono calls scattered everywhere
        (2) writing the profiled duration results to stdout and to a tab-delimited text file
    Parameters: None
    Returns:    None
    Creator:    John Cox, 3/2017
    --------------------------------------------------------------------------------------------*/
    void ParticleCollisions::DetectAndResolveWithProfiling(unsigned int numActiveParticles) const
    {
        //cout << "Generating BVH for " << numActiveParticles << " particle leaves" << endl;

        //// for profiling
        //using namespace std::chrono;
        //steady_clock::time_point generateBvhStart;
        //steady_clock::time_point start;
        //steady_clock::time_point end;
        //long long durationPopulateTreeWithData = 0;
        //long long durationGenerateTree = 0;
        //long long durationMergeBoundingBoxes = 0;
        //long long durationGenerateBvhVertices = 0;
        //long long durationDetectCollisions = 0;
        //long long durationResolveCollisions = 0;

        //// wait for previous instructions to finish
        //WaitForComputeToFinish();
        //glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        //// begin
        //generateBvhStart = high_resolution_clock::now();

        //int numWorkGroupsX = (_numLeaves / PARTICLE_OPERATIONS_WORK_GROUP_SIZE_X) + 1;
        //int numWorkGroupsY = 1;
        //int numWorkGroupsZ = 1;

        //// populate leaves with the particles' Morton Codes
        //start = high_resolution_clock::now();
        //glUseProgram(_populateLeavesWithDataProgramId);
        //glUniform1ui(UNIFORM_LOCATION_NUMBER_ACTIVE_PARTICLES, numActiveParticles);
        //glDispatchCompute(numWorkGroupsX, numWorkGroupsY, numWorkGroupsZ);
        //glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        //WaitForComputeToFinish();
        //end = high_resolution_clock::now();
        //durationPopulateTreeWithData = duration_cast<microseconds>(end - start).count();

        //// construct the hierarchy
        //start = high_resolution_clock::now();
        //glUseProgram(_generateBinaryRadixTreeProgramId);
        //glUniform1ui(UNIFORM_LOCATION_NUMBER_ACTIVE_PARTICLES, numActiveParticles);
        //glDispatchCompute(numWorkGroupsX, numWorkGroupsY, numWorkGroupsZ);
        //glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        //WaitForComputeToFinish();
        //end = high_resolution_clock::now();
        //durationGenerateTree = duration_cast<microseconds>(end - start).count();

        //// merge the bounding boxes of individual leaves (particles) up to the root
        //start = high_resolution_clock::now();
        //glUseProgram(_generateBoundingVolumesProgramId);
        //glUniform1ui(UNIFORM_LOCATION_NUMBER_ACTIVE_PARTICLES, numActiveParticles);
        //glDispatchCompute(numWorkGroupsX, numWorkGroupsY, numWorkGroupsZ);
        //glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        //WaitForComputeToFinish();
        //end = high_resolution_clock::now();
        //durationMergeBoundingBoxes = duration_cast<microseconds>(end - start).count();

        //// generate BVH vertices (optional)
        //start = high_resolution_clock::now();
        //glUseProgram(_generateVerticesProgramId);
        //glUniform1ui(UNIFORM_LOCATION_NUMBER_ACTIVE_PARTICLES, numActiveParticles);
        //glDispatchCompute(numWorkGroupsX, numWorkGroupsY, numWorkGroupsZ);
        //glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);
        //WaitForComputeToFinish();
        //end = high_resolution_clock::now();
        //durationGenerateBvhVertices = duration_cast<microseconds>(end - start).count();

        //// traverse the tree and detect collisions
        //start = high_resolution_clock::now();
        //glUseProgram(_detectCollisionsProgramId);
        //glUniform1ui(UNIFORM_LOCATION_NUMBER_ACTIVE_PARTICLES, numActiveParticles);
        //glDispatchCompute(numWorkGroupsX, numWorkGroupsY, numWorkGroupsZ);
        //glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);
        //WaitForComputeToFinish();
        //end = high_resolution_clock::now();
        //durationDetectCollisions = duration_cast<microseconds>(end - start).count();

        //// resolve any detected collisions
        //start = high_resolution_clock::now();
        //glUseProgram(_resolveCollisionsProgramId);
        //glUniform1ui(UNIFORM_LOCATION_NUMBER_ACTIVE_PARTICLES, numActiveParticles);
        //glDispatchCompute(numWorkGroupsX, numWorkGroupsY, numWorkGroupsZ);
        //glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);
        //WaitForComputeToFinish();
        //end = high_resolution_clock::now();
        //durationResolveCollisions = duration_cast<microseconds>(end - start).count();


        //glUseProgram(0);

        //// end 
        //steady_clock::time_point generateBvhEnd = high_resolution_clock::now();

        //
        ////unsigned int startingIndex = 0;
        ////std::vector<BvhNode> checkOriginalData(_bvhNodeSsbo->NumTotalNodes());
        ////unsigned int bufferSizeBytes = checkOriginalData.size() * sizeof(BvhNode);
        ////glBindBuffer(GL_SHADER_STORAGE_BUFFER, _bvhNodeSsbo->BufferId());
        ////void *bufferPtr = glMapBufferRange(GL_SHADER_STORAGE_BUFFER, startingIndex, bufferSizeBytes, GL_MAP_READ_BIT);
        ////memcpy(checkOriginalData.data(), bufferPtr, bufferSizeBytes);
        ////glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);

        //////startingIndex = 0;
        //////std::vector<PolygonFace> checkOriginalPolygonData(_bvhGeometrySsbo->NumItems());
        //////bufferSizeBytes = checkOriginalPolygonData.size() * sizeof(PolygonFace);
        //////glBindBuffer(GL_SHADER_STORAGE_BUFFER, _bvhGeometrySsbo->BufferId());
        //////bufferPtr = glMapBufferRange(GL_SHADER_STORAGE_BUFFER, startingIndex, bufferSizeBytes, GL_MAP_READ_BIT);
        //////memcpy(checkOriginalPolygonData.data(), bufferPtr, bufferSizeBytes);
        //////glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);

        ////printf("");

        ////if (numActiveParticles > 1000)
        ////{
        ////    for (size_t nodeIndex = 0; nodeIndex < numActiveParticles; nodeIndex++)
        ////    {
        ////        if (checkOriginalData[nodeIndex]._extraData1 == 0)
        ////        {
        ////            printf("");
        ////            std::ofstream outfile("BvhDump.txt");
        ////            for (size_t i = 0; i < checkOriginalData.size(); i++)
        ////            {
        ////                outfile << "i = " << i
        ////                    << "\tisLeaf = " << checkOriginalData[i]._isLeaf
        ////                    << "\tparentIndex = " << checkOriginalData[i]._parentIndex
        ////                    << "\tstartIndex = " << checkOriginalData[i]._startIndex
        ////                    << "\tendIndex = " << checkOriginalData[i]._endIndex
        ////                    << "\tsplitIndex = " << checkOriginalData[i]._leafSplitIndex
        ////                    << "\tleftChildIndex = " << checkOriginalData[i]._leftChildIndex
        ////                    << "\trightChildIndex = " << checkOriginalData[i]._rightChildIndex
        ////                    << "\tdata = " << checkOriginalData[i]._data

        ////                    << "\textraData1 = " << checkOriginalData[i]._extraData1
        ////                    << "\textraData2 = " << checkOriginalData[i]._extraData2
        ////                    << "\tleft = " << checkOriginalData[i]._boundingBox._left
        ////                    << "\tright = " << checkOriginalData[i]._boundingBox._right
        ////                    << "\tbottom = " << checkOriginalData[i]._boundingBox._bottom
        ////                    << "\ttop = " << checkOriginalData[i]._boundingBox._top
        ////                    << endl;
        ////            }
        ////            outfile.close();

        ////            return;
        ////        }
        ////    }
        ////}


        //// write the results to stdout and to a text file so that I can dump them into an Excel spreadsheet
        ////std::ofstream outFile("GenerateBvhDurations.txt");
        ////if (outFile.is_open())
        //{
        //    long long totalCollisionDetectionTime = duration_cast<microseconds>(generateBvhEnd - generateBvhStart).count();
        //    cout << "total collision time: " << totalCollisionDetectionTime << "\tmicroseconds" << endl;
        //    //outFile << "total collision time: " << totalCollisionDetectionTime << "\tmicroseconds" << endl;

        //    cout << "populate leaves with particles' Morton Codes: " << durationPopulateTreeWithData << "\tmicroseconds" << endl;
        //    //outFile << "populate leaves with particles' Morton Codes: " << durationPopulateTreeWithData << "\tmicroseconds" << endl;

        //    cout << "generate BVH tree: " << durationGenerateTree << "\tmicroseconds" << endl;
        //    //outFile << "generate BVH tree: " << durationGenerateTree << "\tmicroseconds" << endl;

        //    cout << "generate BVH bounding boxes: " << durationMergeBoundingBoxes << "\tmicroseconds" << endl;
        //    //outFile << "generate BVH bounding boxes: " << durationMergeBoundingBoxes << "\tmicroseconds" << endl;

        //    cout << "generate BVH vertices: " << durationGenerateBvhVertices << "\tmicroseconds" << endl;
        //    //outFile << "generate BVH vertices: " << durationGenerateBvhVertices << "\tmicroseconds" << endl;

        //    cout << "detect collisions: " << durationDetectCollisions << "\tmicroseconds" << endl;
        //    //outFile << "detect collisions: " << durationDetectCollisions << "\tmicroseconds" << endl;

        //    cout << "resolve collisions: " << durationResolveCollisions << "\tmicroseconds" << endl;
        //    //outFile << "resolve collisions: " << durationResolveCollisions << "\tmicroseconds" << endl;

        //    cout << endl;
        //    //outFile << endl;

        //}
        ////outFile.close();
    }

    /*--------------------------------------------------------------------------------------------
    Description:
        Used so that the RenderGeometry shader controller can draw the BVH.
    Parameters: None
    Returns:    
        A const copy of a const shared pointer to the SSBO that contains the BVH's vertices.
        That's a mouthful.  ??should this SSBO be a member instead of a pointer to member??
    Creator:    John Cox, 3/2017
    --------------------------------------------------------------------------------------------*/
    const PolygonSsbo &ParticleCollisions::BvhVerticesSsbo() const
    {
        return _bvhGeometrySsbo;
    }

    /*--------------------------------------------------------------------------------------------
    Description:
        The GLSL version declaration, compute shader work group sizes, 
        cross-shader uniform locations, and SSBO buffer bindings are used in very compute 
        shader.  This function puts their assembly into one place.
    Parameters: 
        The key to the composite shader that is under construction.
    Returns:    None
    Creator:    John Cox, 5/2017
    --------------------------------------------------------------------------------------------*/
    void ParticleCollisions::AssembleProgramHeader(const std::string &shaderKey) const
    {
        ShaderStorage &shaderStorageRef = ShaderStorage::GetInstance();
        shaderStorageRef.NewCompositeShader(shaderKey);
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/ShaderHeaders/Version.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/ShaderHeaders/ComputeShaderWorkGroupSizes.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/ShaderHeaders/SsboBufferBindings.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/ShaderHeaders/CrossShaderUniformLocations.comp");
    }

    /*--------------------------------------------------------------------------------------------
    Description:
        Assembles headers, buffers, and functional .comp files for the shader that prepares the 
        ParticleBuffer for the final stage of particle sorting in SortParticles.comp.
    Parameters: None
    Returns:    None
    Creator:    John Cox, 5/2017
    --------------------------------------------------------------------------------------------*/
    void ParticleCollisions::AssembleProgramCopyParticlesToCopyBuffer()
    {
        ShaderStorage &shaderStorageRef = ShaderStorage::GetInstance();

        std::string shaderKey = "copy particles to copy buffer";
        shaderStorageRef.NewCompositeShader(shaderKey);
        AssembleProgramHeader(shaderKey);
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ParticleCollisions/Buffers/ParticleBuffer.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ParticleCollisions/CopyParticlesToCopyBuffer.comp");
        shaderStorageRef.CompileCompositeShader(shaderKey, GL_COMPUTE_SHADER);
        shaderStorageRef.LinkShader(shaderKey);
        _programIdCopyParticlesToCopyBuffer = shaderStorageRef.GetShaderProgram(shaderKey);
    }

    /*--------------------------------------------------------------------------------------------
    Description:
        Assembles headers, buffers, and functional .comp files for the shader that prepares the 
        data over which particles will be sorted.
    Parameters: None
    Returns:    None
    Creator:    John Cox, 5/2017
    --------------------------------------------------------------------------------------------*/
    void ParticleCollisions::AssembleProgramGenerateSortingData()
    {
        ShaderStorage &shaderStorageRef = ShaderStorage::GetInstance();

        std::string shaderKey = "generate sorting data";
        shaderStorageRef.NewCompositeShader(shaderKey);
        AssembleProgramHeader(shaderKey);
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ParticleCollisions/Buffers/ParticleBuffer.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ParticleCollisions/Buffers/ParticleSortingDataBuffer.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ParticleCollisions/Buffers/PositionToMortonCode.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ParticleCollisions/GenerateSortingData.comp");
        shaderStorageRef.CompileCompositeShader(shaderKey, GL_COMPUTE_SHADER);
        shaderStorageRef.LinkShader(shaderKey);
        _programIdGenerateSortingData = shaderStorageRef.GetShaderProgram(shaderKey);
    }

    /*--------------------------------------------------------------------------------------------
    Description:
        Assembles headers, buffers, and functional .comp files for the shader that resets the 
        prefix sum for each work group's result to 0.

        Part off the radix sort loop.
    Parameters: None
    Returns:    None
    Creator:    John Cox, 5/2017
    --------------------------------------------------------------------------------------------*/
    void ParticleCollisions::AssembleProgramClearWorkGroupSums()
    {
        ShaderStorage &shaderStorageRef = ShaderStorage::GetInstance();

        std::string shaderKey = "clear work group sums";
        shaderStorageRef.NewCompositeShader(shaderKey);
        AssembleProgramHeader(shaderKey);
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ParticleCollisions/Buffers/PrefixScanBuffer.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ParticleCollisions/ClearWorkGroupSums.comp");
        shaderStorageRef.CompileCompositeShader(shaderKey, GL_COMPUTE_SHADER);
        shaderStorageRef.LinkShader(shaderKey);
        _programIdClearWorkGroupSums = shaderStorageRef.GetShaderProgram(shaderKey);
    }

    /*--------------------------------------------------------------------------------------------
    Description:
        Assembles headers, buffers, and functional .comp files for the shader that extracts a 
        single bit from the sorting data for use during the prefix scan.

        Part of the radix sort loop.
    Parameters: None
    Returns:    None
    Creator:    John Cox, 5/2017
    --------------------------------------------------------------------------------------------*/
    void ParticleCollisions::AssembleProgramGetBitForPrefixScan()
    {
        ShaderStorage &shaderStorageRef = ShaderStorage::GetInstance();

        std::string shaderKey = "get bit for prefix scan";
        shaderStorageRef.NewCompositeShader(shaderKey);
        AssembleProgramHeader(shaderKey);
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ParticleCollisions/Buffers/ParticleSortingDataBuffer.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ParticleCollisions/Buffers/PrefixScanBuffer.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ParticleCollisions/GetBitForPrefixScan.comp");
        shaderStorageRef.CompileCompositeShader(shaderKey, GL_COMPUTE_SHADER);
        shaderStorageRef.LinkShader(shaderKey);
        _programIdGetBitForPrefixScan = shaderStorageRef.GetShaderProgram(shaderKey);
    }

    /*--------------------------------------------------------------------------------------------
    Description:
        Assembles headers, buffers, and functional .comp files for the shader that runs the 
        first part of the prefix scan. 

        Part of the radix sort loop.
    Parameters: None
    Returns:    None
    Creator:    John Cox, 5/2017
    --------------------------------------------------------------------------------------------*/
    void ParticleCollisions::AssembleProgramPrefixScanOverAllData()
    {
        ShaderStorage &shaderStorageRef = ShaderStorage::GetInstance();

        std::string shaderKey = "prefix scan over all data";
        shaderStorageRef.NewCompositeShader(shaderKey);
        AssembleProgramHeader(shaderKey);
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ParticleCollisions/Buffers/PrefixScanBuffer.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ParticleCollisions/PrefixScanOverAllData.comp");
        shaderStorageRef.CompileCompositeShader(shaderKey, GL_COMPUTE_SHADER);
        shaderStorageRef.LinkShader(shaderKey);
        _programIdPrefixScanOverAllData = shaderStorageRef.GetShaderProgram(shaderKey);
    }

    /*--------------------------------------------------------------------------------------------
    Description:
        Assembles headers, buffers, and functional .comp files for the shader that runs the
        second part of the prefix scan.

        Part of the radix sort loop.
    Parameters: None
    Returns:    None
    Creator:    John Cox, 5/2017
    --------------------------------------------------------------------------------------------*/
    void ParticleCollisions::AssembleProgramPrefixScanOverWorkGroupSums()
    {
        ShaderStorage &shaderStorageRef = ShaderStorage::GetInstance();

        std::string shaderKey = "prefix scan over work group sums";
        shaderStorageRef.NewCompositeShader(shaderKey);
        AssembleProgramHeader(shaderKey);
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ParticleCollisions/Buffers/PrefixScanBuffer.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ParticleCollisions/PrefixScanOverWorkGroupSums.comp");
        shaderStorageRef.CompileCompositeShader(shaderKey, GL_COMPUTE_SHADER);
        shaderStorageRef.LinkShader(shaderKey);
        _programIdPrefixScanOverWorkGroupSums = shaderStorageRef.GetShaderProgram(shaderKey);
    }

    /*--------------------------------------------------------------------------------------------
    Description:
        Assembles headers, buffers, and functional .comp files for the shader that sorts the 
        sorting data given the results of the prefix scan.

        Part of the radix sort loop.
    Parameters: None
    Returns:    None
    Creator:    John Cox, 5/2017
    --------------------------------------------------------------------------------------------*/
    void ParticleCollisions::AssembleProgramSortSortingDataWithPrefixSums()
    {
        ShaderStorage &shaderStorageRef = ShaderStorage::GetInstance();

        std::string shaderKey = "sort sorting data with prefix sums";
        shaderStorageRef.NewCompositeShader(shaderKey);
        AssembleProgramHeader(shaderKey);
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ParticleCollisions/Buffers/ParticleSortingDataBuffer.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ParticleCollisions/Buffers/PrefixScanBuffer.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ParticleCollisions/SortSortingDataWithPrefixSums.comp");
        shaderStorageRef.CompileCompositeShader(shaderKey, GL_COMPUTE_SHADER);
        shaderStorageRef.LinkShader(shaderKey);
        _programIdSortSortingDataWithPrefixSums = shaderStorageRef.GetShaderProgram(shaderKey);
    }

    /*--------------------------------------------------------------------------------------------
    Description:
        Assembles headers, buffers, and functional .comp files for the shader that copies 
        particles from the second half of the ParticleBuffer to their sorted position in the 
        first half.  This program is the complement to CopyParticlesToCopyBuffer.comp.
    Parameters: None
    Returns:    None
    Creator:    John Cox, 5/2017
    --------------------------------------------------------------------------------------------*/
    void ParticleCollisions::AssembleProgramSortParticles()
    {
        ShaderStorage &shaderStorageRef = ShaderStorage::GetInstance();

        std::string shaderKey = "sort particles";
        shaderStorageRef.NewCompositeShader(shaderKey);
        AssembleProgramHeader(shaderKey);
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ParticleCollisions/Buffers/ParticleBuffer.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ParticleCollisions/Buffers/ParticleSortingDataBuffer.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ParticleCollisions/SortParticles.comp");
        shaderStorageRef.CompileCompositeShader(shaderKey, GL_COMPUTE_SHADER);
        shaderStorageRef.LinkShader(shaderKey);
        _programIdSortParticles = shaderStorageRef.GetShaderProgram(shaderKey);
    }

    /*--------------------------------------------------------------------------------------------
    Description:
        Assembles headers, buffers, and functional .comp files for the shader that modifies the 
        data over which particles were sorted so that every entry is unique.  This will prevent 
        depth spikes in the binary radix tree.

        Note: As mentioned in GuaranteeSortingDataUniqueness.comp, this shader CANNOT be 
        summoned before the sorting is done.
    Parameters: None
    Returns:    None
    Creator:    John Cox, 5/2017
    --------------------------------------------------------------------------------------------*/
    void ParticleCollisions::AssembleProgramGuaranteeSortingDataUniqueness() 
    {
        ShaderStorage &shaderStorageRef = ShaderStorage::GetInstance();

        std::string shaderKey = "guarantee sorting data uniqueness";
        shaderStorageRef.NewCompositeShader(shaderKey);
        AssembleProgramHeader(shaderKey);
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ParticleCollisions/Buffers/ParticleSortingDataBuffer.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ParticleCollisions/GuaranteeSortingDataUniqueness.comp");
        shaderStorageRef.CompileCompositeShader(shaderKey, GL_COMPUTE_SHADER);
        shaderStorageRef.LinkShader(shaderKey);
        _programIdGuaranteeSortingDataUniqueness = shaderStorageRef.GetShaderProgram(shaderKey);
    }

    /*--------------------------------------------------------------------------------------------
    Description:
        Assembles headers, buffers, and functional .comp files for the shader that generates the 
        borders for each particle's bounding box.

        Note: Can be run at the same time as GenerateBinaryRadixTree.comp.  They write different 
        data, so they can be dispatched without a glMemoryBarrier(...) in between them. 
        
        (??verify??)

    Parameters: None
    Returns:    None
    Creator:    John Cox, 5/2017
    --------------------------------------------------------------------------------------------*/
    void ParticleCollisions::AssembleProgramGenerateLeafNodeBoundingBoxes()
    {
        ShaderStorage &shaderStorageRef = ShaderStorage::GetInstance();

        std::string shaderKey = "generate leaf node bounding boxes";
        shaderStorageRef.NewCompositeShader(shaderKey);
        AssembleProgramHeader(shaderKey);
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ParticleCollisions/Buffers/ParticleBuffer.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ParticleCollisions/Buffers/ParticlePropertiesBuffer.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ParticleCollisions/Buffers/BvhNodeBuffer.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ParticleCollisions/GenerateLeafNodeBoundingBoxes.comp");
        shaderStorageRef.CompileCompositeShader(shaderKey, GL_COMPUTE_SHADER);
        shaderStorageRef.LinkShader(shaderKey);
        _programIdGenerateLeafNodeBoundingBoxes = shaderStorageRef.GetShaderProgram(shaderKey);
    }

    /*--------------------------------------------------------------------------------------------
    Description:
        Assembles headers, buffers, and functional .comp files for the shader that generates the 
        binary radix tree.

        Note: Can be run at the same time as GenerateLeafNodeBoundingBoxes.comp.  Both are 
        writing to leaf nodes, but they are writing different data, so one can be dispatched 
        right after the other without the need for a memory barrier.
    Parameters: None
    Returns:    None
    Creator:    John Cox, 5/2017
    --------------------------------------------------------------------------------------------*/
    void ParticleCollisions::AssembleProgramGenerateBinaryRadixTree()
    {
        ShaderStorage &shaderStorageRef = ShaderStorage::GetInstance();

        std::string shaderKey = "generate binary radix tree";
        shaderStorageRef.NewCompositeShader(shaderKey);
        AssembleProgramHeader(shaderKey);
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ParticleCollisions/Buffers/v.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ParticleCollisions/Buffers/BvhNodeBuffer.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ParticleCollisions/GenerateBinaryRadixTree.comp");
        shaderStorageRef.CompileCompositeShader(shaderKey, GL_COMPUTE_SHADER);
        shaderStorageRef.LinkShader(shaderKey);
        _programIdGenerateBinaryRadixTree = shaderStorageRef.GetShaderProgram(shaderKey);
    }

    /*--------------------------------------------------------------------------------------------
    Description:
        Assembles headers, buffers, and functional .comp files for the shader that takes the 
        bounding boxes the were made in GenerateLeafNodeBoundingBoxes.comp and merges them up 
        the tree to the root.  This takes a tree with bounding volumes at the leaves and 
        generates a bounding volume hierarchy.  All the previous shaders' sorting and tree 
        generation and supporting duties were to get to this point.
    Parameters: None
    Returns:    None
    Creator:    John Cox, 5/2017
    --------------------------------------------------------------------------------------------*/
    void ParticleCollisions::AssembleProgramMergeBoundingVolumes()
    {
        ShaderStorage &shaderStorageRef = ShaderStorage::GetInstance();

        std::string shaderKey = "merge bounding volumes";
        shaderStorageRef.NewCompositeShader(shaderKey);
        AssembleProgramHeader(shaderKey);
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ParticleCollisions/Buffers/BvhNodeBuffer.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ParticleCollisions/MergeBoundingBoxes.comp");
        shaderStorageRef.CompileCompositeShader(shaderKey, GL_COMPUTE_SHADER);
        shaderStorageRef.LinkShader(shaderKey);
        _programIdMergeBoundingVolumes = shaderStorageRef.GetShaderProgram(shaderKey);
    }

    /*--------------------------------------------------------------------------------------------
    Description:
        This method governs the shader dispatches that will result in sorting the ParticleBuffer.
    Parameters: None
    Returns:    None
    Creator:    John Cox, 5/2017
    --------------------------------------------------------------------------------------------*/
    void ParticleCollisions::SortParticlesWithoutProfiling() const
    {
        unsigned int numItemsInPrefixScanBuffer = _prefixSumSsbo.NumDataEntries();

        // for PrefixScan.comp, which works on 2 items per thread
        int numWorkGroupsXForPrefixSum = numItemsInPrefixScanBuffer / PARALLEL_SORT_ITEMS_PER_WORK_GROUP;
        int remainder = numItemsInPrefixScanBuffer % PARALLEL_SORT_ITEMS_PER_WORK_GROUP;
        numWorkGroupsXForPrefixSum += (remainder == 0) ? 0 : 1;

        // for other shaders, which work on 1 item per thread
        int numWorkGroupsXForOther = numItemsInPrefixScanBuffer / PARALLEL_SORT_WORK_GROUP_SIZE_X;
        remainder = numItemsInPrefixScanBuffer % PARALLEL_SORT_WORK_GROUP_SIZE_X;
        numWorkGroupsXForOther += (remainder == 0) ? 0 : 1;

        // working on a 1D array (X dimension), so these are always 1
        int numWorkGroupsY = 1;
        int numWorkGroupsZ = 1;

        glUseProgram(_programIdCopyParticlesToCopyBuffer);
        glDispatchCompute(numWorkGroupsXForOther, numWorkGroupsY, numWorkGroupsZ);
        //??need a memory barrier here??
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        glUseProgram(_programIdGenerateSortingData);
        glDispatchCompute(numWorkGroupsXForOther, numWorkGroupsY, numWorkGroupsZ);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        // the sorting data are Morton Codes, which are 30 bits
        bool writeToSecondBuffer = true;
        unsigned int sortingDataReadBufferOffset = 0;
        unsigned int sortingDataWriteBufferOffset = 0;
        for (unsigned int bitNumber = 0; bitNumber < 30; bitNumber++)
        {
            sortingDataReadBufferOffset = static_cast<unsigned int>(!writeToSecondBuffer) * numItemsInPrefixScanBuffer;
            sortingDataWriteBufferOffset = static_cast<unsigned int>(writeToSecondBuffer) * numItemsInPrefixScanBuffer;

            glUseProgram(_programIdClearWorkGroupSums);
            glDispatchCompute(numWorkGroupsXForOther, numWorkGroupsY, numWorkGroupsZ);
            //??need a memory barrier here??
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

            glUseProgram(_programIdGetBitForPrefixScan);
            glUniform1ui(UNIFORM_LOCATION_PARTICLE_SORTING_DATA_BUFFER_READ_OFFSET, sortingDataReadBufferOffset);
            glUniform1ui(UNIFORM_LOCATION_PARTICLE_SORTING_DATA_BUFFER_WRITE_OFFSET, sortingDataWriteBufferOffset);
            glDispatchCompute(numWorkGroupsXForOther, numWorkGroupsY, numWorkGroupsZ);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

            glUseProgram(_programIdPrefixScanOverAllData);
            glDispatchCompute(numWorkGroupsXForPrefixSum, numWorkGroupsY, numWorkGroupsZ);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

            glUseProgram(_programIdPrefixScanOverWorkGroupSums);
            glDispatchCompute(numWorkGroupsXForPrefixSum, numWorkGroupsY, numWorkGroupsZ);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

            glUseProgram(_programIdSortSortingDataWithPrefixSums);
            glUniform1ui(UNIFORM_LOCATION_PARTICLE_SORTING_DATA_BUFFER_READ_OFFSET, sortingDataReadBufferOffset);
            glUniform1ui(UNIFORM_LOCATION_PARTICLE_SORTING_DATA_BUFFER_WRITE_OFFSET, sortingDataWriteBufferOffset);
            glDispatchCompute(numWorkGroupsXForOther, numWorkGroupsY, numWorkGroupsZ);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

            // swap read/write buffers and do it again
            writeToSecondBuffer = !writeToSecondBuffer;
        }
        
        glUseProgram(_programIdSortParticles);
        glUniform1ui(UNIFORM_LOCATION_PARTICLE_SORTING_DATA_BUFFER_READ_OFFSET, sortingDataReadBufferOffset);
        glUniform1ui(UNIFORM_LOCATION_PARTICLE_SORTING_DATA_BUFFER_WRITE_OFFSET, sortingDataWriteBufferOffset);
        glDispatchCompute(numWorkGroupsXForOther, numWorkGroupsY, numWorkGroupsZ);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        // all done
        glUseProgram(0);
    }

    void ParticleCollisions::SortParticlesWithProfiling() const
    {

    }

    void ParticleCollisions::GenerateBvhWithoutProfiling() const
    {

    }

    void ParticleCollisions::GenerateBvhWithProfiling() const
    {

    }

    void ParticleCollisions::DetectAndResolveCollisionsWithoutProfiling() const
    {

    }

    void ParticleCollisions::DetectAndResolveCollisionsWithProfiling() const
    {

    }
}
