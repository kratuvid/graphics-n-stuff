#pragma once

#include "raytracer/pch.hpp"
#include "raytracer/hittable.hpp"

class HittableList : public Hittable
{
public:
	std::vector<std::shared_ptr<Hittable>> objects;
};
