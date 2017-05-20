// the OpenGL version include also includes all previous versions
// Build note: Due to a minefield of preprocessor build flags, the gl_load.hpp must come after 
// the version include.
// Build note: Do NOT mistakenly include _int_gl_4_4.h.  That one doesn't define OpenGL stuff 
// first.
// Build note: Also need to link opengl32.lib (unknown directory, but VS seems to know where it 
// is, so don't put in an "Additional Library Directories" entry for it).
// Build note: Also need to link glload/lib/glloadD.lib.
#include "ThirdParty/glload/include/glload/gl_4_4.h"
#include "ThirdParty/glload/include/glload/gl_load.hpp"

// Build note: Must be included after OpenGL code (in this case, glload).
// Build note: Also need to link freeglut/lib/freeglutD.lib.  However, the linker will try to 
// find "freeglut.lib" (note the lack of "D") instead unless the following preprocessor 
// directives are set either here or in the source-building command line (VS has a
// "Preprocessor" section under "C/C++" for preprocessor definitions).
// Build note: Also need to link winmm.lib (VS seems to know where it is, so don't put in an 
// "Additional Library Directories" entry).
#define FREEGLUT_STATIC
#define _LIB
#define FREEGLUT_LIB_PRAGMAS 0
#include "ThirdParty/freeglut/include/GL/freeglut.h"

// this linking approach is very useful for portable, crude, barebones demo code, but it is 
// better to link through the project building properties
#pragma comment(lib, "ThirdParty/glload/lib/glloadD.lib")
#pragma comment(lib, "opengl32.lib")            // needed for glload::LoadFunctions()
#pragma comment(lib, "ThirdParty/freeglut/lib/freeglutD.lib")
#ifdef WIN32
#pragma comment(lib, "winmm.lib")               // Windows-specific; freeglut needs it
#endif

// apparently the FreeType lib also needs a companion file, "freetype261d.pdb"
#pragma comment (lib, "ThirdParty/freetype-2.6.1/objs/vc2010/Win32/freetype261d.lib")

#include <stdio.h>
#include <memory>
#include <algorithm>    // for generating demo data

// for basic OpenGL stuff
#include "Include/OpenGlErrorHandling.h"
#include "Shaders/ShaderStorage.h"

// for particles, where they live, and how to update them
#include "ThirdParty/glm/vec2.hpp"
#include "ThirdParty/glm/gtc/matrix_transform.hpp"

#include "Include/Buffers/Particle.h"
#include "Include/Buffers/SSBOs/ParticleSsbo.h"
#include "Include/Buffers/PersistentAtomicCounterBuffer.h"
#include "Include/ShaderControllers/ParticleReset.h"
#include "Include/ShaderControllers/ParticleUpdate.h"
#include "Include/ShaderControllers/ParallelSort.h"
#include "Include/ShaderControllers/ParticleCollisions.h"
#include "Include/ShaderControllers/CountNearbyParticles.h"
#include "Include/ShaderControllers/RenderParticles.h"
#include "Include/ShaderControllers/RenderGeometry.h"


// for the frame rate counter (and other profiling)
#include "Include/ShaderControllers/ProfilingWaitToFinish.h"
#include "Include/RenderFrameRate/FreeTypeEncapsulated.h"
#include "Include/RenderFrameRate/Stopwatch.h"

Stopwatch gTimer;
FreeTypeEncapsulated gTextAtlases;

ParticleSsbo::SharedPtr particleBuffer = nullptr;
PolygonSsbo::SharedPtr polygonBuffer = nullptr;
std::shared_ptr<ShaderControllers::ParticleReset> particleResetter = nullptr;
std::shared_ptr<ShaderControllers::ParticleUpdate> particleUpdater = nullptr;
std::shared_ptr<ShaderControllers::ParallelSort> parallelSort = nullptr;
std::shared_ptr<ShaderControllers::ParticleCollisions> particleCollisions = nullptr;
std::shared_ptr<ShaderControllers::CountNearbyParticles> nearbyParticleCounter = nullptr;
std::shared_ptr<ShaderControllers::RenderParticles> particleRenderer = nullptr;
std::shared_ptr<ShaderControllers::RenderGeometry> geometryRenderer = nullptr;

const unsigned int MAX_PARTICLE_COUNT = 10000;





