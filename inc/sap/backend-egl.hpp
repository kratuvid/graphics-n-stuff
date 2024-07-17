#pragma once

#include "pch.hpp"
#include "backend.hpp"
#include "utility.hpp"

class BackendEGL : public Backend
{
private:
	EGLDisplay display = EGL_NO_DISPLAY;
	wl_egl_window* surface_glue = nullptr;
	EGLSurface surface = EGL_NO_SURFACE;
	EGLContext context = EGL_NO_CONTEXT;

	EGLConfig config = nullptr;

public:
	BackendEGL(const Wayland* pwl, const int* pwidth, const int* pheight)
		:Backend(pwl, pwidth, pheight) {}

	~BackendEGL() override
	{
		eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
		if (context) eglDestroyContext(display, context);
		if (surface) eglDestroySurface(display, surface);
		safe_free(surface_glue, wl_egl_window_destroy);
		safe_free(display, eglTerminate);
	}

	void init() override
	{
		const char* query;
		iassert(query = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS));

		iassert(strstr(query, "EGL_KHR_platform_wayland"));
		if(strstr(query, "EGL_KHR_debug"))
		{
			PFNEGLDEBUGMESSAGECONTROLKHRPROC eglDebugMessageControlKHR;
			iassert(eglDebugMessageControlKHR = (decltype(eglDebugMessageControlKHR))eglGetProcAddress("eglDebugMessageControlKHR"));

			const EGLAttrib debug_attribs[] {
				EGL_DEBUG_MSG_CRITICAL_KHR, EGL_TRUE,
				EGL_DEBUG_MSG_ERROR_KHR, EGL_TRUE,
				EGL_DEBUG_MSG_WARN_KHR, EGL_TRUE,
				EGL_DEBUG_MSG_INFO_KHR, EGL_TRUE,
				EGL_NONE
			};
			iassert(eglDebugMessageControlKHR(egl_debug_callback, debug_attribs) == EGL_SUCCESS);
		}

		iassert(display = eglGetPlatformDisplay(EGL_PLATFORM_WAYLAND_KHR, pwl->display, nullptr));
		iassert(eglInitialize(display, nullptr, nullptr));

		const EGLint config_attribs[] {
			EGL_RED_SIZE, 8,
			EGL_GREEN_SIZE, 8,
			EGL_BLUE_SIZE, 8,
			EGL_ALPHA_SIZE, 8,
			EGL_BUFFER_SIZE, 32,
			EGL_DEPTH_SIZE, 24,
			EGL_STENCIL_SIZE, 0,
			EGL_SAMPLES, 0,
			EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
			EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
			EGL_CONFIG_CAVEAT, EGL_NONE,
			// EGL_MAX_SWAP_INTERVAL, 1,
			EGL_NONE
		};
		EGLint _dummy;
		iassert(eglChooseConfig(display, config_attribs, &config, 1, &_dummy));

		iassert(surface_glue = wl_egl_window_create(pwl->window.surface, *pwidth, *pheight));

		const EGLAttrib surface_attribs[] {
			EGL_GL_COLORSPACE, EGL_GL_COLORSPACE_LINEAR,
			EGL_RENDER_BUFFER, EGL_BACK_BUFFER,
			EGL_NONE
		};
		iassert(surface = eglCreatePlatformWindowSurface(display, config, surface_glue, surface_attribs));
		iassert(eglSurfaceAttrib(display, surface, EGL_SWAP_BEHAVIOR, EGL_BUFFER_DESTROYED));

		iassert(eglBindAPI(EGL_OPENGL_API));

		const EGLint context_attribs[] {
			EGL_CONTEXT_MAJOR_VERSION, 4,
			EGL_CONTEXT_MINOR_VERSION, 6,
			EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
			EGL_CONTEXT_OPENGL_DEBUG, EGL_TRUE,
			EGL_CONTEXT_OPENGL_FORWARD_COMPATIBLE, EGL_TRUE,
			EGL_NONE
		};
		iassert(context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attribs));

		iassert(eglMakeCurrent(display, surface, surface, context));

		iassert(gladLoadGLLoader((GLADloadproc)eglGetProcAddress) != 0);

		glEnable(GL_DEPTH_TEST);

		glDebugMessageCallback(debug_callback, nullptr);
	}

	void on_configure(bool new_dimensions, wl_array* states) override
	{
		// TODO: Don't resize when states == resizing
		if (new_dimensions) {
			wl_egl_window_resize(surface_glue, *pwidth, *pheight, 0, 0);
		}
	}

	void present()
	{
		// TODO: Rebuild context with lost
		eglSwapBuffers(display, surface);
	}

private:
	static void egl_debug_callback(EGLenum error, const char* command, EGLint type, EGLLabelKHR, EGLLabelKHR, const char* message)
	{
		std::println(stderr, "EGL Debug: command: {}, error: {:#x}, type: {:#x}: {}", command, error, type, message);
	}

	static void debug_callback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* data)
	{
		std::println(stderr, "GL Debug: source: {:#x}, type: {:#x}, id: {:#x}, severity: {:#x}: {}", source, type, id, severity, message);
	}
};
