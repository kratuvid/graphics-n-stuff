#pragma once

#include "raytracer/types.hpp"
#include "raytracer/ray.hpp"

struct HitRecord
{
	ovec3 p;
	ovec3 normal;
	oreal t;
	bool front_face;

	void set_face_normal(Ray const& r, ovec3 const& outward_normal)
	{
		front_face = glm::dot(r.direction(), outward_normal) < 0;
		normal = front_face ? outward_normal : -outward_normal;
	}
};

class Hittable
{
public:
	virtual ~Hittable() = default;
	virtual bool hit(Ray const& r, oreal ray_tmin, oreal ray_tmax, HitRecord& record) const = 0;
};
