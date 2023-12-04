#include "Render.h"
#include <random>
#include <Walnut/Random.h>
#include <execution>

namespace Utils {
	static uint32_t ConvertToRGBA(const glm::vec4& color)
	{
		uint8_t r = (uint8_t)(color.r * 255.0f);
		uint8_t g = (uint8_t)(color.g * 250.0f);
		uint8_t b = (uint8_t)(color.b * 250.0f);
		uint8_t a = (uint8_t)(color.a * 250.0f);
		uint32_t result = (a << 24)| (b << 16) | (g << 8) | r;
		return result;
	}

	static uint32_t PCG_Hash(uint32_t input) {
		uint32_t state = input * 747796405u + 2891336453u;
		uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
		return (word >> 22u) ^ word;
	}

	static float RandomFloat(uint32_t& seed)
	{
		seed = PCG_Hash(seed);
		return (float)seed / (float)std::numeric_limits<uint32_t>::max();	
	}

	static glm::vec3 InUnitSphere(uint32_t& seed)
	{
		return glm::normalize(glm::vec3(
			RandomFloat(seed) * 2.0f -1.0f, 
			RandomFloat(seed) * 2.0f - 1.0f, 
			RandomFloat(seed) * 2.0f - 1.0f)
		);
	}

}

void Renderer::onResize(uint32_t width, uint32_t height)
{
	if (m_FinalImage)
	{
		// no resize necessary
		if (m_FinalImage->GetWidth() == width && m_FinalImage->GetHeight() == height)
			return;

		m_FinalImage->Resize(width, height);
	}
	else
	{
		m_FinalImage = std::make_shared<Walnut::Image>(width, height, Walnut::ImageFormat::RGBA);
	}


	delete[] m_ImageData;
	m_ImageData = new uint32_t[width * height];

	delete[] m_AccumilationData;
	m_AccumilationData = new glm::vec4[width * height];

	m_ImageHorizontalIter.resize(width);
	m_ImageVerticalIter.resize(height);

	for (uint32_t i = 0; i < width; i++)
		m_ImageHorizontalIter[i] = i;

	for (uint32_t i = 0; i < height; i++)
		m_ImageVerticalIter[i] = i;
}

void Renderer::Render(const Scene& scene, const Camera& camera)
{
	m_ActiveCam = &camera;
	m_ActiveScene = &scene;

	if (m_FrameIndex == 1)
		memset(m_AccumilationData, 0, m_FinalImage->GetWidth() * m_FinalImage->GetHeight() * sizeof(glm::vec4));

	Ray ray;
	ray.Origin = camera.GetPosition();
#define MT = 1
#ifdef MT
	std::for_each(std::execution::par, m_ImageVerticalIter.begin(), m_ImageVerticalIter.end(),
		[this](uint32_t y){
			std::for_each(std::execution::par, m_ImageHorizontalIter.begin(), m_ImageHorizontalIter.end(),
			[this, y](uint32_t x) {
					glm::vec4 color = PerPixel(x, y);
					m_AccumilationData[x + y * m_FinalImage->GetWidth()] += color;

					glm::vec4 accumulatedColor = m_AccumilationData[x + y * m_FinalImage->GetWidth()];
					accumulatedColor /= m_FrameIndex;

					accumulatedColor = glm::clamp(accumulatedColor, glm::vec4(0.0f), glm::vec4(1.0f));
					m_ImageData[x + y * m_FinalImage->GetWidth()] = Utils::ConvertToRGBA(accumulatedColor);
				});
		});
#else
	for (uint32_t y = 0; y < m_FinalImage->GetHeight(); y++)
	{
		for (uint32_t x = 0; x < m_FinalImage->GetWidth(); x++)
		{
			glm::vec4 color = PerPixel(x, y);
			m_AccumilationData[x + y * m_FinalImage->GetWidth()] += color;

			glm::vec4 accumulatedColor = m_AccumilationData[x + y * m_FinalImage->GetWidth()];
			accumulatedColor /= m_FrameIndex;
			
			accumulatedColor = glm::clamp(accumulatedColor, glm::vec4(0.0f), glm::vec4(1.0f));
			m_ImageData[x + y * m_FinalImage->GetWidth()] = Utils::ConvertToRGBA(accumulatedColor);
		}
	}
#endif // MT
	m_FinalImage->SetData(m_ImageData);

	if (m_Settings.Accumulate)
		m_FrameIndex++;
	else
		m_FrameIndex = 1;
}


