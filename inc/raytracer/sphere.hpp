#pragma once

#include "raytracer/pch.hpp"
#include "raytracer/hittable.hpp"

class Sphere : public Hittable
{
public:
	Sphere(ovec3 const& center, oreal radius)
		:center(center), radius(std::max(oreal(0), radius))
	{}

	bool hit(Ray const& r, oreal ray_tmin, oreal ray_tmax, HitRecord& rec) const override
	{
		ovec3 oc = center - r.origin();
		auto a = glm::length2(r.direction());
		auto h = glm::dot(r.direction(), oc);
		auto c = glm::length2(oc) - radius * radius;

		auto discriminant = h * h - a * c;
		if (discriminant < 0)
			return false;

		auto sqrtd = glm::sqrt(discriminant);

		auto root = (h - sqrtd) / a;
		if (root <= ray_tmin or ray_tmax <= root)
		{
			root = (h + sqrtd) / a;
			if (root <= ray_tmin or ray_tmax <= root)
				return false;
		}

		rec.t = root;
		rec.p = r.at(rec.t);
		ovec3 outward_normal = (rec.p - center) / radius;
		rec.set_face_normal(r, outward_normal);

		return true;
	}

private:
	ovec3 center;
	oreal radius;
};
