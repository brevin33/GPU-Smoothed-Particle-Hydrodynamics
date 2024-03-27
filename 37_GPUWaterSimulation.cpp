#define NUM_CELLS 8192
// Interfaces
#include "TheForge/Common_3/Application/Interfaces/IApp.h"
#include "TheForge/Common_3/Application/Interfaces/ICameraController.h"
#include "TheForge/Common_3/Application/Interfaces/IFont.h"
#include "TheForge/Common_3/Application/Interfaces/IInput.h"
#include "TheForge/Common_3/Application/Interfaces/IProfiler.h"
#include "TheForge/Common_3/Application/Interfaces/IScreenshot.h"
#include "TheForge/Common_3/Application/Interfaces/IUI.h"
#include "TheForge/Common_3/Game/Interfaces/IScripting.h"
#include "TheForge/Common_3/Utilities/Interfaces/IFileSystem.h"
#include "TheForge/Common_3/Utilities/Interfaces/ILog.h"
#include "TheForge/Common_3/Utilities/Interfaces/ITime.h"

#include "TheForge/Common_3/Utilities/RingBuffer.h"

// Renderer
#include "TheForge/Common_3/Graphics/Interfaces/IGraphics.h"
#include "TheForge/Common_3/Resources/ResourceLoader/Interfaces/IResourceLoader.h"

// Math
#include "TheForge/Common_3/Utilities/Math/MathTypes.h"
#include "TheForge/Common_3/Utilities/Interfaces/IMemory.h"

struct Particle
{
    float pos[3];
    float  density;
    float vel[3];
    float  pressure;
};

struct Cell
{
    uint count;
    uint dataPos;
};

struct UniformBlockWater
{
    float dt;
    float width;
    float height;
    float depth;
    float pressureMult;
    float smoothingRadius;
    float desiredDensity;
    float dragCo;
    float bump;
    float viscosity;
};

struct UniformBlock
{
    CameraMatrix mProjectView;
    float        aspectRatio1;
    float        aspectRatio2;
    float        size;
};

struct UniformBlockSky
{
    CameraMatrix mProjectView;
};

// But we only need Two sets of resources (one in flight and one being used on CPU)
const uint32_t gDataBufferCount = 2;

Renderer* pRenderer = NULL;

Queue*     pGraphicsQueue = NULL;
GpuCmdRing gGraphicsCmdRing = {};

SwapChain*    pSwapChain = NULL;
RenderTarget* pDepthBuffer = NULL;
Semaphore*    pImageAcquiredSemaphore = NULL;

Shader*      pWaterShader = NULL;
Shader*      pWaterMoveShader = NULL;
Shader*      pWaterResetCellsShader = NULL;
Shader*      pWaterCalculatePressureShader = NULL;
Shader*      pWaterParticlesIntoCellsShader = NULL;
Shader*      pWaterCountCellsShader = NULL;
Shader*      pWaterFindCellDataPosShader = NULL;
Shader*      pWaterResetSumShader = NULL;
Buffer*      pWaterVertexBuffer = NULL;
Buffer*      pWaterInstanceBuffer = NULL;
Buffer*      pWaterIndexBuffer = NULL;
uint32_t     gWaterIndexCount = 0;
Pipeline*    pWaterPipeline = NULL;
VertexLayout gWaterVertexLayout = {};

Shader*        pSkyBoxDrawShader = NULL;
Buffer*        pSkyBoxVertexBuffer = NULL;
Pipeline*      pSkyBoxDrawPipeline = NULL;
Pipeline*      pWaterMoveCompPipeline = NULL;
Pipeline*      pWaterResetCellsCompPipeline = NULL;
Pipeline*      pWaterParticlesIntoCellsCompPipeline = NULL;
Pipeline*      pWaterCountCellsCompPipeline = NULL;
Pipeline*      pWaterFindCellDataPosCompPipeline = NULL;
Pipeline*      pWaterCalculatePressureCompPipeline = NULL;
Pipeline*      pWaterResetSumCompPipeline = NULL;
RootSignature* pRootSignatureWaterSimulation = NULL;
RootSignature* pRootSignature = NULL;
Sampler*       pSamplerSkyBox = NULL;
Texture*       pSkyBoxTextures[6];
DescriptorSet* pDescriptorSetTexture = { NULL };
DescriptorSet* pDescriptorSetUniforms = { NULL };
DescriptorSet* pDescriptorSetGraphicsParticles = { NULL };
DescriptorSet* pDescriptorSetWater = { NULL };

Buffer* pProjViewUniformBuffer[gDataBufferCount] = { NULL };
Buffer* pSkyboxUniformBuffer[gDataBufferCount] = { NULL };
Buffer* pWaterUniformBuffer[gDataBufferCount] = { NULL };

Buffer* pCellBuffer = NULL;
Buffer* pParticleIDBuffer = NULL;
Buffer* pSumBuffer = NULL;


uint32_t     gFrameIndex = 0;
ProfileToken gGpuProfileToken = PROFILE_INVALID_TOKEN;

UniformBlock      gUniformData;
UniformBlockSky   gUniformDataSky;
UniformBlockWater gUniformDataWater = { 0.003f, 30.0f, 30.0f, 30.0f, 600.0f, 3.0f, 0.68f, 0.015f, 0.15f, 0.02f };
uint32_t          gNumParticles = 32 * 500;
const uint32_t    gMaxNumParticles = gNumParticles * 6;
float             gWaterSize = 5.5f;
bool              gDynamicDeltaTime = true;



ICameraController* pCameraController = NULL;

UIComponent* pGuiWindow = NULL;

uint32_t gFontID = 0;

QueryPool* pPipelineStatsQueryPool[gDataBufferCount] = {};


DECLARE_RENDERER_FUNCTION(void, mapBuffer, Renderer* pRenderer, Buffer* pBuffer, ReadRange* pRange)
DECLARE_RENDERER_FUNCTION(void, unmapBuffer, Renderer* pRenderer, Buffer* pBuffer)

const char* pSkyBoxImageFileNames[] = { "Skybox_right1.tex",  "Skybox_left2.tex",  "Skybox_top3.tex",
                                        "Skybox_bottom4.tex", "Skybox_front5.tex", "Skybox_back6.tex" };

FontDrawDesc gFrameTimeDraw;

// Generate sky box vertex buffer
const float gSkyBoxPoints[] = {
    10.0f,  -10.0f, -10.0f, 6.0f, // -z
    -10.0f, -10.0f, -10.0f, 6.0f,   -10.0f, 10.0f,  -10.0f, 6.0f,   -10.0f, 10.0f,
    -10.0f, 6.0f,   10.0f,  10.0f,  -10.0f, 6.0f,   10.0f,  -10.0f, -10.0f, 6.0f,

    -10.0f, -10.0f, 10.0f,  2.0f, //-x
    -10.0f, -10.0f, -10.0f, 2.0f,   -10.0f, 10.0f,  -10.0f, 2.0f,   -10.0f, 10.0f,
    -10.0f, 2.0f,   -10.0f, 10.0f,  10.0f,  2.0f,   -10.0f, -10.0f, 10.0f,  2.0f,

    10.0f,  -10.0f, -10.0f, 1.0f, //+x
    10.0f,  -10.0f, 10.0f,  1.0f,   10.0f,  10.0f,  10.0f,  1.0f,   10.0f,  10.0f,
    10.0f,  1.0f,   10.0f,  10.0f,  -10.0f, 1.0f,   10.0f,  -10.0f, -10.0f, 1.0f,

    -10.0f, -10.0f, 10.0f,  5.0f, // +z
    -10.0f, 10.0f,  10.0f,  5.0f,   10.0f,  10.0f,  10.0f,  5.0f,   10.0f,  10.0f,
    10.0f,  5.0f,   10.0f,  -10.0f, 10.0f,  5.0f,   -10.0f, -10.0f, 10.0f,  5.0f,

    -10.0f, 10.0f,  -10.0f, 3.0f, //+y
    10.0f,  10.0f,  -10.0f, 3.0f,   10.0f,  10.0f,  10.0f,  3.0f,   10.0f,  10.0f,
    10.0f,  3.0f,   -10.0f, 10.0f,  10.0f,  3.0f,   -10.0f, 10.0f,  -10.0f, 3.0f,

    10.0f,  -10.0f, 10.0f,  4.0f, //-y
    10.0f,  -10.0f, -10.0f, 4.0f,   -10.0f, -10.0f, -10.0f, 4.0f,   -10.0f, -10.0f,
    -10.0f, 4.0f,   -10.0f, -10.0f, 10.0f,  4.0f,   10.0f,  -10.0f, 10.0f,  4.0f,
};

