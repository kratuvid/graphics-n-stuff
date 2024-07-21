#pragma once

#include "pch.hpp"
#include "app.hpp"
#include "backend-shm.hpp"

class PS11 : public App<BackendSHM>
{
private:
	void setup() override
	{
		title = "PS11";
	}

	void update(float delta_time) override
	{
	}

	void draw(float delta_time) override
	{
		auto buf = backend->next_buffer();
		backend->present(buf);
	}

	void on_click(uint32_t button, wl_pointer_button_state state) override
	{
	}

	void on_key(xkb_keysym_t key, wl_keyboard_key_state state) override
	{
	}

	void on_configure(bool new_dimensions, wl_array* array) override
	{
	}
};