struct MortonCodeVertex
{
    MortonCodeVertex() :
        _point(glm::vec4(), glm::vec4()),
        _mortonCode(0)
    {

    }
    MyVertex _point;
    unsigned int _mortonCode;
};


unsigned int ExpandBits(unsigned int i)
{
    unsigned int expandedI = i;

    expandedI = (expandedI * 0x00010001u) & 0xFF0000FFu;
    expandedI = (expandedI * 0x00000101u) & 0x0F00F00Fu;
    expandedI = (expandedI * 0x00000011u) & 0xC30C30C3u;
    expandedI = (expandedI * 0x00000005u) & 0x49249249u;

    return expandedI;
}

unsigned int PositionToMortonCode(glm::vec4 pos, float rangeXLeft, float rangeXWidth, float rangeYBottom, float rangeYHeight)
{
    // bring to range [0,1]
    // Note: This conversion should work regardless of whether all the particle region borders 
    // are in the negative octant or all the borders are in the postive octant or the particle 
    // region is centered on the origin (0,0,0) and there is a mix of positive and negative.
    pos.x = (pos.x - rangeXLeft) / rangeXWidth;
    pos.y = (pos.y - rangeYBottom) / rangeYHeight;

    float clampX = std::min(std::max(pos.x * 1024.0f, 0.0f), 1023.0f);
    float clampY = std::min(std::max(pos.y * 1024.0f, 0.0f), 1023.0f);
    float clampZ = std::min(std::max(pos.z * 1024.0f, 0.0f), 1023.0f);

    unsigned int xx = ExpandBits((unsigned int)clampX);
    unsigned int yy = ExpandBits((unsigned int)clampY);
    unsigned int zz = ExpandBits((unsigned int)clampZ);

    return (xx * 4) + (yy * 2) + zz;
}



void GenerateZOrderCurveMortonCodes(std::vector<MortonCodeVertex> *updateThis)
{
    for (size_t i = 0; i < updateThis->size(); i++)
    {
        (*updateThis)[i]._mortonCode = PositionToMortonCode((*updateThis)[i]._point._position, -1.0f, 2.0f, -1.0f, 2.0f);
    }
}

void GenerateVerticesForZOrderCurve(std::vector<MortonCodeVertex> *putDataHere)
{
    putDataHere->clear();
    float increment = 50.0f;
    float totalRange = 1001.0f;

    float start = 100.0f;
    float end = totalRange - 100.0f;
    for (float x = start; x < end; x += increment)
    {
        for (float y = start; y < end; y += increment)
        {
            MortonCodeVertex thing;

            // generate a position along the range to [-1,+1] on both axes
            // Note: 
            // - normalize the value to the range [0,1]
            // - shift it back along the axis to [-0.5,+0.5]
            // - multiply by 2 to bring it to the range [-1,+1]
            float newX = (((x / totalRange) - 0.5f) * 2.0f);
            float newY = (((y / totalRange) - 0.5f) * 2.0f);

            thing._point._position = glm::vec4(newX, newY, 0.0f, 1.0f);
            putDataHere->push_back(thing);
        }
    }
}

bool SortByMortonCode(const MortonCodeVertex &A, const MortonCodeVertex &B)
{
    if (A._mortonCode < B._mortonCode)
    {
        return true;
    }
    return false;
}

#include <algorithm>

void GenerateZOrderCurveGeometry(std::vector<PolygonFace> *putDataHere)
{
    std::vector<MortonCodeVertex> theThing;
    GenerateVerticesForZOrderCurve(&theThing);
    GenerateZOrderCurveMortonCodes(&theThing);
    std::sort(theThing.begin(), theThing.end(), SortByMortonCode);

    //for (size_t i = 0; i < theThing.size() - 1; i++)
    //{
    //    //if (theThing[i]._mortonCode > theThing[i + 1]._mortonCode)
    //    //{
    //    //    printf("");
    //    //}

    //    int x = theThing[i]._point._position.x;
    //    int y = theThing[i]._point._position.y;

    //    if (x < -1.0f || x > +1.0f)
    //    {
    //        printf("");
    //    }
    //    else if (y < -1.0f || y > +1.0f)
    //    {
    //        printf("");
    //    }
    //    else if (x < 0.05f && x > -0.05f && y < 0.05f && y > -0.05f)
    //    {
    //        // roughly the center
    //        printf("");
    //    }
    //}

    putDataHere->clear();
    // "size - 1" - skip the last vertex
    for (size_t i = 0; i < theThing.size() - 1; i++)
    {
        PolygonFace newFace;
        newFace._start = theThing[i]._point;
        newFace._end = theThing[i + 1]._point;

        putDataHere->push_back(newFace);
    }
}








