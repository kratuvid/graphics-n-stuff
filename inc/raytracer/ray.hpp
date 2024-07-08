#pragma once

#include "raytracer/pch.hpp"
#include "raytracer/types.hpp"

class Ray
{
public:
	Ray() {}

	Ray(ovec3 const& origin, ovec3 const& direction)
		:m_origin(origin), m_direction(direction)
	{}

	auto origin() const { return m_origin; }
	auto direction() const { return m_direction; }

	ovec3 at(oreal t) const
	{
		return m_origin + m_direction * t;
	}

private:
	ovec3 m_origin {}, m_direction {};
};
