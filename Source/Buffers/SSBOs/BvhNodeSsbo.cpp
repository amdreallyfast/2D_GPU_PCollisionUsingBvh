#include "Include/Buffers/SSBOs/BvhNodeSsbo.h"

#include "ThirdParty/glload/include/glload/gl_4_4.h"
#include "Shaders/ShaderHeaders/SsboBufferBindings.comp"
#include "Shaders/ShaderHeaders/CrossShaderUniformLocations.comp"

#include "Include/Buffers/BvhNode.h"

#include <vector>


/*------------------------------------------------------------------------------------------------
Description:
    Initializes base class, then gives derived class members initial values and allocates space 
    for the SSBO.
Parameters: 
    numLeaves   Expected to be the same size as the number of particles.  If it isn't, then 
                there is a risk of particle buffer overrun.
Returns:    None
Creator:    John Cox, 5/2017
------------------------------------------------------------------------------------------------*/
BvhNodeSsbo::BvhNodeSsbo(unsigned int numLeaves)
{
    // binary trees with N leaves have N-1 branches
    _numLeaves = numLeaves;
    _numInternalNodes = numLeaves - 1;
    _numTotalNodes = numLeaves + (numLeaves - 1);
    std::vector<BvhNode> v(_numTotalNodes);

    // now bind this new buffer to the dedicated buffer binding location
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, BVH_NODE_BUFFER_BINDING, _bufferId);

    // and fill it with new data
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, _bufferId);
    glBufferData(GL_SHADER_STORAGE_BUFFER, v.size() * sizeof(BvhNode), v.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

/*------------------------------------------------------------------------------------------------
Description:
    Defines the buffer's size uniform in the specified shader.  It uses the #define'd uniform 
    location found in CrossShaderUniformLocations.comp.

    If the shader does not have the uniform or if the shader compiler optimized it out, then 
    OpenGL will complain about not finding it.  Enable debugging in main() in main.cpp for more 
    detail.
Parameters: 
    computeProgramId    Self-explanatory.
Returns:    
    See Description.
Creator:    John Cox, 5/2017
------------------------------------------------------------------------------------------------*/
void BvhNodeSsbo::ConfigureConstantUniforms(unsigned int computeProgramId) const
{
    // the uniform should remain constant after this 
    glUseProgram(computeProgramId);
    glUniform1ui(UNIFORM_LOCATION_INTERMEDIATE_BUFFER_HALF_SIZE, UNIFORM_LOCATION_BVH_NUMBER_LEAVES);
    glUseProgram(0);

}

/*------------------------------------------------------------------------------------------------
Description:
    A simple getter for the value that was passed in on creation.
Parameters: None
Returns:    
    See Description.
Creator:    John Cox, 5/2017
------------------------------------------------------------------------------------------------*/
unsigned int BvhNodeSsbo::NumLeafNodes() const
{
    return _numLeaves;
}