static unsigned char gPipelineStatsCharArray[2048] = {};
static bstring       gPipelineStats = bfromarr(gPipelineStatsCharArray);

void reloadRequest(void*)
{
    ReloadDesc reload{ RELOAD_TYPE_SHADER };
    requestReload(&reload);
}

const char* gWindowTestScripts[] = { "TestFullScreen.lua", "TestCenteredWindow.lua", "TestNonCenteredWindow.lua", "TestBorderless.lua" };

static void add_attribute(VertexLayout* layout, ShaderSemantic semantic, TinyImageFormat format, uint32_t offset, uint32_t binding = 0)
{
    uint32_t n_attr = layout->mAttribCount++;

    VertexAttrib* attr = layout->mAttribs + n_attr;

    attr->mSemantic = semantic;
    attr->mFormat = format;
    attr->mBinding = binding;
    attr->mLocation = n_attr;
    attr->mOffset = offset;
}

static void copy_attribute(VertexLayout* layout, void* buffer_data, uint32_t offset, uint32_t size, uint32_t vcount, void* data)
{
    uint8_t* dst_data = static_cast<uint8_t*>(buffer_data);
    uint8_t* src_data = static_cast<uint8_t*>(data);
    for (uint32_t i = 0; i < vcount; ++i)
    {
        memcpy(dst_data + offset, src_data, size);

        dst_data += layout->mBindings[0].mStride;
        src_data += size;
    }
}

static void generate_water_buffers()
{
    gWaterVertexLayout = {};
    float    vertices[] = { -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f, 1.0f, 1.0f };
    uint16_t indices[] = { 0, 1, 2, 0, 2, 3 };

    uint32_t vertexCount = sizeof(vertices) / 8;

    gWaterVertexLayout.mBindingCount = 1;

    //  Vertex Buffer
    //  0-8 sq positions
    gWaterVertexLayout.mBindings[0].mStride = 8;
    size_t vsize = vertexCount * gWaterVertexLayout.mBindings[0].mStride;
    size_t bufferSize = vsize;
    void*  bufferData = tf_calloc(1, bufferSize);


    add_attribute(&gWaterVertexLayout, SEMANTIC_POSITION, TinyImageFormat_R32G32_SFLOAT, 0);

    copy_attribute(&gWaterVertexLayout, bufferData, 0, 8, vertexCount, vertices);


    gWaterIndexCount = sizeof(indices) / sizeof(uint16_t);

    BufferLoadDesc waterVbDesc = {};
    waterVbDesc.mDesc.mDescriptors = DESCRIPTOR_TYPE_VERTEX_BUFFER;
    waterVbDesc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_GPU_ONLY;
    waterVbDesc.mDesc.mSize = bufferSize;
    waterVbDesc.pData = bufferData;
    waterVbDesc.ppBuffer = &pWaterVertexBuffer;
    addResource(&waterVbDesc, nullptr);

    BufferLoadDesc WaterIbDesc = {};
    WaterIbDesc.mDesc.mDescriptors = DESCRIPTOR_TYPE_INDEX_BUFFER;
    WaterIbDesc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_GPU_ONLY;
    WaterIbDesc.mDesc.mSize = sizeof(indices);
    WaterIbDesc.pData = indices;
    WaterIbDesc.ppBuffer = &pWaterIndexBuffer;
    addResource(&WaterIbDesc, nullptr);

    //  Instance Buffer
    //  0-12 position
    //  12-16 density
    //  16-28 velocity
    //  28-32 pressure

    Particle* instancePosition = (Particle*)tf_calloc(1, sizeof(Particle) * gMaxNumParticles);
    for (uint32_t i = 0; i < gMaxNumParticles; i++)
    {
        instancePosition[i] = { { randomFloat(0, gUniformDataWater.width), randomFloat(0, gUniformDataWater.height),
                                  randomFloat(0, gUniformDataWater.depth) },
                                0.0f,
                                { 0.0f, -0.0f, -0.0f },
                                0.0f };
    }

    BufferLoadDesc instanceVbDesc = {};
    instanceVbDesc.mDesc.mDescriptors = DESCRIPTOR_TYPE_RW_BUFFER | DESCRIPTOR_TYPE_BUFFER;
    instanceVbDesc.mDesc.mFirstElement = 0;
    instanceVbDesc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_GPU_ONLY;
    instanceVbDesc.mDesc.mSize = gMaxNumParticles * sizeof(Particle);
    instanceVbDesc.mDesc.mStructStride = sizeof(Particle);
    instanceVbDesc.mDesc.mElementCount = gMaxNumParticles;
    instanceVbDesc.mDesc.pName = "Particles Buffer";
    instanceVbDesc.pData = instancePosition;
    instanceVbDesc.ppBuffer = &pWaterInstanceBuffer;
    addResource(&instanceVbDesc, nullptr);

    BufferLoadDesc CellBufferDesc = {};
    CellBufferDesc.mDesc.mDescriptors = DESCRIPTOR_TYPE_RW_BUFFER;
    CellBufferDesc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_GPU_ONLY;
    CellBufferDesc.mDesc.mSize = sizeof(Cell) * NUM_CELLS;
    CellBufferDesc.mDesc.mStructStride = sizeof(Cell);
    CellBufferDesc.mDesc.mElementCount = NUM_CELLS;
    CellBufferDesc.mDesc.pName = "Cells Buffer";
    CellBufferDesc.pData = NULL;
    CellBufferDesc.ppBuffer = &pCellBuffer;
    addResource(&CellBufferDesc, nullptr);

    BufferLoadDesc ParticleIDBufferDesc = {};
    ParticleIDBufferDesc.mDesc.mDescriptors = DESCRIPTOR_TYPE_RW_BUFFER;
    ParticleIDBufferDesc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_GPU_ONLY;
    ParticleIDBufferDesc.mDesc.mSize = sizeof(uint32_t) * gMaxNumParticles;
    ParticleIDBufferDesc.mDesc.mStructStride = sizeof(uint32_t);
    ParticleIDBufferDesc.mDesc.mElementCount = gMaxNumParticles;
    ParticleIDBufferDesc.mDesc.pName = "ParticleIDs Buffer";
    ParticleIDBufferDesc.pData = NULL;
    ParticleIDBufferDesc.ppBuffer = &pParticleIDBuffer;
    addResource(&ParticleIDBufferDesc, nullptr);

    BufferLoadDesc SumBufferDesc = {};
    SumBufferDesc.mDesc.mDescriptors = DESCRIPTOR_TYPE_RW_BUFFER;
    SumBufferDesc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_GPU_ONLY;
    SumBufferDesc.mDesc.mSize = sizeof(uint32_t);
    SumBufferDesc.mDesc.mStructStride = sizeof(uint32_t);
    SumBufferDesc.mDesc.mElementCount = 1;
    SumBufferDesc.mDesc.pName = "Sum Buffer";
    SumBufferDesc.pData = NULL;
    SumBufferDesc.ppBuffer = &pSumBuffer;
    addResource(&SumBufferDesc, nullptr);

    waitForAllResourceLoads();
    tf_free(bufferData);
    tf_free(instancePosition);

}







