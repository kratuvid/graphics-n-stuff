#pragma once

#include "pch.hpp"
#include "utility.hpp"

class Backend
{
protected:
	const Wayland* pwayland = nullptr;
	const int *pwidth = nullptr, *pheight = nullptr;

public:
	Backend(const Wayland* pwayland, const int* pwidth, const int* pheight)
		:pwayland(pwayland), pwidth(pwidth), pheight(pheight) {}
	virtual ~Backend() {};

	virtual void init() = 0;
	virtual void on_configure(bool new_dimensions, wl_array* states) = 0;
	virtual void present() = 0;
};
