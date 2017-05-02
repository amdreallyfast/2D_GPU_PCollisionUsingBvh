#include "Include/ShaderControllers/RenderParticles.h"

#include <string>
#include "Shaders/ShaderStorage.h"

#include "ThirdParty/glload/include/glload/gl_4_4.h"

namespace ShaderControllers
{
    /*--------------------------------------------------------------------------------------------
    Description:
        Gives members initial values.
        
        Constructs the RenderParticles shader out of ParticleRender.vert and ParticleRender.frag.

        Configures the argument SSBO to use this particular object's render shader, then keeps 
        info around for use during Render().  It is the user's responsibility to make sure that
        the SSBO doesn't die before the RenderParticles shader controller does.
    Parameters: 
        particleSsboToRender    Will be configured for this render shader.
    Returns:    None
    Creator:    John Cox, 4/2017
    --------------------------------------------------------------------------------------------*/
    RenderParticles::RenderParticles(ParticleSsbo::SHARED_PTR &particleSsboToRender) :
        _renderProgramId(0),
        _vaoId(0),
        _drawStyle(0),
        _numVertices(0)
    {
        // construct the compute shader
        ShaderStorage &shaderStorageRef = ShaderStorage::GetInstance();

        std::string shaderKey = "particle render";
//        shaderStorageRef.NewShader(shaderKey);
//        shaderStorageRef.AddAndCompileShaderFile(shaderKey, "Shaders/ParticleRender.vert", GL_VERTEX_SHADER);
//        shaderStorageRef.AddAndCompileShaderFile(shaderKey, "Shaders/ParticleRender.frag", GL_FRAGMENT_SHADER);
        shaderStorageRef.NewCompositeShader(shaderKey);
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/ComputeHeaders/Version.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/CountNearbyParticlesLimits.comp");
        shaderStorageRef.AddPartialShaderFile(shaderKey, "Shaders/ParticleRender.vert");
        shaderStorageRef.CompileCompositeShader(shaderKey, GL_VERTEX_SHADER);
        shaderStorageRef.AddAndCompileShaderFile(shaderKey, "Shaders/ParticleRender.frag", GL_FRAGMENT_SHADER);
        shaderStorageRef.LinkShader(shaderKey);
        _renderProgramId = shaderStorageRef.GetShaderProgram(shaderKey);

        // particles are points, so every vertex is a point
        particleSsboToRender->ConfigureRender(_renderProgramId, GL_POINTS);

        // record these for later
        // Note: Is this a bad ida?  It could be.  If the SSBO that this object relies on dies 
        // before this RenderParticles shader controller dies, and then this shader controller 
        // tries to render, then binding the VAO could blow up the program.  The constructor is 
        // generic and does not rely on any particular SSBO, but the SSBO needs to be configured 
        // for rendering using the compiled shader.  There are two methods where this can happen:
        // (1) In the constructor
        // (2) Render()
        // For performance reasons, I'd rather have it happen once upon construction than prior 
        // to every draw call.
        // Also Note: A third option is to pass the SSBO once on construction for configuring 
        // with the shader and then being passed on every Render(), but then the SSBO is being 
        // passed into this object in more than one place.  I'd rather have it happen once.
        // Also Also Note: I suppose that I could keep a const shared pointer to the SSBO here 
        // in the shader controller.  that would prevent it from dying.  But then the shader 
        // controller would be considered an owner" of the SSBO, and I don't want that.  So I'll 
        // stick with passing the SSBO upon construction and then making sure that the SSBO and 
        // shader controller die at the same time.
        _vaoId = particleSsboToRender->VaoId();
        _drawStyle = particleSsboToRender->DrawStyle();

        // in the case of particles, "num items" == "num vertices", so either getter is fine
        _numVertices = particleSsboToRender->NumVertices();
    }

    /*--------------------------------------------------------------------------------------------
    Description:
        Cleans up the shader that was created for this shader controller.
    Parameters: None
    Returns:    None
    Creator:    John Cox, 4/2017
    --------------------------------------------------------------------------------------------*/
    RenderParticles::~RenderParticles()
    {
        glDeleteProgram(_renderProgramId);
    }

    /*--------------------------------------------------------------------------------------------
    Description:
        Binds the VAO for the particle SSBO, then calls glDrawArrays(...).
    Parameters: None
    Returns:    None
    Creator:    John Cox, 4/2017
    --------------------------------------------------------------------------------------------*/
    void RenderParticles::Render() const
    {
        glUseProgram(_renderProgramId);
        glBindVertexArray(_vaoId);
        glDrawArrays(_drawStyle, 0, _numVertices);
        glBindVertexArray(0);
        glUseProgram(0);
    }

}
