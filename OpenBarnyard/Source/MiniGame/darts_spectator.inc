// Pointer-free wire image. Keep the layout compatible with the original DartsPvP feature.
struct DartsViewPose
{
	TFLOAT x, y, z, scale;
	TINT16 basis[9];
};
struct DartsViewFrame
{
	TUINT32 magic, visibleDarts, presentTargets;
	TFLOAT ringAngles[3];
	DartsViewPose darts[20];
	struct Player { TFLOAT direction[3]; TINT32 score; TUINT32 dart, animation; TFLOAT animationTime; } players[2];
	TFLOAT aimX, aimY;
	TUINT32 currentPlayer, round, state;
	TUINT32 throwSerial, aimStage, lastHitSerial, lastHitOwner;
	TINT32 hitScore, hitTarget, hitTeam, totalScore[2];
	TFLOAT timeLeft, timeLimit;
	TUINT32 finished;
};
static_assert(sizeof(DartsViewFrame) == 868);

enum class DartsPressMode { Wait, View, HostTime };
// A referenced sample travels to the guest, through display and send scheduling,
// then back to the host. Two 200 ms legs alone exceed the old 350 ms limit.
static constexpr TUINT32 DartsAimWindowMs = 800;
static DartsPressMode DartsPhysicalPress(bool sameStage, bool usedFallback, TUINT32 elapsed)
{
	if (sameStage && elapsed <= DartsAimWindowMs) return DartsPressMode::Wait;
	if (sameStage && !usedFallback) return DartsPressMode::HostTime;
	return DartsPressMode::View;
}

// The second native sweep starts at +1. Both peers evaluate the displayed tick
// of that sweep, even if the guest's second press precedes the first reply.
static float DartsSecondAxis(float frequency, TUINT32 phaseMs)
{
	return cosf(frequency * (float(phaseMs) * 0.001f) * 3.14159265358979323846f);
}

struct DartsFirstLock
{
	TUINT32 match = 0, dart = 0, reference = 0;
	bool ResolveSecond(TUINT32 matchId, TUINT32 serial, TUINT32 ref, TUINT32 phaseMs,
	                   float hostPhaseSeconds, float frequency, float& axis) const
	{
		if (!ref || ref != reference || matchId != match || serial != dart ||
		    phaseMs < 100 || phaseMs > 60000 ||
		    float(phaseMs) > hostPhaseSeconds * 1000.0f + DartsAimWindowMs) return false;
		axis = DartsSecondAxis(frequency,phaseMs);
		return true;
	}
};

struct DartsLocalAim
{
	DartsFirstLock first;
	TUINT32 firstTick = 0, sentTick = 0, phaseMs = 0;
	float seconds = 0, x = 1, y = 1, flight = 0;
	bool fired = false, sent = false, released = false;

