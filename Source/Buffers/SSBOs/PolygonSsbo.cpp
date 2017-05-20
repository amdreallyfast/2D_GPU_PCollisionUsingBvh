#include "Include/Buffers/SSBOs/PolygonSsbo.h"

#include "ThirdParty/glload/include/glload/gl_4_4.h"
#include "Shaders/ShaderHeaders/SsboBufferBindings.comp"
#include "Shaders/ShaderHeaders/CrossShaderUniformLocations.comp"

#include "Include/Geometry/PolygonFace.h"



/*-----------------------------------------------------------------------------------------------
Description:
    Calls the base class to give members initial values (zeros).

    Allocates space for the SSBO and dumps the given collection of polygon faces into it.
Parameters: 
    numPolygons     Self-explanatory
Returns:    None
Creator: John Cox, 5/2017
-----------------------------------------------------------------------------------------------*/
PolygonSsbo::PolygonSsbo(int numPolygons) :
    SsboBase()  // generate buffers
{
    _numItems = numPolygons;
    _numVertices = numPolygons * PolygonFace::NumVerticesPerFace();

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, POLYGON_BUFFER_BINDING, _bufferId);

    // and fill it with the new data
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, _bufferId);
    GLuint bufferSizeBytes = sizeof(PolygonFace) * numPolygons;
    glBufferData(GL_SHADER_STORAGE_BUFFER, bufferSizeBytes, nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

/*-----------------------------------------------------------------------------------------------
Description:
    Convenience constructor.
Parameters: 
    faceCollection  Self-explanatory
Returns:    None
Creator: John Cox, 9-8-2016
-----------------------------------------------------------------------------------------------*/
PolygonSsbo::PolygonSsbo(const std::vector<PolygonFace> &faceCollection) :
    SsboBase()  // generate buffers
{
    _numItems = faceCollection.size();
    _numVertices = faceCollection.size() * PolygonFace::NumVerticesPerFace();

    // now bind this new buffer to the dedicated buffer binding location
    // Note: This only ??applies to the compute shader SSBO and not to the one that is set up for the Z-order curve? is this binding problemmatic and will it create conflicts??
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, POLYGON_BUFFER_BINDING, _bufferId);

    // and fill it with the new data
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, _bufferId);
    GLuint bufferSizeBytes = sizeof(PolygonFace) * faceCollection.size();
    glBufferData(GL_SHADER_STORAGE_BUFFER, bufferSizeBytes, faceCollection.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

// TODO: header
void PolygonSsbo::ConfigureComputeBindingPoint(unsigned int computeProgramId) const
{

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, POLYGON_BUFFER_BINDING, _bufferId);

    //glBindBuffer(GL_SHADER_STORAGE_BUFFER, _bufferId);

    //// see the corresponding area in ParticleSsbo::Init(...) for explanation
    //// Note: MUST use the same binding point 

    ////GLuint ssboBindingPointIndex = 13;   // or 1, or 5, or 17, or wherever IS UNUSED
    //GLuint storageBlockIndex = glGetProgramResourceIndex(computeProgramId, GL_SHADER_STORAGE_BLOCK, bufferNameInShader.c_str());
    //glShaderStorageBlockBinding(computeProgramId, storageBlockIndex, _ssboBindingPointIndex);
    //glBindBufferBase(GL_SHADER_STORAGE_BUFFER, _ssboBindingPointIndex, _bufferId);


    //// cleanup
    //glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
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
    drawStyle           Expected to be GL_LINES (2D program) or GL_TRIANGLES (3D program).
Returns:    None
Creator: John Cox, 11-24-2016
-----------------------------------------------------------------------------------------------*/
void PolygonSsbo::ConfigureRender(unsigned int drawStyle)
{
    _drawStyle = drawStyle;

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