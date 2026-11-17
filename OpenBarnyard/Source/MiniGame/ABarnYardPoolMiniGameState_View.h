#pragma once

#include <Math/TMatrix44.h>
#include "../Units/AUnitPoolCuePower.h"
#include <cmath>
#include <cstring>

namespace PoolSpectator
{
// The wire layout also serves older PoolPvp peers. Playback state stays local.
struct PoolViewFrame
{
	TUINT32 magic, serial, owner, state, stage, winner, visibleBalls, pottedBalls;
	TFLOAT balls[16][3], cueMatrix[16], aim[3], cameraPosition[3], cameraDirection[3];
	TFLOAT fov, power, cueOpacity;
	TUINT32 cueVisible, aimVisible, potted[2], groups[2], shots[2];
	TWCHAR message[64], title[64], info[64];
};
static_assert(sizeof(PoolViewFrame) == 752);

struct ChargePreview
{
	enum State { Idle, Held, Released } state = Idle;
	TUINT serial = 0, pressSequence = 0, releaseSequence = 0;
	TFLOAT power = 0, age = 0, releaseAge = 0, lead = 0, lastHostPower = 0, correction = 0;
	bool decreasing = false, hostDecreasing = false, confirmed = false;

	void Edge(TUINT shot, TUINT stage, TUINT sequence, bool down, bool up)
	{
		// Stage 1 Shoot confirms ball placement and has no power meter.
		if (down && stage == 2 && state == Idle)
		{
			*this = {};
			state = Held;
			serial = shot;
			pressSequence = sequence;
		}
		if (up && state == Held && serial == shot)
		{
			state = Released;
			releaseAge = age;
			releaseSequence = sequence;
		}
	}

	TFLOAT Update(const PoolViewFrame& frame, bool local, bool fresh, TFLOAT dt)
	{
		if (state == Idle) return frame.power;
		age += dt;
		if (!local || frame.serial != serial || (frame.stage != 2 && frame.stage != 3) ||
		    (frame.stage == 2 && (confirmed || age > 0.65f)) ||
		    (state == Released && age - releaseAge > 0.65f))
		{
			*this = {};
			return frame.power;
		}
		constexpr TFLOAT maximum = 15.0f; // native StartGame configures both pool variants to 15
		if (state == Held && dt > 0) PoolCuePower::Step(power, decreasing, maximum, dt * 10.0f);
		if (fresh && frame.stage == 3)
		{
			if (!confirmed)
			{
				// The first host charge sample approximates the time it was
				// already held there. The rest is the measured input + view delay.
				const TFLOAT sampledAge = (maximum / 10.0f) * std::log1p(frame.power);
				lead = Toshi::TMath::Min(0.22f, Toshi::TMath::Max(0.0f, age - sampledAge));
				confirmed = true;
			}
			else if (frame.power != lastHostPower) hostDecreasing = frame.power < lastHostPower;
			lastHostPower = frame.power;
			if (state == Held)
			{
				TFLOAT target = frame.power * maximum;
				bool down = frame.power >= 1.0f || (hostDecreasing && target > 0.01f);
				// Project the newest authority sample to the local edge's timeline.
				// Small native steps handle a turn at either limit during the delay.
				for (TFLOAT remaining = lead; remaining > 0.00001f;)
				{
					const TFLOAT step = Toshi::TMath::Min(remaining, 1.0f / 120.0f);
					PoolCuePower::Step(target, down, maximum, step * 10.0f);
					remaining -= step;
				}
				correction = target - power;
				decreasing = down;
			}
		}
		if (state == Held)
		{
			const TFLOAT applied = correction * Toshi::TMath::Min(1.0f, dt * 14.0f);
			power = Toshi::TMath::Min(maximum, Toshi::TMath::Max(0.0f, power + applied));
			correction -= applied;
		}
		return power / maximum;
	}
};

inline TFLOAT DistanceSq(const TFLOAT* a, const TFLOAT* b)
{
	const TFLOAT x = b[0] - a[0], y = b[1] - a[1], z = b[2] - a[2];
	return x*x + y*y + z*z;
}

inline void Blend3(TFLOAT* out, const TFLOAT* a, const TFLOAT* b, TFLOAT alpha)
{
	for (TINT i = 0; i < 3; ++i) out[i] = a[i] + (b[i] - a[i]) * alpha;
}

inline void BlendCue(TFLOAT* out, const TFLOAT* from, const TFLOAT* to, TFLOAT alpha)
{
	using namespace Toshi;
	if (DistanceSq(from + 12, to + 12) > 4.0f) return;
	TMatrix44 a, b;
	std::memcpy(&a, from, sizeof(a));
	std::memcpy(&b, to, sizeof(b));
	TQuaternion qa, qb;
	TMatrix44::MatToQuat(qa, a);
	TMatrix44::MatToQuat(qb, b);
	// Only the native rigid cue basis is rotated. Preserve unusual wire bases
	// and instant camera-side changes rather than producing a collapsed cue.
	const TFLOAT dot = std::fabs(TVector4::DotProduct4(qa.AsVector4(), qb.AsVector4()));
	if (dot > 0.7f && std::fabs(qa.MagnitudeSq() - 1.0f) < 0.002f &&
	    std::fabs(qb.MagnitudeSq() - 1.0f) < 0.002f)
	{
		TQuaternion rotation;
		TQuaternion::Nlerp(rotation, qa, qb, alpha);
		TMatrix44 result;
		result.SetFromQuaternion(rotation);
		std::memcpy(out, &result, 12 * sizeof(TFLOAT));
	}
	Blend3(out + 12, from + 12, to + 12, alpha);
}

struct Motion
{
	PoolViewFrame start{}, shown{};
	TUINT sequence = 0, match = 0;
	TFLOAT gap = 0.0f, elapsed = 0.0f, duration = 0.0f;

