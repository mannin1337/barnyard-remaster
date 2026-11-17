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

struct DartsViewMotion
{
	DartsViewFrame from{}, shown{};
	TUINT32 sequence = 0, match = 0;
	float gap = 0, elapsed = 0, duration = 0.05f;

	static float Blend(float a, float b, float alpha) { return a + (b-a)*alpha; }
	void Update(const DartsViewFrame& next, TUINT32 serial, TUINT32 matchId, float dt)
	{
		gap += dt;
		if (serial != sequence || matchId != match)
		{
			const bool cut = !sequence || match != matchId || shown.round != next.round ||
			                 shown.throwSerial != next.throwSerial || gap > 0.2f;
			from = cut ? next : shown;
			duration = gap < 0.05f ? 0.05f : (gap > 0.12f ? 0.12f : gap);
			elapsed = 0;
			gap = 0;
			sequence = serial;
			match = matchId;
		}
		shown = next; // The result, target mask, player and input phase always come from the host.
		elapsed += dt;
		if (elapsed >= duration) return;
		const float alpha = elapsed / duration;
		if (from.state == next.state)
		{
			shown.aimX = Blend(from.aimX,next.aimX,alpha);
			shown.aimY = Blend(from.aimY,next.aimY,alpha);
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
