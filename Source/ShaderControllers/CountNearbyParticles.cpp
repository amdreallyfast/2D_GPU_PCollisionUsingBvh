#include "Include/ShaderControllers/CountNearbyParticles.h"

#include <string>

#include "Shaders/ShaderStorage.h"
#include "Shaders/ComputeHeaders/ComputeShaderWorkGroupSizes.comp"

#include "ThirdParty/glload/include/glload/gl_4_4.h"


namespace ShaderControllers
{
    /*--------------------------------------------------------------------------------------------
    Description:
        Gives members initial values.
        Constructs the CountNearbyParticles shader.

        Note: Take a copy to the SSBO's smart pointer, not a reference, because a non-const 
        shared pointer may be passed in, and std::shared_ptr<class> cannot convert the reference 
        to std::shared_ptr<const class> (differnt template types), but it can convert from 
        non-const to const by copying to a new shared pointer.
    Parameters: 
        particlesToAnalyze  A copy of a const shared pointer to the particle SSBO.
    Returns:    None
    Creator:    John Cox, 4/2017
    --------------------------------------------------------------------------------------------*/
    CountNearbyParticles::CountNearbyParticles(const ParticleSsbo::CONST_SHARED_PTR particlesToAnalyze) :
        _totalParticleCount(0),
        _computeProgramId(0)
    {
        _totalParticleCount = particlesToAnalyze->NumItems();

        // construct the compute shader
        ShaderStorage &shaderStorageRef = ShaderStorage::GetInstance();

        std::string shaderKey = "count nearby particles";
        shaderStorageRef.NewCompositeShader(shaderKey);
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/ComputeHeaders/Version.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/ComputeHeaders/ComputeShaderWorkGroupSizes.comp");
            shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/ComputeHeaders/SsboBufferBindings.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/ComputeHeaders/CrossShaderUniformLocations.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/ParticleBuffer.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/ParticleRegionBoundaries.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/PositionToMortonCode.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/CountNearbyParticlesLimits.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/CountNearbyParticles.comp");
        shaderStorageRef.CompileCompositeShader(shaderKey, GL_COMPUTE_SHADER);
        shaderStorageRef.LinkShader(shaderKey);
        _computeProgramId = shaderStorageRef.GetShaderProgram(shaderKey);
        particlesToAnalyze->ConfigureConstantUniforms(_computeProgramId);

    }

    /*--------------------------------------------------------------------------------------------
    Description:
        Cleans up the shader program that was created for this shader controller.
    Parameters: 
        particlesToAnalyze  A copy of a const shared pointer to the particle SSBO.
    Returns:    None
    Creator:    John Cox, 4/2017
    --------------------------------------------------------------------------------------------*/
    CountNearbyParticles::~CountNearbyParticles()
    {
        glDeleteProgram(_computeProgramId);
    }

    /*--------------------------------------------------------------------------------------------
    Description:
        Launches the compute shader.  This is not an exciting method.
    Parameters: None
    Returns:    None
    Creator:    John Cox, 4/2017
    --------------------------------------------------------------------------------------------*/
    void CountNearbyParticles::Count() const
    {
        glUseProgram(_computeProgramId);

        GLuint numWorkGroupsX = (_totalParticleCount / PARTICLE_OPERATIONS_WORK_GROUP_SIZE_X) + 1;
        GLuint numWorkGroupsY = 1;
        GLuint numWorkGroupsZ = 1;
        glDispatchCompute(numWorkGroupsX, numWorkGroupsY, numWorkGroupsZ);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT | GL_ATOMIC_COUNTER_BARRIER_BIT);

        // cleanup
        glUseProgram(0);
    }
}
