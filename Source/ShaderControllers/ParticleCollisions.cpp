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


void GenerateTestBvh(std::vector<BvhNode> &bvh)
{
    bvh.clear();

    int numLeaves = 16;
    for (int leafCounter = 0; leafCounter < numLeaves; leafCounter++)
    {
        bvh.push_back(BvhNode());
        bvh[leafCounter]._isLeaf = true;
    }
    
    for (int  internalNodeCounter = 0; internalNodeCounter < numLeaves - 1; internalNodeCounter++)
    {
        bvh.push_back(BvhNode());
    }

    //// Test BVH #1
    //bvh[0]._data = 1;
    //bvh[1]._data = 2;
    //bvh[2]._data = 3;
    //bvh[3]._data = 9;
    //bvh[4]._data = 9;
    //bvh[5]._data = 14;
    //bvh[6]._data = 15;
    //bvh[7]._data = 16;
    //bvh[8]._data = 17;
    //bvh[9]._data = 18;
    //bvh[10]._data = 20;
    //bvh[11]._data = 22;
    //bvh[12]._data = 22;
    //bvh[13]._data = 26;
    //bvh[14]._data = 26;
    //bvh[15]._data = 28;

    // Test BVH #2
    bvh[0]._data = 1271858;
    bvh[1]._data = 34211986;
    bvh[2]._data = 47408388;
    bvh[3]._data = 75516948;
    bvh[4]._data = 114443670;
    bvh[5]._data = 276973568;
    bvh[6]._data = 306777138;
    bvh[7]._data = 345188406;
    bvh[8]._data = 538667040;
    bvh[9]._data = 549996564;
    bvh[10]._data = 575677734;
    bvh[11]._data = 584191158;
    bvh[12]._data = 637668368;
    bvh[13]._data = 643326102;
    bvh[14]._data = 806428982;
    bvh[15]._data = 815474724;
}




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
        _populateLeavesWithDataProgramId(0),
        _generateBinaryRadixTreeProgramId(0),
        _generateBoundingVolumesProgramId(0),
        _generateVerticesProgramId(0),
        _detectAndResolveCollisionsProgramId(0),
        _bvhNodeSsbo(nullptr),
        _bvhGeometrySsbo(nullptr)
    {
        ShaderStorage &shaderStorageRef = ShaderStorage::GetInstance();
        std::string shaderKey;

        // populate leaf nodes with the particles' Morton Codes
        shaderKey = "populate leaf nodes with data";
        shaderStorageRef.NewCompositeShader(shaderKey);
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/ShaderHeaders/Version.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/ShaderHeaders/ComputeShaderWorkGroupSizes.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/ShaderHeaders/SsboBufferBindings.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/ShaderHeaders/CrossShaderUniformLocations.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ParticleBuffer.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ParticleCollisions/BvhNodeBuffer.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ParticleCollisions/PopulateLeafNodesWithData.comp");
        shaderStorageRef.CompileCompositeShader(shaderKey, GL_COMPUTE_SHADER);
        shaderStorageRef.LinkShader(shaderKey);
        _populateLeavesWithDataProgramId = shaderStorageRef.GetShaderProgram(shaderKey);

        // generate all the tree's internal nodes
        shaderKey = "generate binary radix tree";
        shaderStorageRef.NewCompositeShader(shaderKey);
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/ShaderHeaders/Version.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/ShaderHeaders/ComputeShaderWorkGroupSizes.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/ShaderHeaders/SsboBufferBindings.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/ShaderHeaders/CrossShaderUniformLocations.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ParticleBuffer.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ParticleCollisions/BvhNodeBuffer.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ParticleCollisions/GenerateBinaryRadixTree.comp");
        shaderStorageRef.CompileCompositeShader(shaderKey, GL_COMPUTE_SHADER);
        shaderStorageRef.LinkShader(shaderKey);
        _generateBinaryRadixTreeProgramId = shaderStorageRef.GetShaderProgram(shaderKey);

        // generate bounding boxes for the leaves, then merge them from the leaves up to the 
        // root, thus finishing the hierarchy of bounding volumes
        shaderKey = "generate bounding volumes";
        shaderStorageRef.NewCompositeShader(shaderKey);
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/ShaderHeaders/Version.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/ShaderHeaders/ComputeShaderWorkGroupSizes.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/ShaderHeaders/SsboBufferBindings.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/ShaderHeaders/CrossShaderUniformLocations.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ParticleBuffer.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ParticleCollisions/BvhNodeBuffer.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ParticleCollisions/GenerateBoundingVolumes.comp");
        shaderStorageRef.CompileCompositeShader(shaderKey, GL_COMPUTE_SHADER);
        shaderStorageRef.LinkShader(shaderKey);
        _generateBoundingVolumesProgramId = shaderStorageRef.GetShaderProgram(shaderKey);

        // generate vertices out of the BVH
        shaderKey = "generate vertices";
        shaderStorageRef.NewCompositeShader(shaderKey);
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/ShaderHeaders/Version.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/ShaderHeaders/ComputeShaderWorkGroupSizes.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/ShaderHeaders/SsboBufferBindings.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/ShaderHeaders/CrossShaderUniformLocations.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ParticleBuffer.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/PolygonBuffer.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ParticleCollisions/BvhNodeBuffer.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ParticleCollisions/GenerateBvhVertices.comp");
        shaderStorageRef.CompileCompositeShader(shaderKey, GL_COMPUTE_SHADER);
        shaderStorageRef.LinkShader(shaderKey);
        _generateVerticesProgramId = shaderStorageRef.GetShaderProgram(shaderKey);

        // have each leaf check through the tree for potential collisions and resolve them
        shaderKey = "detect and resolve collisions";
        shaderStorageRef.NewCompositeShader(shaderKey);
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/ShaderHeaders/Version.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/ShaderHeaders/ComputeShaderWorkGroupSizes.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/ShaderHeaders/SsboBufferBindings.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/ShaderHeaders/CrossShaderUniformLocations.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ParticleBuffer.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ParticleCollisions/BvhNodeBuffer.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ParticleCollisions/DetectAndResolveCollisions.comp");
        shaderStorageRef.CompileCompositeShader(shaderKey, GL_COMPUTE_SHADER);
        shaderStorageRef.LinkShader(shaderKey);
        _detectAndResolveCollisionsProgramId = shaderStorageRef.GetShaderProgram(shaderKey);


        // generate the BVH that will be used for all this collision detection
        _bvhNodeSsbo = std::make_shared<BvhNodeSsbo>(particleSsbo->NumParticles());

        //// test buffer
        //_bvhNodeSsbo = std::make_shared<BvhNodeSsbo>(16);
        //std::vector<BvhNode> testBvh;
        //GenerateTestBvh(testBvh);
        //glBindBuffer(GL_SHADER_STORAGE_BUFFER, _bvhNodeSsbo->BufferId());
        //glBufferData(GL_SHADER_STORAGE_BUFFER, testBvh.size() * sizeof(BvhNode), testBvh.data(), GL_DYNAMIC_DRAW);
        //glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

        // and generate the BVH geometry for the visualization
        // Note: For N particles there are N leaves and N-1 internal nodes in the tree, and each 
        // node's bounding box has 4 faces.  
        unsigned int maxPolygonsInBvh = (_numLeaves + (_numLeaves - 1)) * 4;
        _bvhGeometrySsbo = std::make_shared<PolygonSsbo>(maxPolygonsInBvh);

        // set buffer sizes for each of the programs
        particleSsbo->ConfigureConstantUniforms(_populateLeavesWithDataProgramId);
        particleSsbo->ConfigureConstantUniforms(_generateBinaryRadixTreeProgramId);
        particleSsbo->ConfigureConstantUniforms(_generateBoundingVolumesProgramId);
        particleSsbo->ConfigureConstantUniforms(_detectAndResolveCollisionsProgramId);
        particleSsbo->ConfigureConstantUniforms(_generateVerticesProgramId);
        _bvhNodeSsbo->ConfigureConstantUniforms(_populateLeavesWithDataProgramId);
        _bvhNodeSsbo->ConfigureConstantUniforms(_generateBinaryRadixTreeProgramId);
        _bvhNodeSsbo->ConfigureConstantUniforms(_generateBoundingVolumesProgramId);
        _bvhNodeSsbo->ConfigureConstantUniforms(_detectAndResolveCollisionsProgramId);
        _bvhNodeSsbo->ConfigureConstantUniforms(_generateVerticesProgramId);
        _bvhGeometrySsbo->ConfigureConstantUniforms(_generateVerticesProgramId);
        _bvhGeometrySsbo->ConfigureRender(GL_LINES);
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
        glDeleteProgram(_populateLeavesWithDataProgramId);
        glDeleteProgram(_generateBinaryRadixTreeProgramId);
        glDeleteProgram(_generateBoundingVolumesProgramId);
        glDeleteProgram(_detectAndResolveCollisionsProgramId);
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
        int numWorkGroupsX = (_numLeaves / PARTICLE_OPERATIONS_WORK_GROUP_SIZE_X) + 1;
        int numWorkGroupsY = 1;
        int numWorkGroupsZ = 1;

        // populate leaves with the particles' Morton Codes
        glUseProgram(_populateLeavesWithDataProgramId);
        glUniform1ui(UNIFORM_LOCATION_NUMBER_ACTIVE_PARTICLES, numActiveParticles);
        glDispatchCompute(numWorkGroupsX, numWorkGroupsY, numWorkGroupsZ);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        // construct the hierarchy
        glUseProgram(_generateBinaryRadixTreeProgramId);
        glUniform1ui(UNIFORM_LOCATION_NUMBER_ACTIVE_PARTICLES, numActiveParticles);
        glDispatchCompute(numWorkGroupsX, numWorkGroupsY, numWorkGroupsZ);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        // merge the bounding boxes of individual leaves (particles) up to the root
        glUseProgram(_generateBoundingVolumesProgramId);
        glUniform1ui(UNIFORM_LOCATION_NUMBER_ACTIVE_PARTICLES, numActiveParticles);
        glDispatchCompute(numWorkGroupsX, numWorkGroupsY, numWorkGroupsZ);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        // generate BVH vertices (optional)
        glUseProgram(_generateVerticesProgramId);
        glUniform1ui(UNIFORM_LOCATION_NUMBER_ACTIVE_PARTICLES, numActiveParticles);
        glDispatchCompute(numWorkGroupsX, numWorkGroupsY, numWorkGroupsZ);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);

        // traverse the tree, detect collisions, and resolve them
        glUseProgram(_detectAndResolveCollisionsProgramId);
        glUniform1ui(UNIFORM_LOCATION_NUMBER_ACTIVE_PARTICLES, numActiveParticles);
        glDispatchCompute(numWorkGroupsX, numWorkGroupsY, numWorkGroupsZ);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);

        // end collision detection and resolution
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        glUseProgram(0);
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
        cout << "Generating BVH for " << numActiveParticles << " particle leaves" << endl;

        // for profiling
        using namespace std::chrono;
        steady_clock::time_point generateBvhStart;
        steady_clock::time_point start;
        steady_clock::time_point end;
        long long durationPopulateTreeWithData = 0;
        long long durationGenerateTree = 0;
        long long durationMergeBoundingBoxes = 0;
        long long durationGenerateBvhVertices = 0;
        long long durationDetectAndResolveCollisions = 0;

        // wait for previous instructions to finish
        WaitForComputeToFinish();
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

        // begin
        generateBvhStart = high_resolution_clock::now();

        int numWorkGroupsX = (_numLeaves / PARTICLE_OPERATIONS_WORK_GROUP_SIZE_X) + 1;
        int numWorkGroupsY = 1;
        int numWorkGroupsZ = 1;

        // populate leaves with the particles' Morton Codes
        start = high_resolution_clock::now();
        glUseProgram(_populateLeavesWithDataProgramId);
        glUniform1ui(UNIFORM_LOCATION_NUMBER_ACTIVE_PARTICLES, numActiveParticles);
        glDispatchCompute(numWorkGroupsX, numWorkGroupsY, numWorkGroupsZ);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        WaitForComputeToFinish();
        end = high_resolution_clock::now();
        durationPopulateTreeWithData = duration_cast<microseconds>(end - start).count();

        // construct the hierarchy
        start = high_resolution_clock::now();
        glUseProgram(_generateBinaryRadixTreeProgramId);
        glUniform1ui(UNIFORM_LOCATION_NUMBER_ACTIVE_PARTICLES, numActiveParticles);
        glDispatchCompute(numWorkGroupsX, numWorkGroupsY, numWorkGroupsZ);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        WaitForComputeToFinish();
        end = high_resolution_clock::now();
        durationGenerateTree = duration_cast<microseconds>(end - start).count();

        // merge the bounding boxes of individual leaves (particles) up to the root
        start = high_resolution_clock::now();
        glUseProgram(_generateBoundingVolumesProgramId);
        glUniform1ui(UNIFORM_LOCATION_NUMBER_ACTIVE_PARTICLES, numActiveParticles);
        glDispatchCompute(numWorkGroupsX, numWorkGroupsY, numWorkGroupsZ);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        WaitForComputeToFinish();
        end = high_resolution_clock::now();
        durationMergeBoundingBoxes = duration_cast<microseconds>(end - start).count();

        // generate BVH vertices (optional)
        start = high_resolution_clock::now();
        glUseProgram(_generateVerticesProgramId);
        glUniform1ui(UNIFORM_LOCATION_NUMBER_ACTIVE_PARTICLES, numActiveParticles);
        glDispatchCompute(numWorkGroupsX, numWorkGroupsY, numWorkGroupsZ);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);
        WaitForComputeToFinish();
        end = high_resolution_clock::now();
        durationGenerateBvhVertices = duration_cast<microseconds>(end - start).count();

