#include "Include/ShaderControllers/ParticleCollide.h"

#include <string>

#include "Shaders/ShaderStorage.h"
#include "Shaders/Compute/ComputeHeaders/ComputeShaderWorkGroupSizes.comp"

#include "ThirdParty/glload/include/glload/gl_4_4.h"


namespace ShaderControllers
{
    /*--------------------------------------------------------------------------------------------
    Description:
        Gives members initial values.
        
        Constructs the PaticleCollide compute shader out of the necessary shader pieces.
    Parameters: 
        ssboToWorkWith  The SSBO that will be configured to work with this shader controller.
    Returns:    None
    Creator:    John Cox, 4/2017
    --------------------------------------------------------------------------------------------*/
    ParticleCollide::ParticleCollide(ParticleSsbo::SHARED_PTR &ssboToWorkWith) :
        _totalParticleCount(0),
        _computeProgramId(0),
        _unifLocIndexOffsetBy0Or1(-1)
    {
        _totalParticleCount = ssboToWorkWith->NumVertices();

        // construct the compute shader
        ShaderStorage &shaderStorageRef = ShaderStorage::GetInstance();

        std::string shaderKey = "particle collisions";
        shaderStorageRef.NewCompositeShader(shaderKey);
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ComputeHeaders/Version.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ComputeHeaders/ComputeShaderWorkGroupSizes.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ComputeHeaders/SsboBufferBindings.comp"); 
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ComputeHeaders/CrossShaderUniformLocations.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ParticleBuffer.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/Compute/ParticleCollisions/ParticleCollisions.comp");
        shaderStorageRef.CompileCompositeShader(shaderKey, GL_COMPUTE_SHADER);
        shaderStorageRef.LinkShader(shaderKey);
        _computeProgramId = shaderStorageRef.GetShaderProgram(shaderKey);
        ssboToWorkWith->ConfigureConstantUniforms(_computeProgramId);

        _unifLocIndexOffsetBy0Or1 = shaderStorageRef.GetUniformLocation(shaderKey, "uIndexOffsetBy0Or1");
    }   

    /*--------------------------------------------------------------------------------------------
    Description:
        Cleans up the shader program that was created for this shader controller.
    Parameters: None
    Returns:    None
    Creator:    John Cox, 4/2017
    --------------------------------------------------------------------------------------------*/
    ParticleCollide::~ParticleCollide()
    {
        glDeleteProgram(_computeProgramId);
    }

    /*--------------------------------------------------------------------------------------------
    Description:
        Runs the collision handling compute shader over all particles.  

        The compute shader will check for collisions and resolve them given the particles' 
        current positions and velocities.  Inactive particles are ignored.
        
        This can be called before or after the ParticleUpdate compute shader controller runs, 
        though I think that it makes more sense to run it afterwards.  
    Parameters: None
    Returns:    None
    Creator:    John Cox, 4/2017
    --------------------------------------------------------------------------------------------*/
    void ParticleCollide::DetectAndResolveCollisions()
    {
        // let particle collision detection and resolution occur in pairs (the collision 
        // resolution math is intended for pairs anyway)
        // Note: Add 1 in case there are an odd number of particles.  I want to make sure that 
        // all particles are covered.
        unsigned int halfParticleCount = (_totalParticleCount / 2) + 1;
        GLuint numWorkGroupsX = (halfParticleCount / PARTICLE_OPERATIONS_WORK_GROUP_SIZE_X) + 1;
        GLuint numWorkGroupsY = 1;
        GLuint numWorkGroupsZ = 1;

        glUseProgram(_computeProgramId);

        // see explanation of why this is launched twice in ParticleCollisions.comp in the 
        // comment block for uIndexOffsetBy0Or1
        glUniform1ui(_unifLocIndexOffsetBy0Or1, 0);
        glDispatchCompute(numWorkGroupsX, numWorkGroupsY, numWorkGroupsZ);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);

        glUniform1ui(_unifLocIndexOffsetBy0Or1, 1);
        glDispatchCompute(numWorkGroupsX, numWorkGroupsY, numWorkGroupsZ);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);

        // cleanup
        glUseProgram(0);
    }
    
}
