#pragma once

#include "pch.hpp"
#include "backend.hpp"
#include "utility.hpp"

class BackendSHM : public Backend
{
private:
    struct Buffer {
        wl_buffer* object;
        bool busy;

        union {
            void* data;
            uint8_t* data_u8;
            uint32_t* data_u32;
        };
        size_t size;
    } buffers[2] {};

public:
	BackendSHM(const Wayland* pwl, const int* pwidth, const int* pheight)
		:Backend(pwl, pwidth, pheight) {}

	~BackendSHM() override
	{
		destroy_buffers();
	}

	void init() override
	{
	}

	void on_configure(bool new_dimensions, wl_array* states) override
	{
		if (new_dimensions) {
			destroy_buffers();
		}
	}

	void present(Buffer* buffer)
	{
        wl_surface_attach(pwl->window.surface, buffer->object, 0, 0);
        wl_surface_damage_buffer(pwl->window.surface, 0, 0, *pwidth, *pheight);
		wl_surface_commit(pwl->window.surface);
	}

    Buffer* next_buffer()
    {
        Buffer* buffer = nullptr;

        for (auto& one : buffers) {
            if (!one.busy) {
                buffer = &one;
                break;
            }
        }
        iassert(buffer);

        if (!buffer->object) {
            create_buffer(buffer, *pwidth, *pheight, WL_SHM_FORMAT_XRGB8888);
        }

		buffer->busy = true;

        return buffer;
    }

private:
    static int create_anonymous_file(size_t size)
    {
        int fd, ret;

        fd = memfd_create("opengl_studies_backend_shm", MFD_CLOEXEC | MFD_ALLOW_SEALING);
        if (fd > 0) {
            fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK);
            do {
                ret = ftruncate(fd, size);
            } while (ret < 0 && errno == EINTR);
            if (ret < 0) {
                close(fd);
                return -1;
            }
        }

        return fd;
    }

    void create_buffer(Buffer* buffer, int width, int height, uint32_t format)
    {
        iassert(width > 0);
        iassert(height > 0);
        const size_t stride = 4 * width;
        const size_t size = stride * height;
        buffer->size = size;

        int fd;
        iassert((fd = create_anonymous_file(size)) > 0);

        void* data;
        iassert(data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
        buffer->data = data;

        wl_shm_pool* pool;
        iassert(pool = wl_shm_create_pool(pwl->global.shm, fd, size));
        iassert(buffer->object = wl_shm_pool_create_buffer(pool, 0, width, height, stride, format));
        wl_buffer_add_listener(buffer->object, &buffer_listener, buffer);
        wl_shm_pool_destroy(pool);
        close(fd);
    }

	void destroy_buffers()
	{
        for (auto& buffer : buffers) {
            safe_free(buffer.object, wl_buffer_destroy);
            buffer.busy = false;
            if (buffer.data) {
                munmap(buffer.data, buffer.size);
                buffer.data = nullptr;
            }
            buffer.size = 0;
        }
	}

private:
	static void on_buffer_release(void* data, wl_buffer* buffer_)
	{
		auto buffer = static_cast<Buffer*>(data);
		buffer->busy = false;
	}

	static constexpr wl_buffer_listener buffer_listener {
		.release = on_buffer_release
	};

public: // low-level helpers
    void pixel_range(Buffer* buffer, int x, int y, int ex, int ey, uint32_t color)
    {
        x = std::clamp(x, 0, *pwidth - 1);
        y = std::clamp(y, 0, *pheight - 1);
        ex = std::clamp(ex + 1, 0, *pwidth - 1);
        ey = std::clamp(ey, 0, *pheight - 1);
        const auto location = at(x, y), location_end = at(ex, ey);
        if (location <= location_end)
            std::fill(&buffer->data_u32[location], &buffer->data_u32[location_end], color);
    }

    uint32_t& pixel_at(Buffer* buffer, int x, int y)
    {
        static uint32_t facade;
        if ((x < 0 or x >= *pwidth) or (y < 0 or y >= *pheight)) {
            facade = 0;
            return facade;
        }
        const ssize_t location = at(x, y);
        return buffer->data_u32[location];
    }

    ssize_t at(int x, int y)
    {
        return (y * (*pwidth)) + x;
    }
};