/*------------------------------------------------------------------------------------------------
Description:
    Governs window creation, the initial OpenGL configuration (face culling, depth mask, even
    though this is a 2D demo and that stuff won't be of concern), the creation of geometry, and
    the creation of a texture.
Parameters: None
Returns:    None
Creator:    John Cox (3-7-2016)
------------------------------------------------------------------------------------------------*/
void Init()
{
    // this OpenGL setup stuff is so that the frame rate text renders correctly
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LEQUAL);
    glDepthRange(0.0f, 1.0f);

    // inactive particle Z = -0.6   alpha = 0
    // active particle Z = -0.7     alpha = 1
    // polygon fragment Z = -0.8    alpha = 1
    // Note: The blend function is done via glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA).  
    // The first argument is the scale factor for the source color (presumably the existing 
    // fragment color in the frame buffer) and the second argument is the scale factor for the 
    // destination color (presumably the color of the fragment that is being added).  The second 
    // argument ("one minus source alpha") means that, when any color is being added, the 
    // resulting color will be "(existingFragmentAlpha * existingFragmentColor) - 
    // (addedFragmentAlpha * addedFragmentColor)".  
    // Also Note: If the color furthest from the camera is black (vec4(0,0,0,0)), then any 
    // color on top of it will end up as (using the equation) "vec4(0,0,0,0) - whatever", which 
    // is clamped at 0.  So put the opaque (alpha=1) furthest from the camera (this demo is 2D, 
    // so make it a lower Z).  The depth range is 0-1, so the lower Z limit is -1.
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    ShaderStorage &shaderStorageRef = ShaderStorage::GetInstance();

    // FreeType initialization
    std::string freeTypeShaderKey = "freetype";
    shaderStorageRef.NewShader(freeTypeShaderKey);
    shaderStorageRef.AddAndCompileShaderFile(freeTypeShaderKey, "Shaders/Render/FreeType.vert", GL_VERTEX_SHADER);
    shaderStorageRef.AddAndCompileShaderFile(freeTypeShaderKey, "Shaders/Render/FreeType.frag", GL_FRAGMENT_SHADER);
    shaderStorageRef.LinkShader(freeTypeShaderKey);
    GLuint freeTypeProgramId = shaderStorageRef.GetShaderProgram(freeTypeShaderKey);
    gTextAtlases.Init("ThirdParty/freetype-2.6.1/FreeSans.ttf", freeTypeProgramId);

    // Note: Compute headers with #define'd buffer binding locations makes it easy for the 
    // ParallelSort compute controller's shaders to access originalData's data without 
    // needing to pass the SSBO into it.  GPU computing in multiple steps creates coupling 
    // between the SSBOs and the shaders, but the compute headers lessen the coupling that needs 
    // to happen on the CPU side.
    particleBuffer = std::make_shared<ParticleSsbo>(MAX_PARTICLE_COUNT);
    particleBuffer->ConfigureRender(GL_POINTS);
    
    // set up the particle region
    // Note: This mat4 is a convenience for easily moving the particle region center and all 
    // emitters.
    //glm::mat4 windowSpaceTransform = glm::rotate(glm::mat4(), 45.0f, glm::vec3(0.0f, 0.0f, 1.0f));
    //windowSpaceTransform *= glm::translate(glm::mat4(), glm::vec3(-0.1f, -0.05f, 0.0f));
    glm::mat4 windowSpaceTransform = glm::rotate(glm::mat4(), 0.0f, glm::vec3(0.0f, 0.0f, 1.0f));
    windowSpaceTransform *= glm::translate(glm::mat4(), glm::vec3(0.0f, 0.0f, 0.0f));

    // for resetting particles
    // Note: Put the bar emitters across from each and spraying particles toward each other and 
    // up so that the particles collide near the middle with a slight upward velocity.
    particleResetter = std::make_unique<ShaderControllers::ParticleReset>(particleBuffer);

    // bar on the left and emitting up and right
    glm::vec2 bar1P1(-0.8f, +0.5f);
    glm::vec2 bar1P2(-0.8f, -0.1f);
    glm::vec2 emitDir1(+1.0f, 0.0f);
    float minVel = 0.1f;
    float maxVel = 0.5f;
    ParticleEmitterBar::SHARED_PTR barEmitter1 = std::make_shared<ParticleEmitterBar>(bar1P1, bar1P2, emitDir1, minVel, maxVel);
    barEmitter1->SetTransform(windowSpaceTransform);
    particleResetter->AddEmitter(barEmitter1);

    // bar on the right and emitting up and left
    glm::vec2 bar2P1 = glm::vec2(+0.8f, -0.5f);
    glm::vec2 bar2P2 = glm::vec2(+0.8f, +0.1f);
    glm::vec2 emitDir2 = glm::vec2(-1.0f, 0.0f);
    ParticleEmitterBar::SHARED_PTR barEmitter2 = std::make_shared<ParticleEmitterBar>(bar2P1, bar2P2, emitDir2, minVel, maxVel);
    barEmitter2->SetTransform(windowSpaceTransform);
    particleResetter->AddEmitter(barEmitter2);

    // for moving particles
    particleUpdater = std::make_unique<ShaderControllers::ParticleUpdate>(particleBuffer);

    // for sorting particles once they've been updated
    parallelSort = std::make_unique<ShaderControllers::ParallelSort>(particleBuffer);

    // for detecting and resolving collisions once the particles have been sorted
    particleCollisions = std::make_shared<ShaderControllers::ParticleCollisions>(particleBuffer);

    // determines particle color
    nearbyParticleCounter = std::make_unique<ShaderControllers::CountNearbyParticles>(particleBuffer);

    // for drawing particles
    particleRenderer = std::make_unique<ShaderControllers::RenderParticles>();

    // for drawing non-particle things
    geometryRenderer = std::make_unique<ShaderControllers::RenderGeometry>();

    //std::vector<PolygonFace> zOrderCurvePolygonFaces;
    ////zOrderCurvePolygonFaces.clear();
    //GenerateZOrderCurveGeometry(&zOrderCurvePolygonFaces);
    //polygonBuffer = std::make_shared<PolygonSsbo>(zOrderCurvePolygonFaces);
    //polygonBuffer->ConfigureRender(GL_LINES);



    //// for profiling 
    //// Note: The first call uploads all the buffers before sorting, so the numbers are screwed a 
    //// bit.  They are already loaded on the second call, so that call runs more like what it 
    //// would run in real time.
    //parallelSort->SortWithProfiling();
    //parallelSort->SortWithProfiling();

    // the timer will be used for framerate calculations
    gTimer.Start();
}

