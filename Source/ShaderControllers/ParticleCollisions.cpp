#include "Include/ShaderControllers/ParticleCollisions.h"

#include "Shaders/ShaderStorage.h"
#include "ThirdParty/glload/include/glload/gl_4_4.h"

// for profiling and checking results
#include "Include/ShaderControllers/ProfilingWaitToFinish.h"
#include "Include/Buffers/ParticleSortingData.h"
#include "Include/Buffers/BvhNode.h"

#include "Include/Buffers/Particle.h"


#include "Shaders/ShaderHeaders/ComputeShaderWorkGroupSizes.comp"
#include "Shaders/ShaderHeaders/CrossShaderUniformLocations.comp"

#include <chrono>
#include <fstream>
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
        _particleSortingDataSsbo.ConfigureConstantUniforms(_programIdGetBitForPrefixScan);
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
        printf("");
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

    ///*--------------------------------------------------------------------------------------------
    //Description:
    //    (1) Constructs a binary radix tree using an algorithm that generates all internal nodes 
    //    on one pass.  
    //    (2) Then constructs the bounding volumes for each node in the tree from the leaves to 
    //    the root.
    //    (3) Each particle navigates the tree from the root and collects a list of all particles 
    //    whose bounding boxes overlap, then calculates a collision between the most overlapped 
    //    particles.
    //Parameters: None
    //Returns:    None
    //Creator:    John Cox, 5/2017
    //--------------------------------------------------------------------------------------------*/
    //void ParticleCollisions::DetectAndResolveWithoutProfiling(unsigned int numActiveParticles) const
    //{
    //    //int numWorkGroupsX = (_numLeaves / WORK_GROUP_SIZE_X) + 1;
    //    //int numWorkGroupsY = 1;
    //    //int numWorkGroupsZ = 1;

    //    //// populate leaves with the particles' Morton Codes
    //    //glUseProgram(_populateLeavesWithDataProgramId);
    //    //glUniform1ui(UNIFORM_LOCATION_NUMBER_ACTIVE_PARTICLES, numActiveParticles);
    //    //glDispatchCompute(numWorkGroupsX, numWorkGroupsY, numWorkGroupsZ);
    //    //glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    //    //// construct the hierarchy
    //    //glUseProgram(_generateBinaryRadixTreeProgramId);
    //    //glUniform1ui(UNIFORM_LOCATION_NUMBER_ACTIVE_PARTICLES, numActiveParticles);
    //    //glDispatchCompute(numWorkGroupsX, numWorkGroupsY, numWorkGroupsZ);
    //    //glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    //    //// merge the bounding boxes of individual leaves (particles) up to the root
    //    //glUseProgram(_generateBoundingVolumesProgramId);
    //    //glUniform1ui(UNIFORM_LOCATION_NUMBER_ACTIVE_PARTICLES, numActiveParticles);
    //    //glDispatchCompute(numWorkGroupsX, numWorkGroupsY, numWorkGroupsZ);
    //    //glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    //    //// generate BVH vertices (optional)
    //    //glUseProgram(_generateVerticesProgramId);
    //    //glUniform1ui(UNIFORM_LOCATION_NUMBER_ACTIVE_PARTICLES, numActiveParticles);
    //    //glDispatchCompute(numWorkGroupsX, numWorkGroupsY, numWorkGroupsZ);
    //    //glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);

    //    //// traverse the tree and detect collisions
    //    //glUseProgram(_detectCollisionsProgramId);
    //    //glUniform1ui(UNIFORM_LOCATION_NUMBER_ACTIVE_PARTICLES, numActiveParticles);
    //    //glDispatchCompute(numWorkGroupsX, numWorkGroupsY, numWorkGroupsZ);
    //    //glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);

    //    //// resolve any detected collisions
    //    //glUseProgram(_resolveCollisionsProgramId);
    //    //glUniform1ui(UNIFORM_LOCATION_NUMBER_ACTIVE_PARTICLES, numActiveParticles);
    //    //glDispatchCompute(numWorkGroupsX, numWorkGroupsY, numWorkGroupsZ);
    //    //glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);


    //    //// end collision detection and resolution
    //    //glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    //    //glUseProgram(0);
    //}

    /*--------------------------------------------------------------------------------------------
    Description:
        The same BVH generation and traversal algorithms, but with:
        (1) std::chrono calls scattered everywhere
        (2) writing the profiled duration results to stdout and to a tab-delimited text file

        
        TODO: this description
        
    Parameters: None
    Returns:    None
    Creator:    John Cox, 6/2017
    --------------------------------------------------------------------------------------------*/
    void ParticleCollisions::DetectAndResolve(bool withProfiling) const
    {
        // most shaders work on 1 item per thread
        int numWorkGroupsX = _numParticles / WORK_GROUP_SIZE_X;
        int remainder = _numParticles % WORK_GROUP_SIZE_X;
        numWorkGroupsX += (remainder == 0) ? 0 : 1;

        // the prefix scan works on 2 items per thread
        // Note: See description of PrefixScanSsbo for why the prefix scan algorithm needs its 
        // own work group size calculation.
        unsigned int numItemsInPrefixScanBuffer = _prefixSumSsbo.NumDataEntries();
        int numWorkGroupsXForPrefixSum = numItemsInPrefixScanBuffer / PREFIX_SCAN_ITEMS_PER_WORK_GROUP;
        remainder = numItemsInPrefixScanBuffer % PREFIX_SCAN_ITEMS_PER_WORK_GROUP;
        numWorkGroupsXForPrefixSum += (remainder == 0) ? 0 : 1;

        if (withProfiling)
        {
            SortParticlesWithProfiling(numWorkGroupsX, numWorkGroupsXForPrefixSum);
        }
        else
        {
            SortParticlesWithoutProfiling(numWorkGroupsX, numWorkGroupsXForPrefixSum);
        }




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

        //int numWorkGroupsX = (_numLeaves / WORK_GROUP_SIZE_X) + 1;
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
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ParticleBuffer.comp");
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
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ParticleRegionBoundaries.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/PositionToMortonCode.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ParticleBuffer.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ParticleCollisions/Buffers/ParticleSortingDataBuffer.comp");
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
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ParticleBuffer.comp");
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
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ParticleBuffer.comp");
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
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ParticleCollisions/Buffers/ParticleSortingDataBuffer.comp");
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
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ParticleCollisions/MergeBoundingVolumes.comp");
        shaderStorageRef.CompileCompositeShader(shaderKey, GL_COMPUTE_SHADER);
        shaderStorageRef.LinkShader(shaderKey);
        _programIdMergeBoundingVolumes = shaderStorageRef.GetShaderProgram(shaderKey);
    }

    /*--------------------------------------------------------------------------------------------
    Description:
        This method governs the shader dispatches that will result in sorting the ParticleBuffer.
    Parameters: 
        numWorkGroupsX  Expected to be the total particle count divided by work group size.
        numWorkGroupsXPrefixScan    See comment where this value was calculated.
    Returns:    None
    Creator:    John Cox, 6/2017
    --------------------------------------------------------------------------------------------*/
    void ParticleCollisions::SortParticlesWithoutProfiling(unsigned int numWorkGroupsX, unsigned int numWorkGroupsXPrefixScan) const
    {
        PrepareToSortParticles(numWorkGroupsX);

        // parallel radix sorting algorithm over each bit of the Morton Codes 
        // Note: MUST sort over all 32 bits in GLSL's uint.  See GenerateSortingData.comp for 
        // more detail, but the gist is that the sorting data for inactive particles is 
        // 0xC0000000 for generating the BVH tree.  That is 2x 1s in the 30th and 31st bit, and 
        // the least significant 30bits are 0s.  Sorting these inactive particles to the back 
        // therefore requires sorting over all 32 bits (actually, I think that I could get away 
        // with sorting 31 bits..??should I??)
        unsigned int totalBitCount = 32;

        bool writeToSecondBuffer = true;
        unsigned int sortingDataReadBufferOffset = 0;
        unsigned int sortingDataWriteBufferOffset = 0;
        for (unsigned int bitNumber = 0; bitNumber < totalBitCount; bitNumber++)
        {
            sortingDataReadBufferOffset = static_cast<unsigned int>(!writeToSecondBuffer) * _numParticles;
            sortingDataWriteBufferOffset = static_cast<unsigned int>(writeToSecondBuffer) * _numParticles;

            PrepareForPrefixScan(bitNumber, sortingDataReadBufferOffset);
            PrefixScanOverParticleSortingData(numWorkGroupsXPrefixScan);
            SortSortingDataWithPrefixScan(numWorkGroupsX, bitNumber, sortingDataReadBufferOffset, sortingDataWriteBufferOffset);

            // swap read/write buffers and do it again
            writeToSecondBuffer = !writeToSecondBuffer;
        }

        // the sorting data's final location is in the "write" half of the sorting data buffer
        SortParticlesWithSortedData(numWorkGroupsX, sortingDataWriteBufferOffset);

        // all done
        glUseProgram(0);
    }

    /*--------------------------------------------------------------------------------------------
    Description:
        Like SortParticlesWithoutProfiling(...), but with 
        (1) std::chrono calls 
        (2) forced wait for shader to finish so that the std::chrono calls get an accurate 
            reading for how long the shader takes 
        (3) writing the output to a file (if desired)
    Parameters: 
        numWorkGroupsX  Expected to be the total particle count divided by work group size.
        numWorkGroupsXPrefixScan    See comment where this value was calculated.
    Returns:    None
    Creator:    John Cox, 6/2017
    --------------------------------------------------------------------------------------------*/
    void ParticleCollisions::SortParticlesWithProfiling(unsigned int numWorkGroupsX, unsigned int numWorkGroupsXPrefixScan) const
    {
        cout << "sorting " << _numParticles << " particles" << endl;
        unsigned int totalBitCount = 32;

        // for profiling
        using namespace std::chrono;
        steady_clock::time_point start;
        steady_clock::time_point end;
        long long durationPrepareToSort = 0;
        long long durationParticleSort = 0;
        long long durationSortVerification = 0;
        std::vector<long long> durationsPrepareForPrefixScan(totalBitCount);
        std::vector<long long> durationsPrefixScan(totalBitCount);
        std::vector<long long> durationsSortSortingData(totalBitCount);

        start = high_resolution_clock::now();
        PrepareToSortParticles(numWorkGroupsX);
        WaitForComputeToFinish();
        end = high_resolution_clock::now();
        durationPrepareToSort = duration_cast<microseconds>(end - start).count();

        bool writeToSecondBuffer = true;
        unsigned int sortingDataReadBufferOffset = 0;
        unsigned int sortingDataWriteBufferOffset = 0;
        std::vector<int> checkPrefixScan(_prefixSumSsbo.TotalBufferEntries());
        for (unsigned int bitNumber = 0; bitNumber < totalBitCount; bitNumber++)
        {
            sortingDataReadBufferOffset = static_cast<unsigned int>(!writeToSecondBuffer) * _numParticles;
            sortingDataWriteBufferOffset = static_cast<unsigned int>(writeToSecondBuffer) * _numParticles;

            unsigned int startingIndex = 0;
            unsigned int bufferSizeBytes = checkPrefixScan.size() * sizeof(int);
            void *bufferPtr = nullptr;

            start = high_resolution_clock::now();
            PrepareForPrefixScan(bitNumber, sortingDataReadBufferOffset);
            WaitForComputeToFinish();
            end = high_resolution_clock::now();
            durationsPrepareForPrefixScan[bitNumber] = duration_cast<microseconds>(end - start).count();

            start = high_resolution_clock::now();
            PrefixScanOverParticleSortingData(numWorkGroupsXPrefixScan);
            WaitForComputeToFinish();
            end = high_resolution_clock::now();
            durationsPrefixScan[bitNumber] = duration_cast<microseconds>(end - start).count();

            start = high_resolution_clock::now();
            SortSortingDataWithPrefixScan(numWorkGroupsX, bitNumber, sortingDataReadBufferOffset, sortingDataWriteBufferOffset);
            WaitForComputeToFinish();
            end = high_resolution_clock::now();
            durationsSortSortingData[bitNumber] = duration_cast<microseconds>(end - start).count();

            // swap read/write buffers and do it again
            writeToSecondBuffer = !writeToSecondBuffer;
        }

        // wherever the sorting data ended up, that is where the shader should read from
        start = high_resolution_clock::now();
        SortParticlesWithSortedData(numWorkGroupsX, sortingDataWriteBufferOffset);
        WaitForComputeToFinish();
        end = high_resolution_clock::now();
        durationParticleSort = duration_cast<microseconds>(end - start).count();

        // verify sorted data
        // Note: Only need to copy the first half of the buffer.  This is where the last loop of 
        // the radix sorting algorithm put the sorting data.
        start = high_resolution_clock::now();
        unsigned int startingIndex = sortingDataWriteBufferOffset;
        std::vector<ParticleSortingData> checkSortingData(_particleSortingDataSsbo.NumItems());
        unsigned int bufferSizeBytes = checkSortingData.size() * sizeof(ParticleSortingData);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, _particleSortingDataSsbo.BufferId());
        void *bufferPtr = glMapBufferRange(GL_SHADER_STORAGE_BUFFER, startingIndex, bufferSizeBytes, GL_MAP_READ_BIT);
        memcpy(checkSortingData.data(), bufferPtr, bufferSizeBytes);
        glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
        for (unsigned int i = 1; i < checkSortingData.size(); i++)
        {
            unsigned int thisIndex = i;
            unsigned int prevIndex = i - 1;
            unsigned int val = checkSortingData[thisIndex]._sortingData;
            unsigned int prevVal = checkSortingData[prevIndex]._sortingData;

            // the original data is 0 - N-1, 1 value at a time, so it's ok to hard code 
            if (val < prevVal)
            {
                printf("value %u at index %u is >= previous value %u and index %u\n", val, i, prevVal, i - 1);
            }
        }

        end = high_resolution_clock::now();
        durationSortVerification = duration_cast<microseconds>(end - start).count();


        // report results
        // Note: Write the results to a tab-delimited text file so that I can dump them into an 
        // Excel spreadsheet.
        std::ofstream outFile("ParallelSortDurations.txt");
        if (outFile.is_open())
        {
            long long totalSortingTime = durationPrepareToSort + durationParticleSort;
            for (unsigned int bitCounter = 0; bitCounter < totalBitCount; bitCounter++)
            {
                totalSortingTime += durationsPrepareForPrefixScan[bitCounter];
                totalSortingTime += durationsPrefixScan[bitCounter];
                totalSortingTime += durationsSortSortingData[bitCounter];
            }

            cout << "total sorting time: " << totalSortingTime << "\tmicroseconds" << endl;
            outFile << "total sorting time: " << totalSortingTime << "\tmicroseconds" << endl;

            cout << "preparation time: " << durationPrepareToSort << "\tmicroseconds" << endl;
            outFile << "preparation time: " << durationPrepareToSort << "\tmicroseconds" << endl;

            cout << "move particles to sorted positions: " << durationParticleSort << "\tmicroseconds" << endl;
            outFile << "move particles to sorted positions: " << durationParticleSort << "\tmicroseconds" << endl;

            cout << endl << "prepare for prefix scan:" << endl;
            outFile << endl << "prepare for prefix scan:" << endl;
            for (size_t i = 0; i < durationsPrepareForPrefixScan.size(); i++)
            {
                cout << "\t" << durationsPrepareForPrefixScan[i] << "\tmicroseconds" << endl;
                outFile << "\t" << durationsPrepareForPrefixScan[i] << "\tmicroseconds" << endl;
            }

            cout << endl << "prefix scan:" << endl;
            outFile << endl << "prefix scan:" << endl;
            for (size_t i = 0; i < durationsPrefixScan.size(); i++)
            {
                cout << "\t" << durationsPrefixScan[i] << "\tmicroseconds" << endl;
                outFile << "\t" << durationsPrefixScan[i] << "\tmicroseconds" << endl;
            }

            cout << endl << "sort sorting data:" << endl;
            outFile << endl << "sort sorting data:" << endl;
            for (size_t i = 0; i < durationsSortSortingData.size(); i++)
            {
                cout << "\t" << durationsSortSortingData[i] << "\tmicroseconds" << endl;
                outFile << "\t" << durationsSortSortingData[i] << "\tmicroseconds" << endl;
            }
        }
        outFile.close();

        // all done
        glUseProgram(0);
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




    void ParticleCollisions::PrepareToSortParticles(unsigned int numWorkGroupsX) const
    {
        glUseProgram(_programIdCopyParticlesToCopyBuffer);
        glDispatchCompute(numWorkGroupsX, 1, 1);
        //??need a memory barrier here??
        //glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        glUseProgram(_programIdGenerateSortingData);
        glDispatchCompute(numWorkGroupsX, 1, 1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    }

    void ParticleCollisions::PrepareForPrefixScan(unsigned int bitNumber, unsigned int sortingDataReadOffset) const
    {
        // Note: The number of work groups for this sorting stage need special handling.  Most shaders operate on 1 item per thread and will require ("num particles" / "work group size X") + 1 work groups to give every data entry (particle, particle sorting, etc.) a thread.  The prefix sum algorithm operates on 2 items per thread and needs special handling (read description block of PrefixSumSsbo for details).  This stage is neither.  Clearing out the work group sums operates on 2 items per thread and requires 1, and exactly 1, work groups.  Getting bits for the prefix sum operates on 1 item per thread and is a function of the size of the prefix scan array.  Deal with these special cases manually.

        // Note: The "work group sums" array is the size of a single work group * 2.  This shader
        // should only be dispatched with a single work group.
        glUseProgram(_programIdClearWorkGroupSums);
        glDispatchCompute(1, 1, 1);
        //??need a memory barrier here??
        //glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        int numWorkGroupsX = _prefixSumSsbo.NumDataEntries() / WORK_GROUP_SIZE_X;
        int remainder = _prefixSumSsbo.NumDataEntries() % WORK_GROUP_SIZE_X;
        numWorkGroupsX += (remainder == 0) ? 0 : 1;
        glUseProgram(_programIdGetBitForPrefixScan);
        glUniform1ui(UNIFORM_LOCATION_PARTICLE_SORTING_DATA_BUFFER_READ_OFFSET, sortingDataReadOffset);
        glUniform1ui(UNIFORM_LOCATION_BIT_NUMBER, bitNumber);
        glDispatchCompute(numWorkGroupsX, 1, 1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    }

    void ParticleCollisions::PrefixScanOverParticleSortingData(unsigned int numWorkGroupsX) const
    {
        glUseProgram(_programIdPrefixScanOverAllData);
        glDispatchCompute(numWorkGroupsX, 1, 1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        // this stage is designed to work with 1 work group, and exactly 1 work group
        // Note: See PrefixScanBuffer.comp description for details on this second stage.
        glUseProgram(_programIdPrefixScanOverWorkGroupSums);
        glDispatchCompute(1, 1, 1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    }

    void ParticleCollisions::SortSortingDataWithPrefixScan(unsigned int numWorkGroupsX, unsigned int bitNumber, unsigned int sortingDataReadOffset, unsigned int sortingDataWriteOffset) const
    {
        glUseProgram(_programIdSortSortingDataWithPrefixSums);
        glUniform1ui(UNIFORM_LOCATION_PARTICLE_SORTING_DATA_BUFFER_READ_OFFSET, sortingDataReadOffset);
        glUniform1ui(UNIFORM_LOCATION_PARTICLE_SORTING_DATA_BUFFER_WRITE_OFFSET, sortingDataWriteOffset);
        glUniform1ui(UNIFORM_LOCATION_BIT_NUMBER, bitNumber);
        glDispatchCompute(numWorkGroupsX, 1, 1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    }

    void ParticleCollisions::SortParticlesWithSortedData(unsigned int numWorkGroupsX, unsigned int sortingDataReadOffset) const
    {
        glUseProgram(_programIdSortParticles);
        glUniform1ui(UNIFORM_LOCATION_PARTICLE_SORTING_DATA_BUFFER_READ_OFFSET, sortingDataReadOffset);
        glDispatchCompute(numWorkGroupsX, 1, 1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    }

    void ParticleCollisions::ConstructBvh(unsigned int numWorkGroupsX) const
    {

    }

}
