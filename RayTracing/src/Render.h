#pragma once

#include "Walnut/Image.h"
#include <memory>
#include <glm/glm.hpp>
#include "Ray.h"
#include "Camera.h"
#include "Scene.h"

class Renderer
{
public:
	struct Settings
	{
		bool Accumulate = true;
		bool SlowRandom = true;
	};
public:
	Renderer() = default;

	void onResize(uint32_t width, uint32_t height);

	void Render(const Scene& scene, const Camera& camera);

	std::shared_ptr<Walnut::Image> getFinalImage() const { return m_FinalImage; }

	void resetFrameIndex() { m_FrameIndex = 1; }

	Settings& getSettings() { return m_Settings; }

private:
	struct HitPayLoad
	{
		float HitDistance;
		glm::vec3 WorldPosition;
		glm::vec3 WorldNormal;

		int ObjectIndex;
	};

	glm::vec4 PerPixel(uint32_t x, uint32_t y);

	HitPayLoad TraceRay(const Ray& ray);
	HitPayLoad ClosestHit(const Ray& ray, float HitDistance, int ObjectIndex);
	HitPayLoad Miss(const Ray& ray);
private:
	std::shared_ptr<Walnut::Image> m_FinalImage;
	
	Settings m_Settings; 

	std::vector<uint32_t> m_ImageHorizontalIter, m_ImageVerticalIter;



	const Scene* m_ActiveScene = nullptr;

	const Camera* m_ActiveCam = nullptr;

	uint32_t* m_ImageData = nullptr;

	glm::vec4* m_AccumilationData = nullptr;

	uint32_t m_FrameIndex = 1;
};