#include <chrono>
#include <iostream>

/*------------------------------------------------------------------------------------------------
Description:
    Updates particle positions, generates the quad tree for the particles' new positions, and 
    commands a new draw.
Parameters: None
Returns:    None
Exception:  Safe
Creator:    John Cox (1-2-2017)
------------------------------------------------------------------------------------------------*/
void UpdateAllTheThings()
{
    using namespace std::chrono;
    steady_clock::time_point start = high_resolution_clock::now();
    
    // just hard-code it for this demo
    float deltaTimeSec = 0.01f;

    particleResetter->ResetParticles(1);
    particleUpdater->Update(deltaTimeSec);
    //parallelSort->SortWithProfiling();
    parallelSort->SortWithoutProfiling();
    //particleCollisions->DetectAndResolveWithProfiling(particleUpdater->NumActiveParticles());
    particleCollisions->DetectAndResolveWithoutProfiling(particleUpdater->NumActiveParticles());
    //nearbyParticleCounter->Count();


    ShaderControllers::WaitOnQueuedSynchronization();






    // tell glut to call this display() function again on the next iteration of the main loop
    // Note: https://www.opengl.org/discussion_boards/showthread.php/168717-I-dont-understand-what-glutPostRedisplay()-does
    // Also Note: This display() function will also be registered to run if the window is moved
    // or if the viewport is resized.  If glutPostRedisplay() is not called, then as long as the
    // window stays put and doesn't resize, display() won't be called again (tested with 
    // debugging).
    // Also Also Note: It doesn't matter where this is called in this function.  It sets a flag
    // for glut's main loop and doesn't actually call the registered display function, but I 
    // got into the habbit of calling it at the end.
    glutPostRedisplay();

    steady_clock::time_point end = high_resolution_clock::now();
    //std::cout << "UpdateAllTheThings(): " << duration_cast<milliseconds>(end - start).count() << " milliseconds" << std::endl;
}