glm::vec4 Renderer::PerPixel(uint32_t x, uint32_t y)
{
	Ray ray;
	ray.Origin = m_ActiveCam->GetPosition();
	ray.Direction = m_ActiveCam->GetRayDirections()[x + y * m_FinalImage->GetWidth()];
	/**/
	glm::vec3 light(0.0f);
	glm::vec3 contribution(1.0f);

	uint32_t seed = x + y * m_FinalImage->GetWidth();
	seed *= m_FrameIndex;

	int bounces = 5;
	for (int i = 0; i < bounces; i++) 
	{
		seed += i;
		Renderer::HitPayLoad payload = TraceRay(ray);

		if (payload.HitDistance < 0.0f) {
			glm::vec3 skyColor = glm::vec3(0.6f, 0.7f, 0.9f);
			//light += skyColor * contribution;
			break;
		}

		const Sphere& sphere = m_ActiveScene->Spheres[payload.ObjectIndex];
		const Material& material = m_ActiveScene->Materials[sphere.materialIndex];

		//light += material.Albedo * contribution;
		contribution *= material.Albedo;
		light += material.GetEmission();

		ray.Origin = payload.WorldPosition + (payload.WorldNormal * 0.0001f);
		
		if (m_Settings.SlowRandom)
			ray.Direction = glm::normalize(payload.WorldNormal + Walnut::Random::InUnitSphere());
		else
			ray.Direction = glm::normalize(payload.WorldNormal + Utils::InUnitSphere(seed));
	}

	
	return glm::vec4(light, 1.0f);
}

Renderer::HitPayLoad Renderer::TraceRay(const Ray& ray)
{

	// (bx^2 + by^2 + bz^2)t^2 + (2(axbx +ayby))t + (ax^2 + ay^2 -r^2) =0
	int closestSphere = -1;
	float hitDistance = FLT_MAX;

	for (size_t i = 0; i < m_ActiveScene->Spheres.size(); i++)
	{
		const Sphere& sphere = m_ActiveScene->Spheres[i];
		// a = ray origin
	// b = ray direction
	// r = radius
	// t = hit distance

		glm::vec3 origin = ray.Origin - sphere.Position;


		// (bx^2 + by^2 + bz^2)
		float a = glm::dot(ray.Direction, ray.Direction);

		//(2(axbx +ayby))
		float b = 2.0f * glm::dot(ray.Direction, origin);

		//(ax ^ 2 + ay ^ 2 - r ^ 2)
		float c = glm::dot(origin, origin) - sphere.Radius * sphere.Radius;


		// Quadratic formula discriminent
		// b^2 -4ac

		//      __  _____________
		// (-b +- \/ discriminent)  / 2a


		float discriminent = b * b - 4.0f * a * c;
		if (discriminent < 0.0f) {
			continue;
		}
		//float t0 = ( - b + glm::sqrt(discriminent)) / (2.0f * a); (currently unused)
		float closestT = (-b - glm::sqrt(discriminent)) / (2.0f * a);
		if (closestT > 0.0f && closestT < hitDistance)
		{
			hitDistance = closestT;
			closestSphere = (int)i;
		}
	}
	if (closestSphere < 0)
		return Miss(ray);
	
	return ClosestHit(ray, hitDistance, closestSphere);
}

Renderer::HitPayLoad Renderer::ClosestHit(const Ray& ray, float HitDistance, int ObjectIndex)
{
	Renderer::HitPayLoad payload;
	payload.HitDistance = HitDistance;
	payload.ObjectIndex = ObjectIndex;

	const Sphere& closestSphere = m_ActiveScene->Spheres[ObjectIndex];

	glm::vec3 origin = ray.Origin - closestSphere.Position;

	payload.WorldPosition = origin + ray.Direction * HitDistance;
	payload.WorldNormal = glm::normalize(payload.WorldPosition);

	payload.WorldPosition += closestSphere.Position;

	

	return payload;
}

Renderer::HitPayLoad Renderer::Miss(const Ray& ray)
{
	Renderer::HitPayLoad payload;
	payload.HitDistance = -1.0f;

	return payload;
}