	bool Fresh(TUINT id, TUINT session) const { return id != sequence || session != match; }

	void Update(const PoolViewFrame& target, TUINT id, TUINT session, TFLOAT dt, bool localInput)
	{
		dt = Toshi::TMath::Max(dt, 0.0f);
		gap += dt;
		if (Fresh(id, session))
		{
			const bool cut = !sequence || session != match || gap > 0.2f ||
			    shown.serial != target.serial || shown.owner != target.owner || shown.state != target.state;
			start = shown;
			// Cover a whole 30 Hz sample interval, including render quantisation.
			duration = cut ? 0.0f : Toshi::TMath::Min(Toshi::TMath::Max(gap, 1.0f / 30.0f),
			    localInput ? 0.06f : 0.08f) + dt;
			elapsed = gap = 0.0f;
			sequence = id;
			match = session;
		}
		elapsed += dt;
		shown = target;
		if (duration == 0.0f || elapsed >= duration + 0.075f) return;
		const TFLOAT time = elapsed / duration;
		// Constant speed through most of the interval. A short smooth tail
		// continues if the next packet is late, without moving past the host.
		const TFLOAT alpha = time <= 0.8f ? time : 1.0f - 0.2f * std::exp(-5.0f * (time - 0.8f));
		for (TUINT i = 0; i < 16; ++i)
		{
			const TUINT bit = 1u << i;
			if ((start.visibleBalls & target.visibleBalls & bit) &&
			    !((start.pottedBalls ^ target.pottedBalls) & bit) &&
			    DistanceSq(start.balls[i], target.balls[i]) <= 4.0f)
				Blend3(shown.balls[i], start.balls[i], target.balls[i], alpha);
		}
		if (start.cueVisible && target.cueVisible)
		{
			BlendCue(shown.cueMatrix, start.cueMatrix, target.cueMatrix, alpha);
			shown.cueOpacity = start.cueOpacity + (target.cueOpacity - start.cueOpacity) * alpha;
		}
		if (start.aimVisible && target.aimVisible && DistanceSq(start.aim, target.aim) < 1.0f)
			Blend3(shown.aim, start.aim, target.aim, alpha);
		if (DistanceSq(start.cameraPosition, target.cameraPosition) < 9.0f &&
		    DistanceSq(start.cameraDirection, target.cameraDirection) < 1.0f)
		{
			Blend3(shown.cameraPosition, start.cameraPosition, target.cameraPosition, alpha);
			Blend3(shown.cameraDirection, start.cameraDirection, target.cameraDirection, alpha);
			// LookAt normalises this vector. The angle guard excludes an opposite pair.
			shown.fov = start.fov + (target.fov - start.fov) * alpha;
		}
	}
};
} // namespace PoolSpectator
