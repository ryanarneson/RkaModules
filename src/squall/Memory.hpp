#pragma once
// Squall's Memory (spec: Memory): the last 16 events, and the loop. A
// replay copies the event L - 1 back from the newest, so with every event
// replayed the output repeats with a period of L events. Gaps and gate
// lengths are fractions of the average gap, so a loop follows Weather.
namespace rka {
namespace squall {

struct Event {
	float gap = 1.f;      // since the event before, in average gaps
	float value = 0.5f;   // 0 ... 1, mapped to the range on output
	float length = 0.25f; // the gate, in average gaps
};

class Memory {
public:
	static const int CAP = 16;

	// 4, 8 or 16: anything else rounds up to the next of them.
	void setLength(int L) {
		length_ = L <= 4 ? 4 : (L <= 8 ? 8 : 16);
	}

	int length() const {
		return length_;
	}

	void push(const Event& e) {
		buf_[head_] = e;
		head_ = (head_ + 1) % CAP;
		if (count_ < CAP)
			count_++;
	}

	int count() const {
		return count_;
	}

	bool canReplay() const {
		return count_ >= length_;
	}

	// The event L - 1 back from the newest. Only when canReplay().
	Event next() const {
		return at(count_ - length_);
	}

	// Oldest first: at(0) ... at(count() - 1).
	const Event& at(int i) const {
		const int start = (head_ - count_ + CAP) % CAP;
		return buf_[(start + i) % CAP];
	}

	void clear() {
		count_ = head_ = 0;
	}

private:
	Event buf_[CAP];
	int head_ = 0, count_ = 0, length_ = 8;
};

} // namespace squall
} // namespace rka
