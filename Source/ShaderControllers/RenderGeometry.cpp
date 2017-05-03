#include "Include/ShaderControllers/RenderGeometry.h"

#include <string>

#include "Shaders/ShaderStorage.h"
#include "Shaders/Compute/ComputeHeaders/ComputeShaderWorkGroupSizes.comp"

#include "ThirdParty/glload/include/glload/gl_4_4.h"


namespace ShaderControllers
{
    /*--------------------------------------------------------------------------------------------
    Description:
        Gives members initial values.
        Constructs the RenderGeometry shader.
    Parameters: None
    Returns:    None
    Creator:    John Cox, 5/2017
    --------------------------------------------------------------------------------------------*/
    RenderGeometry::RenderGeometry() :
        _renderProgramId(0)
    {
        // construct the compute shader
        ShaderStorage &shaderStorageRef = ShaderStorage::GetInstance();

        std::string shaderKey = "render geometry";
        shaderStorageRef.NewShader(shaderKey);
        shaderStorageRef.AddAndCompileShaderFile(shaderKey, "Shaders/Render/Geometry.vert", GL_VERTEX_SHADER);
        shaderStorageRef.AddAndCompileShaderFile(shaderKey, "Shaders/Render/Geometry.frag", GL_FRAGMENT_SHADER);
        shaderStorageRef.LinkShader(shaderKey);
        _renderProgramId = shaderStorageRef.GetShaderProgram(shaderKey);
    }

    /*--------------------------------------------------------------------------------------------
    Description:
        Cleans up the shader program that was created for this shader controller.
    Parameters: None    
    Returns:    None
    Creator:    John Cox, 5/2017
    --------------------------------------------------------------------------------------------*/
    RenderGeometry::~RenderGeometry()
    {
        glDeleteProgram(_renderProgramId);
    }

    /*--------------------------------------------------------------------------------------------
    Description:
        Exists so that this shader controller doesn't have to allow the program ID out of its 
        sight (read, "returned to other code").

        // TODO: ??better way to do this? why does the VAO require a bound program??


    Parameters: 
        ssboToRender    The SSBO that needs to have its attribute pointers set up.
        drawStyle       GL_POINTS, GL_LINES, GL_TRIANGLES, etc.
    Returns:    None
    Creator:    John Cox, 5/2017
    --------------------------------------------------------------------------------------------*/
    void RenderGeometry::ConfigureSsboForRendering(const PolygonSsbo::SHARED_PTR &ssboToRender, unsigned int drawStyle)
    {
        // TODO: try to set up the VAO with a p
        ssboToRender->ConfigureRender(_renderProgramId, drawStyle);
    }

    /*--------------------------------------------------------------------------------------------
    Description:
        Binds the render program and the SSBO's VAO, draws all the SSBO's vertices, and cleans 
        the VAO and program bindings.
    Parameters: 
        ssboToRender    Contains VAO ID, draw style, and number of vertices.
    Returns:    None
    Creator:    John Cox, 5/2017
    --------------------------------------------------------------------------------------------*/
    void RenderGeometry::Render(const PolygonSsbo::SHARED_PTR &ssboToRender) const
    {
        glUseProgram(_renderProgramId);
        glBindVertexArray(ssboToRender->VaoId());
        glDrawArrays(ssboToRender->DrawStyle(), 0, ssboToRender->NumVertices());
        glBindVertexArray(0);
        glUseProgram(0);
    }

}