/*------------------------------------------------------------------------------------------------
Description:
    This is the rendering function.  It tells OpenGL to clear out some color and depth buffers,
    to set up the data to draw, to draw than stuff, and to report any errors that it came across.
    This is not a user-called function.

    This function is registered with glutDisplayFunc(...) during glut's initialization.
Parameters: None
Returns:    None
Creator:    John Cox (2-13-2016)
------------------------------------------------------------------------------------------------*/
void Display()
{
    using namespace std::chrono;
    steady_clock::time_point start = high_resolution_clock::now();

    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClearDepth(1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    particleRenderer->Render(particleBuffer);
    //geometryRenderer->Render(polygonBuffer);
    geometryRenderer->Render(particleCollisions->BvhVerticesSsbo());

    // draw the frame rate once per second in the lower left corner
    glUseProgram(ShaderStorage::GetInstance().GetShaderProgram("freetype"));

    GLfloat color[4] = { 0.5f, 0.5f, 0.0f, 1.0f };
    char str[32];
    static int elapsedFramesPerSecond = 0;
    static double elapsedTime = 0.0;
    static double frameRate = 0.0;
    elapsedFramesPerSecond++;
    elapsedTime += gTimer.Lap();
    if (elapsedTime > 1.0f)
    { 
        frameRate = (double)elapsedFramesPerSecond / elapsedTime;
        elapsedFramesPerSecond = 0;
        elapsedTime -= 1.0f;
    }
    sprintf(str, "%.2lf", frameRate);

    // Note: The font textures' orgin is their lower left corner, so the "lower left" in screen 
    // space is just above [-1.0f, -1.0f].
    float xy[2] = { -0.99f, -0.99f };
    float scaleXY[2] = { 1.0f, 1.0f };

    // frame rate
    gTextAtlases.GetAtlas(48)->RenderText(str, xy, scaleXY, color);

    // now show number of active particles
    // Note: For some reason, lower case "i" seems to appear too close to the other letters.
    sprintf(str, "active: %d", particleUpdater->NumActiveParticles());
    float numActiveParticlesXY[2] = { -0.99f, +0.7f };
    gTextAtlases.GetAtlas(48)->RenderText(str, numActiveParticlesXY, scaleXY, color);


    // clean up bindings
    glUseProgram(0);
    glBindVertexArray(0);       // unbind this BEFORE the buffer
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // tell the GPU to swap out the displayed buffer with the one that was just rendered
    glutSwapBuffers();

    steady_clock::time_point end = high_resolution_clock::now();
    //std::cout << "Display(): " << duration_cast<milliseconds>(end - start).count() << " milliseconds" << std::endl;
}

/*------------------------------------------------------------------------------------------------
Description:
    Tell's OpenGL to resize the viewport based on the arguments provided.  This is an 
    opportunity to call glViewport or glScissor to keep up with the change in size.
    
    This is not a user-called function.  It is registered with glutReshapeFunc(...) during 
    glut's initialization.
Parameters:
    w   The width of the window in pixels.
    h   The height of the window in pixels.
Returns:    None
Creator:    John Cox (2-13-2016)
------------------------------------------------------------------------------------------------*/
void Reshape(int w, int h)
{
    glViewport(0, 0, w, h);
}

/*------------------------------------------------------------------------------------------------
Description:
    Executes when the user presses a key on the keyboard.

    This is not a user-called function.  It is registered with glutKeyboardFunc(...) during
    glut's initialization.

    Note: Although the x and y arguments are for the mouse's current position, this function does
    not respond to mouse presses.
Parameters:
    key     The ASCII code of the key that was pressed (ex: ESC key is 27)
    x       The horizontal viewport coordinates of the mouse's current position.
    y       The vertical window coordinates of the mouse's current position
Returns:    None
Creator:    John Cox (2-13-2016)
------------------------------------------------------------------------------------------------*/
void Keyboard(unsigned char key, int x, int y)
{
    // this statement is mostly to get ride of an "unreferenced parameter" warning
    printf("keyboard: x = %d, y = %d\n", x, y);
    switch (key)
    {
    case 27:
    {
        // ESC key
        glutLeaveMainLoop();
        return;
    }
    default:
        break;
    }
}

/*------------------------------------------------------------------------------------------------
Description:
    I don't know what this does, but I've kept it around since early times, and this was the 
    comment given with it:
    
    "Called before FreeGLUT is initialized. It should return the FreeGLUT display mode flags 
    that you want to use. The initial value are the standard ones used by the framework. You can 
    modify it or just return you own set.  This function can also set the width/height of the 
    window. The initial value of these variables is the default, but you can change it."
Parameters:
    displayMode     ??
    width           ??
    height          ??
Returns:
    ??what??
Creator:    John Cox (2-13-2016)
------------------------------------------------------------------------------------------------*/
unsigned int Defaults(unsigned int displayMode, int &width, int &height) 
{
    // this statement is mostly to get ride of an "unreferenced parameter" warning
    printf("Defaults: width = %d, height = %d\n", width, height);
    return displayMode; 
}

/*------------------------------------------------------------------------------------------------
Description:
    Cleans up GPU memory.  This might happen when the processes die, but be a good memory steward
    and clean up properly.

    Note: A big program would have the textures, program IDs, buffers, and other things 
    encapsulated somewhere, and each other those would do the cleanup, but in this barebones 
    demo, I can just clean up everything here.
Parameters: None
Returns:    None
Creator:    John Cox (2-13-2016)
------------------------------------------------------------------------------------------------*/
void CleanupAll()
{
}

/*------------------------------------------------------------------------------------------------
Description:
    Program start and end.
Parameters:
    argc    The number of strings in argv.
    argv    A pointer to an array of null-terminated, C-style strings.
Returns:
    0 if program ended well, which it always does or it crashes outright, so returning 0 is fine
Creator:    John Cox (2-13-2016)
------------------------------------------------------------------------------------------------*/
int main(int argc, char *argv[])
{
    glutInit(&argc, argv);

    int width = 500;
    int height = 500;
    unsigned int displayMode = GLUT_DOUBLE | GLUT_ALPHA | GLUT_DEPTH | GLUT_STENCIL;
    displayMode = Defaults(displayMode, width, height);

    glutInitDisplayMode(displayMode);
    glutInitContextVersion(4, 5);
    glutInitContextProfile(GLUT_CORE_PROFILE);

    // enable this for automatic message reporting (see OpenGlErrorHandling.cpp)
#define DEBUG
#ifdef DEBUG
    glutInitContextFlags(GLUT_DEBUG);
#endif

    glutInitWindowSize(width, height);
    glutInitWindowPosition(300, 200);
    int window = glutCreateWindow(argv[0]);

    glload::LoadTest glLoadGood = glload::LoadFunctions();
    // ??check return value??

    glutSetOption(GLUT_ACTION_ON_WINDOW_CLOSE, GLUT_ACTION_CONTINUE_EXECUTION);

    // Note: Compute shaders require at least OpenGL 4.3, but I'm pushing this to OpenGL 4.5 
    // anyway.  This is an old check from a tutorial that I never bothered to change until 
    // now (4-1-2017).
    int requiredMajorVersion = 4;
    int requiredMinorVersion = 5;
    if (!glload::IsVersionGEQ(requiredMajorVersion, requiredMinorVersion))
    {
        printf("Your OpenGL version is %i, %i. You must have at least OpenGL %d.%d to run this tutorial.\n",
            glload::GetMajorVersion(), glload::GetMinorVersion(), requiredMajorVersion, requiredMinorVersion);
        glutDestroyWindow(window);
        return 0;
    }

    if (glext_ARB_debug_output)
    {
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS_ARB);
        glDebugMessageCallback(DebugFunc, (void*)15);
    }

    Init();

    glutIdleFunc(UpdateAllTheThings);
    glutDisplayFunc(Display);
    glutReshapeFunc(Reshape);
    glutKeyboardFunc(Keyboard);
    glutMainLoop();

    CleanupAll();

    return 0;
}