class Transformations: public IApp
{
public:
    bool Init()
    {
        // FILE PATHS
        fsSetPathForResourceDir(pSystemFileIO, RM_CONTENT, RD_SHADER_BINARIES, "CompiledShaders");
        fsSetPathForResourceDir(pSystemFileIO, RM_CONTENT, RD_TEXTURES, "Textures");
        fsSetPathForResourceDir(pSystemFileIO, RM_CONTENT, RD_FONTS, "Fonts");
        fsSetPathForResourceDir(pSystemFileIO, RM_DEBUG, RD_SCREENSHOTS, "Screenshots");
        fsSetPathForResourceDir(pSystemFileIO, RM_CONTENT, RD_SCRIPTS, "Scripts");
        fsSetPathForResourceDir(pSystemFileIO, RM_DEBUG, RD_DEBUG, "Debug");

        // window and renderer setup
        RendererDesc settings;
        memset(&settings, 0, sizeof(settings));
        settings.mD3D11Supported = true;
        settings.mGLESSupported = true;
        initRenderer(GetName(), &settings, &pRenderer);
        // check for init success
        if (!pRenderer)
            return false;

        if (pRenderer->pGpu->mSettings.mPipelineStatsQueries)
        {
            QueryPoolDesc poolDesc = {};
            poolDesc.mQueryCount = 3; // The count is 3 due to quest & multi-view use otherwise 2 is enough as we use 2 queries.
            poolDesc.mType = QUERY_TYPE_PIPELINE_STATISTICS;
            for (uint32_t i = 0; i < gDataBufferCount; ++i)
            {
                addQueryPool(pRenderer, &poolDesc, &pPipelineStatsQueryPool[i]);
            }
        }

        QueueDesc queueDesc = {};
        queueDesc.mType = QUEUE_TYPE_GRAPHICS;
        queueDesc.mFlag = QUEUE_FLAG_INIT_MICROPROFILE;
        addQueue(pRenderer, &queueDesc, &pGraphicsQueue);

        GpuCmdRingDesc cmdRingDesc = {};
        cmdRingDesc.pQueue = pGraphicsQueue;
        cmdRingDesc.mPoolCount = gDataBufferCount;
        cmdRingDesc.mCmdPerPoolCount = 1;
        cmdRingDesc.mAddSyncPrimitives = true;
        addGpuCmdRing(pRenderer, &cmdRingDesc, &gGraphicsCmdRing);

        addSemaphore(pRenderer, &pImageAcquiredSemaphore);

        initResourceLoaderInterface(pRenderer);

        // Loads Skybox Textures
        for (int i = 0; i < 6; ++i)
        {
            TextureLoadDesc textureDesc = {};
            textureDesc.pFileName = pSkyBoxImageFileNames[i];
            textureDesc.ppTexture = &pSkyBoxTextures[i];
            // Textures representing color should be stored in SRGB or HDR format
            textureDesc.mCreationFlag = TEXTURE_CREATION_FLAG_SRGB;
            addResource(&textureDesc, NULL);
        }

        SamplerDesc samplerDesc = { FILTER_LINEAR,
                                    FILTER_LINEAR,
                                    MIPMAP_MODE_NEAREST,
                                    ADDRESS_MODE_CLAMP_TO_EDGE,
                                    ADDRESS_MODE_CLAMP_TO_EDGE,
                                    ADDRESS_MODE_CLAMP_TO_EDGE };
        addSampler(pRenderer, &samplerDesc, &pSamplerSkyBox);

        uint64_t       skyBoxDataSize = 4 * 6 * 6 * sizeof(float);
        BufferLoadDesc skyboxVbDesc = {};
        skyboxVbDesc.mDesc.mDescriptors = DESCRIPTOR_TYPE_VERTEX_BUFFER;
        skyboxVbDesc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_GPU_ONLY;
        skyboxVbDesc.mDesc.mSize = skyBoxDataSize;
        skyboxVbDesc.pData = gSkyBoxPoints;
        skyboxVbDesc.ppBuffer = &pSkyBoxVertexBuffer;
        addResource(&skyboxVbDesc, NULL);

        BufferLoadDesc ubDesc = {};
        ubDesc.mDesc.mDescriptors = DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        ubDesc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
        ubDesc.mDesc.mFlags = BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
        ubDesc.pData = NULL;
        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            ubDesc.mDesc.pName = "ProjViewUniformBuffer";
            ubDesc.mDesc.mSize = sizeof(UniformBlock);
            ubDesc.ppBuffer = &pProjViewUniformBuffer[i];
            addResource(&ubDesc, NULL);
            ubDesc.mDesc.pName = "SkyboxUniformBuffer";
            ubDesc.mDesc.mSize = sizeof(UniformBlockSky);
            ubDesc.ppBuffer = &pSkyboxUniformBuffer[i];
            addResource(&ubDesc, NULL);
            ubDesc.mDesc.pName = "WaterUniformBuffer";
            ubDesc.mDesc.mSize = sizeof(UniformBlockWater);
            ubDesc.ppBuffer = &pWaterUniformBuffer[i];
            addResource(&ubDesc, NULL);
        }


        // Load fonts
        FontDesc font = {};
        font.pFontPath = "TitilliumText/TitilliumText-Bold.otf";
        fntDefineFonts(&font, 1, &gFontID);

        FontSystemDesc fontRenderDesc = {};
        fontRenderDesc.pRenderer = pRenderer;
        if (!initFontSystem(&fontRenderDesc))
            return false; // report?

        // Initialize Forge User Interface Rendering
        UserInterfaceDesc uiRenderDesc = {};
        uiRenderDesc.pRenderer = pRenderer;
        initUserInterface(&uiRenderDesc);

        // Initialize micro profiler and its UI.
        ProfilerDesc profiler = {};
        profiler.pRenderer = pRenderer;
        profiler.mWidthUI = mSettings.mWidth;
        profiler.mHeightUI = mSettings.mHeight;
        initProfiler(&profiler);

        // Gpu profiler can only be added after initProfile.
        gGpuProfileToken = addGpuProfiler(pRenderer, pGraphicsQueue, "Graphics");

        /************************************************************************/
        // GUI
        /************************************************************************/
        UIComponentDesc guiDesc = {};
        guiDesc.mStartPosition = vec2(mSettings.mWidth * 0.01f, mSettings.mHeight * 0.2f);
        uiCreateComponent(GetName(), &guiDesc, &pGuiWindow);

        SliderFloatWidget sliderWidgetf;
        sliderWidgetf.mMin = 1.0f;
        sliderWidgetf.mMax = 150.0f;
        sliderWidgetf.mStep = 0.001f;
        sliderWidgetf.pData = &gUniformDataWater.width;
        uiCreateComponentWidget(pGuiWindow, "Container Width", &sliderWidgetf, WIDGET_TYPE_SLIDER_FLOAT);
        sliderWidgetf.pData = &gUniformDataWater.height;
        uiCreateComponentWidget(pGuiWindow, "Container Height", &sliderWidgetf, WIDGET_TYPE_SLIDER_FLOAT);
        sliderWidgetf.pData = &gUniformDataWater.depth;
        uiCreateComponentWidget(pGuiWindow, "Container Depth", &sliderWidgetf, WIDGET_TYPE_SLIDER_FLOAT);
        sliderWidgetf.pData = &gUniformDataWater.dt;
        sliderWidgetf.mMin = 0.0f;
        sliderWidgetf.mMax = 0.04f;
        sliderWidgetf.mStep = 0.000001f;
        CheckboxWidget checkbox{ &gDynamicDeltaTime };
        uiCreateComponentWidget(pGuiWindow, "Framerate Based Delta Time", &checkbox, WIDGET_TYPE_CHECKBOX);
        uiCreateComponentWidget(pGuiWindow, "Delta Time", &sliderWidgetf, WIDGET_TYPE_SLIDER_FLOAT);
        sliderWidgetf.pData = &gUniformDataWater.pressureMult;
        sliderWidgetf.mMin = 0.0f;
        sliderWidgetf.mStep = 0.001f;
        sliderWidgetf.mMax = 1200.0f;
        uiCreateComponentWidget(pGuiWindow, "Pressure Mult", &sliderWidgetf, WIDGET_TYPE_SLIDER_FLOAT);
        sliderWidgetf.pData = &gUniformDataWater.smoothingRadius;
        sliderWidgetf.mMax = 5.0f;
        uiCreateComponentWidget(pGuiWindow, "Smoothing Radius", &sliderWidgetf, WIDGET_TYPE_SLIDER_FLOAT);
        sliderWidgetf.pData = &gUniformDataWater.desiredDensity;
        sliderWidgetf.mMax = 1.5f;
        uiCreateComponentWidget(pGuiWindow, "Desired Density", &sliderWidgetf, WIDGET_TYPE_SLIDER_FLOAT);
        sliderWidgetf.pData = &gUniformDataWater.dragCo;
        sliderWidgetf.mMax = 0.08f;
        uiCreateComponentWidget(pGuiWindow, "Drag Coefficient", &sliderWidgetf, WIDGET_TYPE_SLIDER_FLOAT);
        sliderWidgetf.pData = &gUniformDataWater.bump;
        sliderWidgetf.mMax = 0.4f;
        uiCreateComponentWidget(pGuiWindow, "Bump Const", &sliderWidgetf, WIDGET_TYPE_SLIDER_FLOAT);
        sliderWidgetf.pData = &gUniformDataWater.viscosity;
        sliderWidgetf.mMax = 0.5f;
        uiCreateComponentWidget(pGuiWindow, "Viscosity", &sliderWidgetf, WIDGET_TYPE_SLIDER_FLOAT);
        sliderWidgetf.pData = &gWaterSize;
        sliderWidgetf.mMax = 10.0f;
        uiCreateComponentWidget(pGuiWindow, "Water Size", &sliderWidgetf, WIDGET_TYPE_SLIDER_FLOAT);
        SliderUintWidget sliderWidgetu;
        sliderWidgetu.mMin = 0;
        sliderWidgetu.mMax = gMaxNumParticles;
        sliderWidgetu.mStep = 32;
        sliderWidgetu.pData = &gNumParticles;
        uiCreateComponentWidget(pGuiWindow, "Number Of Particles", &sliderWidgetu, WIDGET_TYPE_SLIDER_UINT);