	void Begin(TUINT32 match, TUINT32 dart, TUINT32 reference, float lockedY, TUINT32 now)
	{
		*this = {};
		first = {match,dart,reference};
		y = lockedY;
		firstTick = now;
	}
	bool Fire()
	{
		if (!first.reference || fired || phaseMs <= 100) return false;
		fired = true;
		return true;
	}
	void FireSnapshot(TUINT32 match, TUINT32 dart, float lockedX, float lockedY, TUINT32 now)
	{
		Begin(match,dart,0,lockedY,now);
		x = lockedX;
		fired = sent = true;
		sentTick = now;
	}
	void Update(const DartsViewFrame& host, TUINT32 match, bool ownTurn, TUINT32 now,
	            float dt, float frequency)
	{
		if (!first.dart) return;
		if (!ownTurn || host.finished || first.match != match || first.dart != host.throwSerial ||
		    (host.state == 1 && now-firstTick > DartsAimWindowMs) ||
		    (host.state == 2 && sent && now-sentTick > DartsAimWindowMs) ||
		    (host.state == 2 && first.reference && host.aimY != y) ||
		    (host.state != 1 && host.state != 2 && !(fired && (host.state == 3 || host.state == 9 || (host.state == 11 && released)))))
		{
			*this = {};
			return;
		}
		if (!fired)
		{
			seconds += dt;
			phaseMs = TUINT32((seconds < 60.0f ? seconds : 60.0f) * 1000.0f + 0.5f);
			x = DartsSecondAxis(frequency,phaseMs);
		}
		else if (released && flight < 1)
		{
			flight += dt * 10.0f; // Native flight takes 100 ms.
			if (flight > 1) flight = 1;
		}
	}
	bool TakeSecond(const DartsViewFrame& host, TUINT32 now)
	{
		// Keep retransmitting the first edge until the host opens the second gate.
		if (!first.reference || !fired || sent || host.aimStage != 2) return false;
		sent = true;
		sentTick = now;
		return true;
	}
	void Present(DartsViewFrame& visual) const
	{
		if (!first.dart) return;
		visual.aimX = x;
		visual.aimY = y;
		visual.state = released ? 11u : (fired ? 3u : 2u);
		visual.aimStage = !fired && phaseMs > 100 ? 2u : 0u;
		if (fired)
		{
			auto& arm = visual.players[visual.currentPlayer];
			arm.animation = 2; // darts_throw, with the held dart attached to dart_loc
			arm.animationTime = 0;
		}
	}
};

// The transmitted fraction is also used to draw the bar. It therefore names
// the exact sub-frame the player saw, including renders between two packets.
static float DartsBlendAxis(float from, float to, TUINT32 fraction)
{
	if (!fraction) return from;
	if (fraction == 65535) return to;
	return from + (to-from) * (float(fraction) / 65535.0f);
}

struct DartsViewMotion
{
	DartsViewFrame from{}, shown{};
	TUINT32 sequence = 0, match = 0;
	float gap = 0, elapsed = 0, duration = 0.05f;
	TUINT32 previousFirstView = 0, firstBlend = 0;
	float previousFirstAxis = 0, latestFirstAxis = 0, firstDuration = 0;

	static float Blend(float a, float b, float alpha) { return a + (b-a)*alpha; }
	void Update(const DartsViewFrame& next, TUINT32 serial, TUINT32 matchId, float dt, bool exactFirst = false)
	{
		gap += dt;
		if (serial != sequence || matchId != match)
		{
			const bool cut = !sequence || match != matchId || shown.round != next.round ||
			                 shown.throwSerial != next.throwSerial || gap > 0.2f;
			previousFirstView = exactFirst && !cut && next.aimStage == 1 && shown.aimStage == 1 ? sequence : 0;
			previousFirstAxis = latestFirstAxis;
			latestFirstAxis = next.aimY;
			firstDuration = gap;
			from = cut ? next : shown;
			duration = gap < 0.05f ? 0.05f : (gap > 0.12f ? 0.12f : gap);
			elapsed = 0;
			gap = 0;
			sequence = serial;
			match = matchId;
		}
		shown = next; // The result, target mask, player and input phase always come from the host.
		elapsed += dt;
		const bool firstAxis = exactFirst && next.aimStage == 1;
		if (firstAxis)
		{
			firstBlend = !previousFirstView ? 0u : elapsed >= firstDuration ? 65535u :
			             TUINT32((elapsed / firstDuration) * 65535.0f + 0.5f);
			if (previousFirstView) shown.aimY = DartsBlendAxis(previousFirstAxis,next.aimY,firstBlend);
		}
		if (elapsed >= duration) return;
		const float alpha = elapsed / duration;
		if (from.state == next.state)
		{
			shown.aimX = Blend(from.aimX,next.aimX,alpha);
			if (!firstAxis) shown.aimY = Blend(from.aimY,next.aimY,alpha);
			shown.timeLeft = Blend(from.timeLeft,next.timeLeft,alpha);
		}
		for (int p = 0; p < 2; ++p)
			for (int axis = 0; axis < 3; ++axis)
				shown.players[p].direction[axis] = Blend(from.players[p].direction[axis],next.players[p].direction[axis],alpha);
		constexpr float pi = 3.14159265f;
		for (int r = 0; r < 3; ++r)
		{
			float delta = next.ringAngles[r] - from.ringAngles[r];
			while (delta > pi) delta -= 2*pi;
			while (delta < -pi) delta += 2*pi;
			float angle = from.ringAngles[r] + alpha*delta;
			if (angle > pi) angle -= 2*pi;
			if (angle < -pi) angle += 2*pi;
			shown.ringAngles[r] = angle;
		}
		for (int i = 0; i < 20; ++i)
		{
			if (!(from.visibleDarts & next.visibleDarts & (1u << i))) continue;
			auto& pose = shown.darts[i];
			pose.x = Blend(from.darts[i].x,pose.x,alpha);
			pose.y = Blend(from.darts[i].y,pose.y,alpha);
			pose.z = Blend(from.darts[i].z,pose.z,alpha);
			pose.scale = Blend(from.darts[i].scale,pose.scale,alpha);
			// Use an accepted basis; interpolating matrix elements can create a singular transform.
		}
	}
};

