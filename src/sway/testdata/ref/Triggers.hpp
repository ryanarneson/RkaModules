// Frozen copy of src/sway/Triggers.hpp at a46828c (before the issue #8 CPU work), namespace rka ->
// rkaref: the reference test_engine.cpp compares the optimised code with.
// Never edit or regenerate.
#pragma once

// Rack-free copies of rack::dsp::SchmittTrigger (the float specialisation)
// and rack::dsp::PulseGenerator, with the same behaviour, so Sway's
// per-sample logic (SwayEngine.hpp) builds and benches without the SDK.
namespace rkaref {

struct SchmittTrigger {
	enum State { LOW, HIGH, UNINITIALIZED };
	State s = UNINITIALIZED;

	// Rising edge: true on the sample the input reaches high from low. An
	// uninitialised trigger settles without firing.
	bool process(float in, float lowThreshold, float highThreshold) {
		if (s == LOW && in >= highThreshold) {
			s = HIGH;
			return true;
		}
		else if (s == HIGH && in <= lowThreshold)
			s = LOW;
		else if (s == UNINITIALIZED && in >= highThreshold)
			s = HIGH;
		else if (s == UNINITIALIZED && in <= lowThreshold)
			s = LOW;
		return false;
	}

	bool isHigh() const {
		return s == HIGH;
	}
};

struct PulseGenerator {
	float remaining = 0.f;

	// Whether the pulse was high before this step.
	bool process(float deltaTime) {
		if (remaining > 0.f) {
			remaining -= deltaTime;
			return true;
		}
		return false;
	}

	void trigger(float duration) {
		if (duration > remaining)
			remaining = duration;
	}
};

} // namespace rkaref
