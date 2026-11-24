#pragma once

namespace PoolCuePower
{
inline void PullBack(float& power, float& pull, float& speed, float maximum, float length, float delta, float dt)
{
	const float next = power - delta;
	const float inverse = 1.0f / maximum;
	power = next - inverse * next * delta;
	const float nextPull = power * inverse * (length * 0.25f);
	speed = (nextPull - pull) / dt;
	pull = nextPull;
}

// Retail AUnitPoolCue::UpdatePowerOscillation (004d7f10). The caller
// supplies game time multiplied by ten. Keep the original operation order.
inline void Step(float& power, bool& decreasing, float maximum, float delta)
{
	if (decreasing)
	{
		const float next = power - delta;
		power = next - next / maximum * delta;
	}
	else
	{
		const float next = delta + power;
		power = next / maximum * delta + next;
	}
	if (power > maximum)
	{
		power = maximum;
		decreasing = !decreasing;
	}
	if (power < 0.01f)
	{
		power = 0.01f;
		decreasing = !decreasing;
	}
}
}
