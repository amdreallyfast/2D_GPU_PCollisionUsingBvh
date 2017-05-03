#include "Include/Buffers/SSBOs/PolygonSsbo.h"

#include "ThirdParty/glload/include/glload/gl_4_4.h"
#include "Shaders/Compute/ComputeHeaders/SsboBufferBindings.comp"
#include "Shaders/Compute/ComputeHeaders/CrossShaderUniformLocations.comp"

#include "Include/Geometry/PolygonFace.h"


/*-----------------------------------------------------------------------------------------------
Description:
    Calls the base class to give members initial values (zeros).

    Allocates space for the SSBO and dumps the given collection of polygon faces into it.
Parameters: 
    faceCollection  Self-explanatory
Returns:    None
Creator: John Cox, 9-8-2016
-----------------------------------------------------------------------------------------------*/
PolygonSsbo::PolygonSsbo(const std::vector<PolygonFace> &faceCollection) :
    SsboBase()  // generate buffers
{
    _numItems = faceCollection.size();

    // two vertices per face (used with glDrawArrays(...))
    _numVertices = faceCollection.size() * 2;

    // now bind this new buffer to the dedicated buffer binding location
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, POLYGON_BUFFER_BINDING, _bufferId);

    // and fill it with the new data
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, _bufferId);
    GLuint bufferSizeBytes = sizeof(PolygonFace) * faceCollection.size();
    glBufferData(GL_SHADER_STORAGE_BUFFER, bufferSizeBytes, faceCollection.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

/*------------------------------------------------------------------------------------------------
Description:
    Defines the buffer's size uniform in the specified shader.  It uses the #define'd uniform 

    If the shader does not have the uniform or if the shader compiler optimized it out, then 
    OpenGL will complain about not finding it.  Enable debugging in main() in main.cpp for more 
    detail.
Parameters: 
    computeProgramId    Self-explanatory.
Returns:    
    See Description.
Creator:    John Cox, 3/2017
------------------------------------------------------------------------------------------------*/
void PolygonSsbo::ConfigureConstantUniforms(unsigned int computeProgramId) const
{
    // the uniform should remain constant after this 
    glUseProgram(computeProgramId);

    // use "num items" instead of "num vertices" because they are not the same (see constructor)
    glUniform1ui(UNIFORM_LOCATION_POLYGON_BUFFER_SIZE, _numItems);
    glUseProgram(0);
}

/*-----------------------------------------------------------------------------------------------
Description:
    Sets up the vertex attribute pointers for this SSBO's VAO.
Parameters: 
    RenderProgramId     Self-explanatory
    drawStyle           Expected to be GL_LINES (2D program) or GL_TRIANGLES (3D program).
Returns:    None
Creator: John Cox, 11-24-2016
-----------------------------------------------------------------------------------------------*/
void PolygonSsbo::ConfigureRender(unsigned int renderProgramId, unsigned int drawStyle)
{
    _drawStyle = drawStyle;

    // the render program is required for vertex attribute initialization or else the program 
    // WILL crash at runtime
    //glUseProgram(renderProgramId);
    glUseProgram(0);
    glGenVertexArrays(1, &_vaoId);
    glBindVertexArray(_vaoId);
    glBindBuffer(GL_ARRAY_BUFFER, _bufferId);

    unsigned int vertexArrayIndex = 0;
    unsigned int bufferStartOffset = 0;
    unsigned int bytesPerStep = sizeof(MyVertex);
    unsigned int sizeOfItem = 0;
    unsigned int numItems = 0;

    // only need to define 1 vertex
    // TODO: if I ever want the polygon to be defined by an irregular pattern (ex: 3 vertices, a center point, and some property of the surface), then this SSBO will need to be split into 2: 1 for rendering (MyVertexSsbo) and another for computing (GeometrySsbo or something like that)

    // position
    GLenum itemType = GL_FLOAT;
    sizeOfItem = sizeof(MyVertex::_position);
    numItems = sizeOfItem / sizeof(float);
    glEnableVertexAttribArray(vertexArrayIndex);
    glVertexAttribPointer(vertexArrayIndex, numItems, itemType, GL_FALSE, bytesPerStep, (void *)bufferStartOffset);
    bufferStartOffset += sizeOfItem;

    // normal
    itemType = GL_FLOAT;
    sizeOfItem = sizeof(MyVertex::_normal);
    numItems = sizeOfItem / sizeof(float);
    vertexArrayIndex++;
    glEnableVertexAttribArray(vertexArrayIndex);
    glVertexAttribPointer(vertexArrayIndex, numItems, itemType, GL_FALSE, bytesPerStep, (void *)bufferStartOffset);
    bufferStartOffset += sizeOfItem;

    // cleanup
    glBindVertexArray(0);   // unbind this BEFORE the array
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);    // render program
}

/*------------------------------------------------------------------------------------------------
Description:
    A simple getter for the value that was passed in on creation.
Parameters: None
Returns:    
    See Description.
Creator:    John Cox, 5/2017
------------------------------------------------------------------------------------------------*/
unsigned int PolygonSsbo::NumItems() const
{
    return _numItems;
}