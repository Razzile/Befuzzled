#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

#include <skia/core/SkAlphaType.h>
#include <skia/core/SkCanvas.h>
#include <skia/core/SkSurface.h>
#include <skia/core/SkStream.h>
#include <skia/core/SkRefCnt.h>
#include <skia/core/SkFont.h>
#include <skia/core/SkColorType.h>
#include <skia/core/SkColorSpace.h>
#include <skia/core/SkPath.h>
#include <skia/core/SkGraphics.h>
#include <skia/gpu/GrDirectContext.h>
#include <skia/gpu/gl/GrGLInterface.h>
#include <skia/utils/SkRandom.h>
#include <SDL2/SDL.h>

#include "GL/gl.h"
#include "GL/glext.h"

struct ApplicationState {
    ApplicationState() : fQuit(false) {}
    // Storage for the user created rectangles. The last one may still be being
    // edited.
    SkTArray<SkRect> fRects;
    bool fQuit;
};

static void handle_error()
{
    const char* error = SDL_GetError();
    SkDebugf("SDL Error: %s\n", error);
    SDL_ClearError();
}

static void handle_events(ApplicationState* state, SkCanvas* canvas) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      switch (event.type) {
        case SDL_MOUSEMOTION:
          if (event.motion.state == SDL_PRESSED) {
            SkRect& rect = state->fRects.back();
            rect.fRight = event.motion.x;
            rect.fBottom = event.motion.y;
          }
          break;
        case SDL_MOUSEBUTTONDOWN:
          if (event.button.state == SDL_PRESSED) {
            state->fRects.push_back() = SkRect::MakeLTRB(
                SkIntToScalar(event.button.x), SkIntToScalar(event.button.y),
                SkIntToScalar(event.button.x), SkIntToScalar(event.button.y));
          }
          break;
        case SDL_KEYDOWN: {
          SDL_Keycode key = event.key.keysym.sym;
          if (key == SDLK_ESCAPE) {
            state->fQuit = true;
          }
          break;
        }
        case SDL_QUIT:
          state->fQuit = true;
          break;
        default:
          break;
      }
    }
}

// Creates a star type shape using a SkPath
static SkPath create_star() {
    static const int kNumPoints = 5;
    SkPath concavePath;
    SkPoint points[kNumPoints] = {{0, SkIntToScalar(-50)}};
    SkMatrix rot;
    rot.setRotate(SkIntToScalar(360) / kNumPoints);
    for (int i = 1; i < kNumPoints; ++i) {
      rot.mapPoints(points + i, points + i - 1, 1);
    }
    concavePath.moveTo(points[0]);
    for (int i = 0; i < kNumPoints; ++i) {
      concavePath.lineTo(points[(2 * i) % kNumPoints]);
    }
    concavePath.setFillType(SkPathFillType::kEvenOdd);
    SkASSERT(!concavePath.isConvex());
    concavePath.close();
    return concavePath;
}


int SDL_main(int argc, char** argv)
{
    SkGraphics::Init();

    // setup OpenGL params
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        SDL_GL_CONTEXT_PROFILE_CORE);

    uint32_t window_flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;

    static constexpr int kStencilBits = 8; // Skia needs 8 stencil bits
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, kStencilBits);
    SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, 1);

    static const int kMsaaSampleCount = 4;
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, kMsaaSampleCount);

    int res = SDL_Init(SDL_INIT_EVERYTHING);
    SkASSERT(res == 0);

    SDL_DisplayMode dm;
    if (SDL_GetDesktopDisplayMode(0, &dm) != 0)
    {
        handle_error();
        return 1;
    }
    SDL_Window* window =
        SDL_CreateWindow("SDL Window", SDL_WINDOWPOS_CENTERED,
                         SDL_WINDOWPOS_CENTERED, 1366 , 768, window_flags);
    if (!window)
    {
        handle_error();
        return 1;
    }

    auto gl_context = SDL_GL_CreateContext(window);
    if (!gl_context)
    {
        handle_error();
        return 1;
    }

    // attempt to enable vsync
    if (SDL_GL_SetSwapInterval(-1) != 0)
    {
        if (SDL_GL_SetSwapInterval(1) != 0)
        {
            // WGL_EXT_swap_control not available. TODO: resort to manual vsync
        }
    }

    int success = SDL_GL_MakeCurrent(window, gl_context);
    if (success != 0)
    {
        handle_error();
        return success;
    }

    int dw, dh;
    SDL_GL_GetDrawableSize(window, &dw, &dh);

    glViewport(0, 0, dm.w, dm.h);
    glClearColor(1, 1, 1, 1);
    glClearStencil(0);
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    // setup GrContext
    sk_sp<const GrGLInterface> gl_interface = GrGLMakeNativeInterface();
    SkASSERT(gl_interface);
    // setup contexts
    sk_sp<GrDirectContext> grContext = GrDirectContext::MakeGL(gl_interface);
    SkASSERT(grContext);

    GrGLint buffer;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &buffer);
    GrGLFramebufferInfo info;
    info.fFBOID = (GrGLuint)buffer;

#if defined(SK_BUILD_FOR_ANDROID)
    info.fFormat = GL_RGB8_OES;
#else
    info.fFormat = GL_RGBA8;
#endif

    GrBackendRenderTarget target(1366, 768, kMsaaSampleCount, kStencilBits, info);

    // setup SkSurface
    // To use distance field text, use commented out SkSurfaceProps instead
    SkSurfaceProps props(SkSurfaceProps::kUseDeviceIndependentFonts_Flag,
                         SkPixelGeometry::kUnknown_SkPixelGeometry);


    sk_sp<SkSurface> surface(SkSurface::MakeFromBackendRenderTarget(
        grContext.get(), target, kBottomLeft_GrSurfaceOrigin,
        kBGRA_8888_SkColorType,
        nullptr, &props));

    SkASSERT(surface);

    SkCanvas* canvas = surface->getCanvas();

    ApplicationState state;

    const char* helpMessage =
        "Click and drag to create rects.  Press esc to quit.";

    SkPaint paint;

    // create a surface for CPU rasterization
    sk_sp<SkSurface> cpuSurface(SkSurface::MakeRaster(canvas->imageInfo()));

    SkCanvas* offscreen = cpuSurface->getCanvas();
    offscreen->save();
    offscreen->translate(50.0f, 50.0f);
    offscreen->drawPath(create_star(), paint);
    offscreen->restore();

    sk_sp<SkImage> image = cpuSurface->makeImageSnapshot();

    int rotation = 0;
    SkFont font;
    while (!state.fQuit)
    {
        // Our application loop
        SkRandom rand;
        canvas->clear(SK_ColorWHITE);
        handle_events(&state, canvas);

        paint.setColor(SK_ColorBLACK);
        canvas->drawString(helpMessage, 100.0f, 100.0f, font, paint);
        for (int i = 0; i < state.fRects.count(); i++)
        {
            paint.setColor(rand.nextU() | 0x44808080);
            canvas->drawRect(state.fRects[i], paint);
        }

        // draw offscreen canvas
        canvas->save();
        canvas->translate(dm.w / 2.0, dm.h / 2.0);
        canvas->rotate(rotation++);
        canvas->drawImage(image, -50.0f, -50.0f);
        canvas->restore();

        canvas->flush();
        SDL_GL_SwapWindow(window);
    }

    if (gl_context)
    {
        SDL_GL_DeleteContext(gl_context);
    }

    // Destroy window
    SDL_DestroyWindow(window);

    // Quit SDL subsystems
    SDL_Quit();

    grContext->abandonContext();

    return 0;
}