//        // traverse the tree, detect collisions, and resolve them
//        start = high_resolution_clock::now();
//        glUseProgram(_detectAndResolveCollisionsProgramId);
//        glUniform1ui(UNIFORM_LOCATION_NUMBER_ACTIVE_PARTICLES, numActiveParticles);
//        glDispatchCompute(numWorkGroupsX, numWorkGroupsY, numWorkGroupsZ);
//        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);
//        WaitForComputeToFinish();
//        end = high_resolution_clock::now();
//        durationDetectAndResolveCollisions = duration_cast<microseconds>(end - start).count();


        glUseProgram(0);

        // end 
        steady_clock::time_point generateBvhEnd = high_resolution_clock::now();

        
        unsigned int startingIndex = 0;
        std::vector<BvhNode> checkOriginalData(_bvhNodeSsbo->NumTotalNodes());
        unsigned int bufferSizeBytes = checkOriginalData.size() * sizeof(BvhNode);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, _bvhNodeSsbo->BufferId());
        void *bufferPtr = glMapBufferRange(GL_SHADER_STORAGE_BUFFER, startingIndex, bufferSizeBytes, GL_MAP_READ_BIT);
        memcpy(checkOriginalData.data(), bufferPtr, bufferSizeBytes);
        glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);

        startingIndex = 0;
        std::vector<PolygonFace> checkOriginalPolygonData(_bvhGeometrySsbo->NumItems());
        bufferSizeBytes = checkOriginalPolygonData.size() * sizeof(PolygonFace);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, _bvhGeometrySsbo->BufferId());
        bufferPtr = glMapBufferRange(GL_SHADER_STORAGE_BUFFER, startingIndex, bufferSizeBytes, GL_MAP_READ_BIT);
        memcpy(checkOriginalPolygonData.data(), bufferPtr, bufferSizeBytes);
        glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);

        printf("");

        //int currentParticleCountIndex = checkOriginalData[0]._extraData1 - 1;
        //int prevParticleCountIndex = checkOriginalData[0]._extraData1 - 2;
        //if (currentParticleCountIndex == 0)
        //{
        //    currentParticleCountIndex = 24;
        //}
        //if (currentParticleCountIndex == 0)
        //{
        //    prevParticleCountIndex = 24;
        //}
        //if (checkOriginalData[0]._extraDataArr1[currentParticleCountIndex] == 0 &&
        //    checkOriginalData[0]._extraDataArr1[prevParticleCountIndex] >= 7)
        //{
        //    printf("");
        //}

        //if (checkOriginalData[0]._extraData1 != -1)
        ////if (checkOriginalData[0]._extraData2 == 1977)
        ////if (numActiveParticles > 2000)
        //{
        //    printf("");
        //    std::ofstream outfile("BvhDump.txt");
        //    for (size_t i = 0; i < checkOriginalData.size(); i++)
        //    {
        //        outfile << "i = " << i
        //            << "\tisLeaf = " << checkOriginalData[i]._isLeaf
        //            << "\tparentIndex = " << checkOriginalData[i]._parentIndex
        //            << "\tstartIndex = " << checkOriginalData[i]._startIndex
        //            << "\tendIndex = " << checkOriginalData[i]._endIndex
        //            << "\tsplitIndex = " << checkOriginalData[i]._leafSplitIndex
        //            << "\tleftChildIndex = " << checkOriginalData[i]._leftChildIndex
        //            << "\trightChildIndex = " << checkOriginalData[i]._rightChildIndex
        //            << "\tdata = " << checkOriginalData[i]._data

        //            << "\textraData1 = " << checkOriginalData[i]._extraData1
        //            << "\textraData2 = " << checkOriginalData[i]._extraData2
        //            //<< "\tleft = " << checkOriginalData[i]._boundingBox._left
        //            //<< "\tright = " << checkOriginalData[i]._boundingBox._right
        //            //<< "\tbottom = " << checkOriginalData[i]._boundingBox._bottom
        //            //<< "\ttop = " << checkOriginalData[i]._boundingBox._top
        //            << endl;
        //        outfile << "\t";
        //        for (size_t j = 0; j < 25; j++)
        //        {
        //            outfile << checkOriginalData[i]._extraDataArrI[j] << "->";
        //        }
        //        outfile << endl;
        //        outfile << "\t";
        //        for (size_t j = 0; j < 25; j++)
        //        {
        //            outfile << checkOriginalData[i]._extraDataArrF[j] << "->";
        //        }
        //        outfile << endl;
        //    }
        //    outfile.close();

        //    return;
        //}

        // TODO: remove
        //if (numActiveParticles > 7000)
        //{
        //    std::ofstream outfile("BvhDump.txt");
        //    for (size_t i = 0; i < checkOriginalData.size(); i++)
        //    {
        //        outfile << "i= " << i
        //            << "\tisLeaf= " << checkOriginalData[i]._isLeaf
        //            << "\tparent= " << checkOriginalData[i]._parentIndex
        //            << "\tstart= " << checkOriginalData[i]._startIndex
        //            << "\tend= " << checkOriginalData[i]._endIndex
        //            << "\tsplit= " << checkOriginalData[i]._leafSplitIndex
        //            << "\tleftChild= " << checkOriginalData[i]._leftChildIndex
        //            << "\trightChild= " << checkOriginalData[i]._rightChildIndex
        //            << "\tdata= " << checkOriginalData[i]._data
        //            << "\tthreadEntranceCounter= " << checkOriginalData[i]._threadEntranceCounter
        //            
        //            // TODO: remove debug stuff
        //            << "\textraData1= " << checkOriginalData[i]._extraData1 
        //            << "\textraData2= " << checkOriginalData[i]._extraData2
        //            << "\tthreadId[" << 0 << "]= " << checkOriginalData[i]._threadAccessors[0]._threadId
        //            << "\tpreviously from " << checkOriginalData[i]._threadAccessors[0]._previousNodeIndex
        //            << "\tthreadId[" << 1 << "]= " << checkOriginalData[i]._threadAccessors[1]._threadId
        //            << "\tpreviously from " << checkOriginalData[i]._threadAccessors[1]._previousNodeIndex
        //            << "\tthreadId[" << 2 << "]= " << checkOriginalData[i]._threadAccessors[2]._threadId
        //            << "\tpreviously from " << checkOriginalData[i]._threadAccessors[2]._previousNodeIndex
        //            << "\tthreadId[" << 3 << "]= " << checkOriginalData[i]._threadAccessors[3]._threadId
        //            << "\tpreviously from " << checkOriginalData[i]._threadAccessors[3]._previousNodeIndex
        //            << "\tthreadId[" << 4 << "]= " << checkOriginalData[i]._threadAccessors[4]._threadId
        //            << "\tpreviously from " << checkOriginalData[i]._threadAccessors[4]._previousNodeIndex
        //            << endl;

        //        //for (size_t j = 0; j < 25; j++)
        //        //{
        //        //    outfile << "\tthreadId[" << j << "]=" << checkOriginalData[i]._threadAccessors[j];
        //        //}
        //        
        //        //outfile << endl;
        //    }
        //    outfile.close();
        //}





        //// write the results to stdout and to a text file so that I can dump them into an Excel spreadsheet
        //std::ofstream outFile("GenerateBvhDurations.txt");
        //if (outFile.is_open())
        //{
        //    long long totalCollisionDetectionTime = duration_cast<microseconds>(generateBvhEnd - generateBvhStart).count();
        //    cout << "total collision detection time: " << totalCollisionDetectionTime << "\tmicroseconds" << endl;
        //    outFile << "total collision detection time: " << totalCollisionDetectionTime << "\tmicroseconds" << endl;

        //    cout << "populate leaves with particles' Morton Codes: " << durationPopulateTreeWithData << "\tmicroseconds" << endl;
        //    outFile << "populate leaves with particles' Morton Codes: " << durationPopulateTreeWithData << "\tmicroseconds" << endl;

        //    cout << "generate BVH tree: " << durationGenerateTree << "\tmicroseconds" << endl;
        //    outFile << "generate BVH tree: " << durationGenerateTree << "\tmicroseconds" << endl;

        //    cout << "generate BVH bounding boxes: " << durationMergeBoundingBoxes << "\tmicroseconds" << endl;
        //    outFile << "generate BVH bounding boxes: " << durationMergeBoundingBoxes << "\tmicroseconds" << endl;

        //    cout << "generate BVH vertices: " << durationGenerateBvhVertices << "\tmicroseconds" << endl;
        //    outFile << "generate BVH vertices: " << durationGenerateBvhVertices << "\tmicroseconds" << endl;

        //    cout << "detect and resolve collisions: " << durationDetectAndResolveCollisions << "\tmicroseconds" << endl;
        //    outFile << "detect and resolve collisions: " << durationDetectAndResolveCollisions << "\tmicroseconds" << endl;

        //    cout << endl;
        //    outFile << endl;

        //}
        //outFile.close();
    }


    /*--------------------------------------------------------------------------------------------
    Description:
        used so that the RenderGeometry shader controller can draw this.
    Parameters: None
    Returns:    
        A const copy of a const shared pointer to the SSBO that contains the BVH's vertices.
        That's a mouthful.  ??should this SSBO be a member instead of a pointer to member??
    Creator:    John Cox, 3/2017
    --------------------------------------------------------------------------------------------*/
    const PolygonSsbo::SharedConstPtr ParticleCollisions::BvhVerticesSsbo() const
    {
        return _bvhGeometrySsbo;
    }
}