// Resolve presses against a short history of the host's own published aim values.
// A guest can name a recent view but cannot supply a position or score.
struct DartsAimHistory
{
	struct Sample { TUINT32 sequence, match, dart, stage, tick; float axis; };
	static constexpr TUINT32 Capacity = 64;
	Sample samples[Capacity]{};
	TUINT32 latestRecorded = 0, beforeLatest = 0;
	void Record(const DartsViewFrame& frame, TUINT32 sequence, TUINT32 match, TUINT32 now)
	{
		if (sequence && frame.aimStage)
		{
			beforeLatest = latestRecorded;
			latestRecorded = sequence;
			samples[sequence % Capacity] = {sequence, match, frame.throwSerial, frame.aimStage, now,
			                          frame.aimStage == 1 ? frame.aimY : frame.aimX};
		}
	}
	bool Resolve(TUINT32 sequence, TUINT32 match, TUINT32 dart, TUINT32 stage, TUINT32 now, float& aim) const
	{
		const Sample& s = samples[sequence % Capacity];
		if (!sequence || s.sequence != sequence || s.match != match || s.dart != dart ||
		    s.stage != stage || now-s.tick > DartsAimWindowMs) return false;
		aim = s.axis;
		return true;
	}
	bool ResolveClick(TUINT32 latest, TUINT32 previous, TUINT32 fraction, TUINT32 match, TUINT32 dart,
	                  TUINT32 stage, TUINT32 now, float& aim) const
	{
		if (!previous) return !fraction && Resolve(latest,match,dart,stage,now,aim);
		float a, b;
		if (stage != 1 || fraction > 65535 || TINT32(latest-previous) <= 0 ||
		    !Resolve(previous,match,dart,stage,now,a) || !Resolve(latest,match,dart,stage,now,b) ||
		    samples[latest % Capacity].tick-samples[previous % Capacity].tick > 200) return false;
		aim = DartsBlendAxis(a,b,fraction);
		return true;
	}
	TUINT32 Closest(TUINT32 latest, TUINT32 match, TUINT32 dart, TUINT32 stage, TUINT32 now, float axis) const
	{
		// The renderer may span two packets. Choose the nearest *observed* sample from this
		// segment. Looking far back would permit an unrelated pass of the oscillating bar.
		TUINT32 best = 0;
		float error = 4.0f;
		const TUINT32 candidates[] = {latest, beforeLatest};
		for (TUINT32 sequence : candidates)
		{
			float value;
			if (!Resolve(sequence,match,dart,stage,now,value)) continue;
			const float difference = value > axis ? value-axis : axis-value;
			if (difference < error) { best = sequence; error = difference; }
		}
		return best;
	}
};