        if (pRenderer->pGpu->mSettings.mPipelineStatsQueries)
        {
            static float4     color = { 1.0f, 1.0f, 1.0f, 1.0f };
            DynamicTextWidget statsWidget;
            statsWidget.pText = &gPipelineStats;
            statsWidget.pColor = &color;
            uiCreateComponentWidget(pGuiWindow, "Pipeline Stats", &statsWidget, WIDGET_TYPE_DYNAMIC_TEXT);
        }


        const uint32_t numScripts = TF_ARRAY_COUNT(gWindowTestScripts);
        LuaScriptDesc  scriptDescs[numScripts] = {};
        for (uint32_t i = 0; i < numScripts; ++i)
            scriptDescs[i].pScriptFileName = gWindowTestScripts[i];
        DEFINE_LUA_SCRIPTS(scriptDescs, numScripts);

        waitForAllResourceLoads();

        CameraMotionParameters cmp{ 160.0f, 600.0f, 200.0f };
        vec3                   camPos{ 48.0f, 48.0f, 20.0f };
        vec3                   lookAt{ vec3(0) };

        pCameraController = initFpsCameraController(camPos, lookAt);

        pCameraController->setMotionParameters(cmp);

        InputSystemDesc inputDesc = {};
        inputDesc.pRenderer = pRenderer;
        inputDesc.pWindow = pWindow;
        inputDesc.pJoystickTexture = "circlepad.tex";
        if (!initInputSystem(&inputDesc))
            return false;

        // App Actions
        InputActionDesc actionDesc = { DefaultInputActions::DUMP_PROFILE_DATA,
                                       [](InputActionContext* ctx)
                                       {
                                           dumpProfileData(((Renderer*)ctx->pUserData)->pName);
                                           return true;
                                       },
                                       pRenderer };
        addInputAction(&actionDesc);
        actionDesc = { DefaultInputActions::TOGGLE_FULLSCREEN,
                       [](InputActionContext* ctx)
                       {
                           WindowDesc* winDesc = ((IApp*)ctx->pUserData)->pWindow;
                           if (winDesc->fullScreen)
                               winDesc->borderlessWindow
                                   ? setBorderless(winDesc, getRectWidth(&winDesc->clientRect), getRectHeight(&winDesc->clientRect))
                                   : setWindowed(winDesc, getRectWidth(&winDesc->clientRect), getRectHeight(&winDesc->clientRect));
                           else
                               setFullscreen(winDesc);
                           return true;
                       },
                       this };
        addInputAction(&actionDesc);
        actionDesc = { DefaultInputActions::EXIT, [](InputActionContext* ctx)
                       {
                           requestShutdown();
                           return true;
                       } };
        addInputAction(&actionDesc);
        InputActionCallback onAnyInput = [](InputActionContext* ctx)
        {
            if (ctx->mActionId > UISystemInputActions::UI_ACTION_START_ID_)
            {
                uiOnInput(ctx->mActionId, ctx->mBool, ctx->pPosition, &ctx->mFloat2);
            }

            return true;
        };

        typedef bool              (*CameraInputHandler)(InputActionContext* ctx, DefaultInputActions::DefaultInputAction action);
        static CameraInputHandler onCameraInput = [](InputActionContext* ctx, DefaultInputActions::DefaultInputAction action)
        {
            if (*(ctx->pCaptured))
            {
                float2 delta = uiIsFocused() ? float2(0.f, 0.f) : ctx->mFloat2;
                switch (action)
                {
                case DefaultInputActions::ROTATE_CAMERA:
                    pCameraController->onRotate(delta);
                    break;
                case DefaultInputActions::TRANSLATE_CAMERA:
                    pCameraController->onMove(delta);
                    break;
                case DefaultInputActions::TRANSLATE_CAMERA_VERTICAL:
                    pCameraController->onMoveY(delta[0]);
                    break;
                default:
                    break;
                }
            }
            return true;
        };
        actionDesc = { DefaultInputActions::CAPTURE_INPUT,
                       [](InputActionContext* ctx)
                       {
                           setEnableCaptureInput(!uiIsFocused() && INPUT_ACTION_PHASE_CANCELED != ctx->mPhase);
                           return true;
                       },
                       NULL };
        addInputAction(&actionDesc);
        actionDesc = { DefaultInputActions::ROTATE_CAMERA,
                       [](InputActionContext* ctx) { return onCameraInput(ctx, DefaultInputActions::ROTATE_CAMERA); }, NULL };
        addInputAction(&actionDesc);
        actionDesc = { DefaultInputActions::TRANSLATE_CAMERA,
                       [](InputActionContext* ctx) { return onCameraInput(ctx, DefaultInputActions::TRANSLATE_CAMERA); }, NULL };
        addInputAction(&actionDesc);
        actionDesc = { DefaultInputActions::TRANSLATE_CAMERA_VERTICAL,
                       [](InputActionContext* ctx) { return onCameraInput(ctx, DefaultInputActions::TRANSLATE_CAMERA_VERTICAL); }, NULL };
        addInputAction(&actionDesc);
        actionDesc = { DefaultInputActions::RESET_CAMERA, [](InputActionContext* ctx)
                       {
                           if (!uiWantTextInput())
                               pCameraController->resetView();
                           return true;
                       } };
        addInputAction(&actionDesc);
        GlobalInputActionDesc globalInputActionDesc = { GlobalInputActionDesc::ANY_BUTTON_ACTION, onAnyInput, this };
        setGlobalInputAction(&globalInputActionDesc);

        gFrameIndex = 0;

