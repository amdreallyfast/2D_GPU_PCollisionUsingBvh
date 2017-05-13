#include "Include/ShaderControllers/ProfilingWaitToFinish.h"

#include "ThirdParty/glload/include/glload/gl_4_4.h"



/*------------------------------------------------------------------------------------------------
Description:
    This function is used to wait for the GPU to finish its commands.  It is useful during 
    profiling.  It was recommended to me by Osbios on the OpenGL subbreddit question 
    https://www.reddit.com/r/opengl/comments/69ri6l/increasing_buffer_size_for_compute_operations/.  
    It was recommended over glFinish() because that function needs to make a round-trip to the 
    GPU and back (??glClientWaitSync(...) doesn't??) and that apparently eats up some extra 
    cycles that can throw off my profiling.
Parameters: None
Returns:    None
Creator:    John Cox, 5/2017
------------------------------------------------------------------------------------------------*/
void WaitForComputeToFinish()
{
    GLsync waitSync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    GLenum waitReturn = GL_UNSIGNALED;
    while (waitReturn != GL_ALREADY_SIGNALED && waitReturn != GL_CONDITION_SATISFIED)
    {
        waitReturn = glClientWaitSync(waitSync, GL_SYNC_FLUSH_COMMANDS_BIT, GL_TIMEOUT_IGNORED);
    }
    glDeleteSync(waitSync);
}