        return true;
    }

    void Exit()
    {
        exitInputSystem();

        exitCameraController(pCameraController);

        exitUserInterface();

        exitFontSystem();

        // Exit profile
        exitProfiler();


        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            removeResource(pProjViewUniformBuffer[i]);
            removeResource(pWaterUniformBuffer[i]);
            removeResource(pSkyboxUniformBuffer[i]);
            if (pRenderer->pGpu->mSettings.mPipelineStatsQueries)
            {
                removeQueryPool(pRenderer, pPipelineStatsQueryPool[i]);
            }
        }

        removeResource(pSkyBoxVertexBuffer);

        for (uint i = 0; i < 6; ++i)
            removeResource(pSkyBoxTextures[i]);

        removeSampler(pRenderer, pSamplerSkyBox);

        removeGpuCmdRing(pRenderer, &gGraphicsCmdRing);
        removeSemaphore(pRenderer, pImageAcquiredSemaphore);

        exitResourceLoaderInterface(pRenderer);

        removeQueue(pRenderer, pGraphicsQueue);

        exitRenderer(pRenderer);
        pRenderer = NULL;

        Renderer* pRenderer = NULL;
    }

    bool Load(ReloadDesc* pReloadDesc)
    {

        if (pReloadDesc->mType & RELOAD_TYPE_SHADER)
        { 
            addShaders();
            addRootSignatures();
            addDescriptorSets();
        }

        if (pReloadDesc->mType & (RELOAD_TYPE_RESIZE | RELOAD_TYPE_RENDERTARGET))
        {
            if (!addSwapChain())
                return false;

            if (!addDepthBuffer())
                return false;
        }

        if (pReloadDesc->mType & (RELOAD_TYPE_SHADER | RELOAD_TYPE_RENDERTARGET))
        {
            generate_water_buffers();
            addPipelines();
        }

        prepareDescriptorSets();

        UserInterfaceLoadDesc uiLoad = {};
        uiLoad.mColorFormat = pSwapChain->ppRenderTargets[0]->mFormat;
        uiLoad.mHeight = mSettings.mHeight;
        uiLoad.mWidth = mSettings.mWidth;
        uiLoad.mLoadType = pReloadDesc->mType;
        loadUserInterface(&uiLoad);

        FontSystemLoadDesc fontLoad = {};
        fontLoad.mColorFormat = pSwapChain->ppRenderTargets[0]->mFormat;
        fontLoad.mHeight = mSettings.mHeight;
        fontLoad.mWidth = mSettings.mWidth;
        fontLoad.mLoadType = pReloadDesc->mType;
        loadFontSystem(&fontLoad);

        initScreenshotInterface(pRenderer, pGraphicsQueue);

        return true;
    }

    void Unload(ReloadDesc* pReloadDesc)
    {
        waitQueueIdle(pGraphicsQueue);

        unloadFontSystem(pReloadDesc->mType);
        unloadUserInterface(pReloadDesc->mType);

        if (pReloadDesc->mType & (RELOAD_TYPE_SHADER | RELOAD_TYPE_RENDERTARGET))
        {
            removePipelines();
            removeResource(pWaterVertexBuffer);
            removeResource(pWaterInstanceBuffer);
            removeResource(pWaterIndexBuffer);
            removeResource(pCellBuffer);
            removeResource(pParticleIDBuffer);
            removeResource(pSumBuffer);
        }

        if (pReloadDesc->mType & (RELOAD_TYPE_RESIZE | RELOAD_TYPE_RENDERTARGET))
        {
            removeSwapChain(pRenderer, pSwapChain);
            removeRenderTarget(pRenderer, pDepthBuffer);
        }

        if (pReloadDesc->mType & RELOAD_TYPE_SHADER)
        {
            removeDescriptorSets();
            removeRootSignatures();
            removeShaders();
        }

        exitScreenshotInterface();
    }

    void Update(float deltaTime)
    {
        if (gDynamicDeltaTime)
        {
            gUniformDataWater.dt = deltaTime;
        }
        updateInputSystem(deltaTime, mSettings.mWidth, mSettings.mHeight);

        pCameraController->update(deltaTime);
        /************************************************************************/
        // Scene Update
        /************************************************************************/
        static float currentTime = 0.0f;
        currentTime += deltaTime * 1000.0f;

        // update camera with time
        mat4 viewMat = pCameraController->getViewMatrix();

        const float  aspectInverse = (float)mSettings.mHeight / (float)mSettings.mWidth;
        const float  horizontal_fov = PI / 2.0f;
        CameraMatrix projMat = CameraMatrix::perspectiveReverseZ(horizontal_fov, aspectInverse, 0.1f, 2000.0f);
        gUniformData.mProjectView = projMat * viewMat;
        gUniformData.aspectRatio1 = lerp((float)mSettings.mHeight / (float)mSettings.mWidth, 1.0f, 0.5f);
        gUniformData.aspectRatio2 = lerp((float)mSettings.mWidth / (float)mSettings.mHeight, 1.0f, 0.5f);
        gUniformData.size = gWaterSize;


        viewMat.setTranslation(vec3(0));
        gUniformDataSky = {};
        gUniformDataSky.mProjectView = projMat * viewMat;
    }

    void Draw()
    {
        if (pSwapChain->mEnableVsync != mSettings.mVSyncEnabled)
        {
            waitQueueIdle(pGraphicsQueue);
            ::toggleVSync(pRenderer, &pSwapChain);
        }

        uint32_t swapchainImageIndex;
        acquireNextImage(pRenderer, pSwapChain, pImageAcquiredSemaphore, NULL, &swapchainImageIndex);

        RenderTarget*     pRenderTarget = pSwapChain->ppRenderTargets[swapchainImageIndex];
        GpuCmdRingElement elem = getNextGpuCmdRingElement(&gGraphicsCmdRing, true, 1);

        // Stall if CPU is running "gDataBufferCount" frames ahead of GPU
        FenceStatus fenceStatus;
        getFenceStatus(pRenderer, elem.pFence, &fenceStatus);
        if (fenceStatus == FENCE_STATUS_INCOMPLETE)
            waitForFences(pRenderer, 1, &elem.pFence);

        // Update uniform buffers
        BufferUpdateDesc viewProjCbv = { pProjViewUniformBuffer[gFrameIndex] };
        beginUpdateResource(&viewProjCbv);
        memcpy(viewProjCbv.pMappedData, &gUniformData, sizeof(gUniformData));
        endUpdateResource(&viewProjCbv);

        BufferUpdateDesc waterUniformCbv = { pWaterUniformBuffer[gFrameIndex] };
        beginUpdateResource(&waterUniformCbv);
        memcpy(waterUniformCbv.pMappedData, &gUniformDataWater, sizeof(gUniformDataWater));
        endUpdateResource(&waterUniformCbv);

        BufferUpdateDesc skyboxViewProjCbv = { pSkyboxUniformBuffer[gFrameIndex] };
        beginUpdateResource(&skyboxViewProjCbv);
        memcpy(skyboxViewProjCbv.pMappedData, &gUniformDataSky, sizeof(gUniformDataSky));
        endUpdateResource(&skyboxViewProjCbv);

        // Reset cmd pool for this frame
        resetCmdPool(pRenderer, elem.pCmdPool);

        if (pRenderer->pGpu->mSettings.mPipelineStatsQueries)
        {
            QueryData data3D = {};
            QueryData data2D = {};
            getQueryData(pRenderer, pPipelineStatsQueryPool[gFrameIndex], 0, &data3D);
            getQueryData(pRenderer, pPipelineStatsQueryPool[gFrameIndex], 1, &data2D);
            bformat(&gPipelineStats,
                    "\n"
                    "Pipeline Stats 3D:\n"
                    "    VS invocations:      %u\n"
                    "    PS invocations:      %u\n"
                    "    Clipper invocations: %u\n"
                    "    IA primitives:       %u\n"
                    "    Clipper primitives:  %u\n"
                    "\n"
                    "Pipeline Stats 2D UI:\n"
                    "    VS invocations:      %u\n"
                    "    PS invocations:      %u\n"
                    "    Clipper invocations: %u\n"
                    "    IA primitives:       %u\n"
                    "    Clipper primitives:  %u\n",
                    data3D.mPipelineStats.mVSInvocations, data3D.mPipelineStats.mPSInvocations, data3D.mPipelineStats.mCInvocations,
                    data3D.mPipelineStats.mIAPrimitives, data3D.mPipelineStats.mCPrimitives, data2D.mPipelineStats.mVSInvocations,
                    data2D.mPipelineStats.mPSInvocations, data2D.mPipelineStats.mCInvocations, data2D.mPipelineStats.mIAPrimitives,
                    data2D.mPipelineStats.mCPrimitives);
        }

        Cmd* cmd = elem.pCmds[0];
        beginCmd(cmd);


        cmdBeginGpuFrameProfile(cmd, gGpuProfileToken);
        if (pRenderer->pGpu->mSettings.mPipelineStatsQueries)
        {
            cmdResetQuery(cmd, pPipelineStatsQueryPool[gFrameIndex], 0, 2);
            QueryDesc queryDesc = { 0 };
            cmdBeginQuery(cmd, pPipelineStatsQueryPool[gFrameIndex], &queryDesc);
        }

        RenderTargetBarrier barriers[] = {
            { pRenderTarget, RESOURCE_STATE_PRESENT, RESOURCE_STATE_RENDER_TARGET },
        };
        cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 1, barriers);

        // Compute Shaders
        BufferBarrier WaterBufferBarriers[4];
        WaterBufferBarriers[0].pBuffer = pWaterInstanceBuffer;
        WaterBufferBarriers[0].mCurrentState = WaterBufferBarriers[0].mNewState = RESOURCE_STATE_UNORDERED_ACCESS;
        WaterBufferBarriers[1].pBuffer = pCellBuffer;
        WaterBufferBarriers[1].mCurrentState = WaterBufferBarriers[1].mNewState = RESOURCE_STATE_UNORDERED_ACCESS;
        WaterBufferBarriers[2].pBuffer = pParticleIDBuffer;
        WaterBufferBarriers[2].mCurrentState = WaterBufferBarriers[1].mNewState = RESOURCE_STATE_UNORDERED_ACCESS;
        WaterBufferBarriers[2].pBuffer = pSumBuffer;
        WaterBufferBarriers[2].mCurrentState = WaterBufferBarriers[1].mNewState = RESOURCE_STATE_UNORDERED_ACCESS;

        cmdBeginGpuTimestampQuery(cmd, gGpuProfileToken, "Water Simulation");

        cmdBindPipeline(cmd, pWaterResetCellsCompPipeline);
        cmdBindDescriptorSet(cmd, gFrameIndex, pDescriptorSetWater);
        cmdDispatch(cmd, NUM_CELLS / 32, 1, 1);
        cmdBindPipeline(cmd, pWaterResetSumCompPipeline);
        cmdBindDescriptorSet(cmd, gFrameIndex, pDescriptorSetWater);
        cmdDispatch(cmd, 1, 1, 1);
        cmdResourceBarrier(cmd, 2, WaterBufferBarriers, 0, NULL, 0, NULL);

        cmdBindPipeline(cmd, pWaterCountCellsCompPipeline);
        cmdBindDescriptorSet(cmd, gFrameIndex, pDescriptorSetWater);
        cmdDispatch(cmd, gNumParticles / 32, 1, 1);
        cmdResourceBarrier(cmd, 2, WaterBufferBarriers, 0, NULL, 0, NULL);

        cmdBindPipeline(cmd, pWaterFindCellDataPosCompPipeline);
        cmdBindDescriptorSet(cmd, gFrameIndex, pDescriptorSetWater);
        cmdDispatch(cmd, NUM_CELLS / 32, 1, 1);
        cmdResourceBarrier(cmd, 2, WaterBufferBarriers, 0, NULL, 0, NULL);

        cmdBindPipeline(cmd, pWaterParticlesIntoCellsCompPipeline);
        cmdBindDescriptorSet(cmd, gFrameIndex, pDescriptorSetWater);
        cmdDispatch(cmd, gNumParticles / 32, 1, 1);
        cmdResourceBarrier(cmd, 2, WaterBufferBarriers, 0, NULL, 0, NULL);

        cmdBindPipeline(cmd, pWaterCalculatePressureCompPipeline);
        cmdBindDescriptorSet(cmd, gFrameIndex, pDescriptorSetWater);
        cmdDispatch(cmd, gNumParticles / 32, 1, 1);
        cmdResourceBarrier(cmd, 2, WaterBufferBarriers, 0, NULL, 0, NULL);

        cmdBindPipeline(cmd, pWaterMoveCompPipeline);
        cmdBindDescriptorSet(cmd, gFrameIndex, pDescriptorSetWater);
        cmdDispatch(cmd, gNumParticles / 32, 1, 1);
        cmdResourceBarrier(cmd, 2, WaterBufferBarriers, 0, NULL, 0, NULL);

        cmdEndGpuTimestampQuery(cmd, gGpuProfileToken);

        // simply record the screen cleaning command
        BindRenderTargetsDesc bindRenderTargets = {};
        bindRenderTargets.mRenderTargetCount = 1;
        bindRenderTargets.mRenderTargets[0] = { pRenderTarget, LOAD_ACTION_CLEAR };
        bindRenderTargets.mDepthStencil = { pDepthBuffer, LOAD_ACTION_CLEAR };
        cmdBindRenderTargets(cmd, &bindRenderTargets);
        cmdSetViewport(cmd, 0.0f, 0.0f, (float)pRenderTarget->mWidth, (float)pRenderTarget->mHeight, 0.0f, 1.0f);
        cmdSetScissor(cmd, 0, 0, pRenderTarget->mWidth, pRenderTarget->mHeight);

        const uint32_t skyboxVbStride = sizeof(float) * 4;


        // draw skybox
        cmdBeginGpuTimestampQuery(cmd, gGpuProfileToken, "Draw Skybox");
        cmdSetViewport(cmd, 0.0f, 0.0f, (float)pRenderTarget->mWidth, (float)pRenderTarget->mHeight, 1.0f, 1.0f);
        cmdBindPipeline(cmd, pSkyBoxDrawPipeline);
        cmdBindDescriptorSet(cmd, 0, pDescriptorSetTexture);
        cmdBindDescriptorSet(cmd, gFrameIndex * 2 + 0, pDescriptorSetUniforms);
        cmdBindVertexBuffer(cmd, 1, &pSkyBoxVertexBuffer, &skyboxVbStride, NULL);
        cmdDraw(cmd, 36, 0);
        cmdSetViewport(cmd, 0.0f, 0.0f, (float)pRenderTarget->mWidth, (float)pRenderTarget->mHeight, 0.0f, 1.0f);
        cmdEndGpuTimestampQuery(cmd, gGpuProfileToken);

        ////// draw Water
        cmdBeginGpuTimestampQuery(cmd, gGpuProfileToken, "Draw Water");

        Pipeline* pipeline = pWaterPipeline;

        cmdBindPipeline(cmd, pipeline);
        cmdBindDescriptorSet(cmd, gFrameIndex * 2 + 1, pDescriptorSetUniforms);
        cmdBindDescriptorSet(cmd, 0, pDescriptorSetGraphicsParticles);

        Buffer* vertAndInst[] = { pWaterVertexBuffer };
        uint32_t strides[] = { gWaterVertexLayout.mBindings[0].mStride };
        cmdBindVertexBuffer(cmd, 1, &pWaterVertexBuffer, strides, nullptr);
        cmdBindIndexBuffer(cmd, pWaterIndexBuffer, INDEX_TYPE_UINT16, 0);

        cmdDrawIndexedInstanced(cmd, gWaterIndexCount, 0, gNumParticles, 0, 0);

        cmdEndGpuTimestampQuery(cmd, gGpuProfileToken);

        if (pRenderer->pGpu->mSettings.mPipelineStatsQueries)
        {
            QueryDesc queryDesc = { 0 };
            cmdEndQuery(cmd, pPipelineStatsQueryPool[gFrameIndex], &queryDesc);

            queryDesc = { 1 };
            cmdBeginQuery(cmd, pPipelineStatsQueryPool[gFrameIndex], &queryDesc);
        }

        bindRenderTargets = {};
        bindRenderTargets.mRenderTargetCount = 1;
        bindRenderTargets.mRenderTargets[0] = { pRenderTarget, LOAD_ACTION_LOAD };
        cmdBindRenderTargets(cmd, &bindRenderTargets);

        cmdBeginGpuTimestampQuery(cmd, gGpuProfileToken, "Draw UI");

        gFrameTimeDraw.mFontColor = 0xff00ffff;
        gFrameTimeDraw.mFontSize = 18.0f;
        gFrameTimeDraw.mFontID = gFontID;
        float2 txtSizePx = cmdDrawCpuProfile(cmd, float2(8.f, 15.f), &gFrameTimeDraw);
        cmdDrawGpuProfile(cmd, float2(8.f, txtSizePx.y + 75.f), gGpuProfileToken, &gFrameTimeDraw);

        cmdDrawUserInterface(cmd);

        cmdBindRenderTargets(cmd, NULL);
        cmdEndGpuTimestampQuery(cmd, gGpuProfileToken);

        barriers[0] = { pRenderTarget, RESOURCE_STATE_RENDER_TARGET, RESOURCE_STATE_PRESENT };
        cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 1, barriers);

        cmdEndGpuFrameProfile(cmd, gGpuProfileToken);

        if (pRenderer->pGpu->mSettings.mPipelineStatsQueries)
        {
            QueryDesc queryDesc = { 1 };
            cmdEndQuery(cmd, pPipelineStatsQueryPool[gFrameIndex], &queryDesc);
            cmdResolveQuery(cmd, pPipelineStatsQueryPool[gFrameIndex], 0, 2);
        }

        endCmd(cmd);

        FlushResourceUpdateDesc flushUpdateDesc = {};
        flushUpdateDesc.mNodeIndex = 0;
        flushResourceUpdates(&flushUpdateDesc);
        Semaphore* waitSemaphores[2] = { flushUpdateDesc.pOutSubmittedSemaphore, pImageAcquiredSemaphore };

        QueueSubmitDesc submitDesc = {};
        submitDesc.mCmdCount = 1;
        submitDesc.mSignalSemaphoreCount = 1;
        submitDesc.mWaitSemaphoreCount = TF_ARRAY_COUNT(waitSemaphores);
        submitDesc.ppCmds = &cmd;
        submitDesc.ppSignalSemaphores = &elem.pSemaphore;
        submitDesc.ppWaitSemaphores = waitSemaphores;
        submitDesc.pSignalFence = elem.pFence;
        queueSubmit(pGraphicsQueue, &submitDesc);
        QueuePresentDesc presentDesc = {};
        presentDesc.mIndex = swapchainImageIndex;
        presentDesc.mWaitSemaphoreCount = 1;
        presentDesc.pSwapChain = pSwapChain;
        presentDesc.ppWaitSemaphores = &elem.pSemaphore;
        presentDesc.mSubmitDone = true;

        queuePresent(pGraphicsQueue, &presentDesc);
        flipProfiler();

        gFrameIndex = (gFrameIndex + 1) % gDataBufferCount;
    }

    const char* GetName() { return "37_GPUWaterSimulation"; }

    bool addSwapChain()
    {
        SwapChainDesc swapChainDesc = {};
        swapChainDesc.mWindowHandle = pWindow->handle;
        swapChainDesc.mPresentQueueCount = 1;
        swapChainDesc.ppPresentQueues = &pGraphicsQueue;
        swapChainDesc.mWidth = mSettings.mWidth;
        swapChainDesc.mHeight = mSettings.mHeight;
        swapChainDesc.mImageCount = getRecommendedSwapchainImageCount(pRenderer, &pWindow->handle);
        swapChainDesc.mColorFormat = getSupportedSwapchainFormat(pRenderer, &swapChainDesc, COLOR_SPACE_SDR_SRGB);
        swapChainDesc.mColorSpace = COLOR_SPACE_SDR_SRGB;
        swapChainDesc.mEnableVsync = mSettings.mVSyncEnabled;
        swapChainDesc.mFlags = SWAP_CHAIN_CREATION_FLAG_ENABLE_FOVEATED_RENDERING_VR;
        ::addSwapChain(pRenderer, &swapChainDesc, &pSwapChain);

        return pSwapChain != NULL;
    }

    bool addDepthBuffer()
    {
        // Add depth buffer
        RenderTargetDesc depthRT = {};
        depthRT.mArraySize = 1;
        depthRT.mClearValue.depth = 0.0f;
        depthRT.mClearValue.stencil = 0;
        depthRT.mDepth = 1;
        depthRT.mFormat = TinyImageFormat_D32_SFLOAT;
        depthRT.mStartState = RESOURCE_STATE_DEPTH_WRITE;
        depthRT.mHeight = mSettings.mHeight;
        depthRT.mSampleCount = SAMPLE_COUNT_1;
        depthRT.mSampleQuality = 0;
        depthRT.mWidth = mSettings.mWidth;
        depthRT.mFlags = TEXTURE_CREATION_FLAG_ON_TILE | TEXTURE_CREATION_FLAG_VR_MULTIVIEW;
        addRenderTarget(pRenderer, &depthRT, &pDepthBuffer);

        return pDepthBuffer != NULL;
    }

    void addDescriptorSets()
    {
        DescriptorSetDesc desc = { pRootSignature, DESCRIPTOR_UPDATE_FREQ_NONE, 1 };
        addDescriptorSet(pRenderer, &desc, &pDescriptorSetTexture);
        desc = { pRootSignature, DESCRIPTOR_UPDATE_FREQ_PER_FRAME, gDataBufferCount * 2 };
        addDescriptorSet(pRenderer, &desc, &pDescriptorSetUniforms);
        desc = { pRootSignature, DESCRIPTOR_UPDATE_FREQ_PER_DRAW, 1 };
        addDescriptorSet(pRenderer, &desc, &pDescriptorSetGraphicsParticles);
        desc = { pRootSignatureWaterSimulation, DESCRIPTOR_UPDATE_FREQ_PER_DRAW, gDataBufferCount };
        addDescriptorSet(pRenderer, &desc, &pDescriptorSetWater);
    }

    void removeDescriptorSets()
    {
        removeDescriptorSet(pRenderer, pDescriptorSetTexture);
        removeDescriptorSet(pRenderer, pDescriptorSetUniforms);
        removeDescriptorSet(pRenderer, pDescriptorSetWater);
        removeDescriptorSet(pRenderer, pDescriptorSetGraphicsParticles);
    }

    void addRootSignatures()
    {
        Shader*  shaders[3];
        uint32_t shadersCount = 2;
        shaders[0] = pWaterShader;
        shaders[1] = pSkyBoxDrawShader;

        const char*       pStaticSamplers[] = { "uSampler0" };
        RootSignatureDesc rootDesc = {};
        rootDesc.mStaticSamplerCount = 1;
        rootDesc.ppStaticSamplerNames = pStaticSamplers;
        rootDesc.ppStaticSamplers = &pSamplerSkyBox;
        rootDesc.mShaderCount = shadersCount;
        rootDesc.ppShaders = shaders;
        addRootSignature(pRenderer, &rootDesc, &pRootSignature);


        Shader* waterSimulationShaders[] = { pWaterMoveShader, pWaterResetCellsShader, pWaterParticlesIntoCellsShader, pWaterCalculatePressureShader, pWaterFindCellDataPosShader, pWaterCountCellsShader, pWaterResetSumShader };
        RootSignatureDesc waterSimulationRootSignatureDesc = {};
        waterSimulationRootSignatureDesc.ppShaders = waterSimulationShaders;
        waterSimulationRootSignatureDesc.mShaderCount = (uint32_t)TF_ARRAY_COUNT(waterSimulationShaders);
        addRootSignature(pRenderer, &waterSimulationRootSignatureDesc, &pRootSignatureWaterSimulation);

        
    }

    void removeRootSignatures() {
        removeRootSignature(pRenderer, pRootSignature);
        removeRootSignature(pRenderer, pRootSignatureWaterSimulation);
    }

    void addShaders()
    {
        ShaderLoadDesc skyShader = {};
        skyShader.mStages[0].pFileName = "skybox.vert";
        skyShader.mStages[1].pFileName = "skybox.frag";

        ShaderLoadDesc basicShader = {};
        basicShader.mStages[0].pFileName = "basic.vert";
        basicShader.mStages[1].pFileName = "basic.frag";

        ShaderLoadDesc waterMoveShader = {};
        waterMoveShader.mStages[0].pFileName = "waterMove.comp";

        ShaderLoadDesc waterResetCells = {};
        waterResetCells.mStages[0].pFileName = "waterResetCells.comp";

        ShaderLoadDesc waterCalculatePressure = {};
        waterCalculatePressure.mStages[0].pFileName = "waterCalculatePressure.comp";

        ShaderLoadDesc waterParticlesIntoCells = {};
        waterParticlesIntoCells.mStages[0].pFileName = "waterParticlesIntoCells.comp";

        ShaderLoadDesc waterCountCells = {};
        waterCountCells.mStages[0].pFileName = "waterCountCells.comp";

        ShaderLoadDesc waterFindCellDataPos = {};
        waterFindCellDataPos.mStages[0].pFileName = "waterFindCellDataPos.comp";

        ShaderLoadDesc waterResetSum = {};
        waterResetSum.mStages[0].pFileName = "waterResetSum.comp";

        addShader(pRenderer, &skyShader, &pSkyBoxDrawShader);
        addShader(pRenderer, &basicShader, &pWaterShader);
        addShader(pRenderer, &waterMoveShader, &pWaterMoveShader);
        addShader(pRenderer, &waterResetCells, &pWaterResetCellsShader);
        addShader(pRenderer, &waterCalculatePressure, &pWaterCalculatePressureShader);
        addShader(pRenderer, &waterParticlesIntoCells, &pWaterParticlesIntoCellsShader);
        addShader(pRenderer, &waterCountCells, &pWaterCountCellsShader);
        addShader(pRenderer, &waterFindCellDataPos, &pWaterFindCellDataPosShader);
        addShader(pRenderer, &waterResetSum, &pWaterResetSumShader);
    }

    void removeShaders()
    {
        removeShader(pRenderer, pWaterShader);
        removeShader(pRenderer, pSkyBoxDrawShader);
        removeShader(pRenderer, pWaterMoveShader);
        removeShader(pRenderer, pWaterResetCellsShader);
        removeShader(pRenderer, pWaterCalculatePressureShader);
        removeShader(pRenderer, pWaterParticlesIntoCellsShader);
        removeShader(pRenderer, pWaterCountCellsShader);
        removeShader(pRenderer, pWaterFindCellDataPosShader);
        removeShader(pRenderer, pWaterResetSumShader);
    }

    void addPipelines()
    {
        RasterizerStateDesc rasterizerStateDesc = {};
        rasterizerStateDesc.mCullMode = CULL_MODE_NONE;

        RasterizerStateDesc waterRasterizerStateDesc = {};
        waterRasterizerStateDesc.mCullMode = CULL_MODE_BACK;

        DepthStateDesc depthStateDesc = {};
        depthStateDesc.mDepthTest = true;
        depthStateDesc.mDepthWrite = true;
        depthStateDesc.mDepthFunc = CMP_GEQUAL;

        PipelineDesc desc = {};
        desc.mType = PIPELINE_TYPE_GRAPHICS;
        GraphicsPipelineDesc& pipelineSettings = desc.mGraphicsDesc;
        pipelineSettings.mPrimitiveTopo = PRIMITIVE_TOPO_TRI_LIST;
        pipelineSettings.mRenderTargetCount = 1;
        pipelineSettings.pDepthState = &depthStateDesc;
        pipelineSettings.pColorFormats = &pSwapChain->ppRenderTargets[0]->mFormat;
        pipelineSettings.mSampleCount = pSwapChain->ppRenderTargets[0]->mSampleCount;
        pipelineSettings.mSampleQuality = pSwapChain->ppRenderTargets[0]->mSampleQuality;
        pipelineSettings.mDepthStencilFormat = pDepthBuffer->mFormat;
        pipelineSettings.pRootSignature = pRootSignature;
        pipelineSettings.pShaderProgram = pWaterShader;
        pipelineSettings.pVertexLayout = &gWaterVertexLayout;
        pipelineSettings.pRasterizerState = &waterRasterizerStateDesc;
        pipelineSettings.mVRFoveatedRendering = true;
        addPipeline(pRenderer, &desc, &pWaterPipeline);

        // layout and pipeline for skybox draw
        VertexLayout vertexLayout = {};
        vertexLayout.mBindingCount = 1;
        vertexLayout.mBindings[0].mStride = sizeof(float4);
        vertexLayout.mAttribCount = 1;
        vertexLayout.mAttribs[0].mSemantic = SEMANTIC_POSITION;
        vertexLayout.mAttribs[0].mFormat = TinyImageFormat_R32G32B32A32_SFLOAT;
        vertexLayout.mAttribs[0].mBinding = 0;
        vertexLayout.mAttribs[0].mLocation = 0;
        vertexLayout.mAttribs[0].mOffset = 0;
        pipelineSettings.pVertexLayout = &vertexLayout;

        pipelineSettings.pDepthState = NULL;
        pipelineSettings.pRasterizerState = &rasterizerStateDesc;
        pipelineSettings.pShaderProgram = pSkyBoxDrawShader; //-V519
        addPipeline(pRenderer, &desc, &pSkyBoxDrawPipeline);

        PipelineDesc computeDesc = {};
        computeDesc.mType = PIPELINE_TYPE_COMPUTE;
        ComputePipelineDesc& computePipelineDesc = computeDesc.mComputeDesc;
        computePipelineDesc.pRootSignature = pRootSignatureWaterSimulation;
        computePipelineDesc.pShaderProgram = pWaterMoveShader;
        addPipeline(pRenderer, &computeDesc, &pWaterMoveCompPipeline);

        computePipelineDesc.pShaderProgram = pWaterResetCellsShader;
        addPipeline(pRenderer, &computeDesc, &pWaterResetCellsCompPipeline);

        computePipelineDesc.pShaderProgram = pWaterParticlesIntoCellsShader;
        addPipeline(pRenderer, &computeDesc, &pWaterParticlesIntoCellsCompPipeline);

        computePipelineDesc.pShaderProgram = pWaterCalculatePressureShader;
        addPipeline(pRenderer, &computeDesc, &pWaterCalculatePressureCompPipeline);

        computePipelineDesc.pShaderProgram = pWaterCountCellsShader;
        addPipeline(pRenderer, &computeDesc, &pWaterCountCellsCompPipeline);

        computePipelineDesc.pShaderProgram = pWaterFindCellDataPosShader;
        addPipeline(pRenderer, &computeDesc, &pWaterFindCellDataPosCompPipeline);

        computePipelineDesc.pShaderProgram = pWaterResetSumShader;
        addPipeline(pRenderer, &computeDesc, &pWaterResetSumCompPipeline);
    }

    void removePipelines()
    {
        removePipeline(pRenderer, pSkyBoxDrawPipeline);
        removePipeline(pRenderer, pWaterPipeline);
        removePipeline(pRenderer, pWaterMoveCompPipeline);
        removePipeline(pRenderer, pWaterResetCellsCompPipeline);
        removePipeline(pRenderer, pWaterParticlesIntoCellsCompPipeline);
        removePipeline(pRenderer, pWaterCalculatePressureCompPipeline);
        removePipeline(pRenderer, pWaterFindCellDataPosCompPipeline);
        removePipeline(pRenderer, pWaterCountCellsCompPipeline);
        removePipeline(pRenderer, pWaterResetSumCompPipeline);
    }

    void prepareDescriptorSets()
    {
        // Prepare descriptor sets
        DescriptorData params[6] = {};
        params[0].pName = "RightText";
        params[0].ppTextures = &pSkyBoxTextures[0];
        params[1].pName = "LeftText";
        params[1].ppTextures = &pSkyBoxTextures[1];
        params[2].pName = "TopText";
        params[2].ppTextures = &pSkyBoxTextures[2];
        params[3].pName = "BotText";
        params[3].ppTextures = &pSkyBoxTextures[3];
        params[4].pName = "FrontText";
        params[4].ppTextures = &pSkyBoxTextures[4];
        params[5].pName = "BackText";
        params[5].ppTextures = &pSkyBoxTextures[5];
        updateDescriptorSet(pRenderer, 0, pDescriptorSetTexture, 6, params);

        params[0].pName = "Particles";
        params[0].ppBuffers = &pWaterInstanceBuffer;
        updateDescriptorSet(pRenderer, 0, pDescriptorSetGraphicsParticles, 1, params);

        for (uint32_t i = 0; i < gDataBufferCount; ++i)
        {
            DescriptorData params[5] = {};
            params[0].pName = "uniformBlock";
            params[0].ppBuffers = &pSkyboxUniformBuffer[i];
            updateDescriptorSet(pRenderer, i * 2 + 0, pDescriptorSetUniforms, 1, params);

            params[0].pName = "uniformBlock";
            params[0].ppBuffers = &pProjViewUniformBuffer[i];
            updateDescriptorSet(pRenderer, i * 2 + 1, pDescriptorSetUniforms, 1, params);

            params[0].pName = "Particles";
            params[0].ppBuffers = &pWaterInstanceBuffer;
            params[1].pName = "Cells";
            params[1].ppBuffers = &pCellBuffer;
            params[2].pName = "ParticleIDs";
            params[2].ppBuffers = &pParticleIDBuffer;
            params[3].pName = "CellSum";
            params[3].ppBuffers = &pSumBuffer;
            params[4].pName = "uniformBlock";
            params[4].ppBuffers = &pWaterUniformBuffer[i];
            updateDescriptorSet(pRenderer, i, pDescriptorSetWater, 5, params);
        }

    }

};

DEFINE_APPLICATION_MAIN(Transformations)
