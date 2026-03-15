#include "util/string.h"
#include <array>
#include <cppspec.hpp>

extern "C" void putchar_(char c) {
	putchar(c);
}

// clang-format off
describe string("deluge::string", ${
	using namespace deluge;
	using namespace std::literals;
	context("fromInt", _ {
		it("converts an integer into a string", _ {
			expect(string::fromInt(42)).to_equal("42"s);
		});

		it("left-pads with '0's", _ {
			expect(string::fromInt(42, 3)).to_equal("042"s);
		});
	});

	context("fromFloat", _ {
		it("converts a float into a string", _ {
			expect(string::fromFloat(3.14, 2)).to_equal("3.14"s);
		});

		it("rounds to the given precision", _ {
			expect(string::fromFloat(3.14159, 3)).to_equal("3.142"s);
		});
	});

	context("to_chars", _ {
		it("converts a float to a char buffer", _ {
			std::array<char, 32> buf{};
			auto result = to_chars(buf.data(), buf.data() + buf.size(), 3.14f, 2);
			expect(result.has_value()).to_equal(true);
			expect(std::string(buf.data())).to_equal("3.14"s);
		});

		it("returns error when buffer is too small", _ {
			std::array<char, 2> buf{};
			auto result = to_chars(buf.data(), buf.data() + buf.size(), 3.14f, 2);
			expect(result.has_value()).to_equal(false);
			expect(result.error()).to_equal(std::errc::no_buffer_space);
		});

		it("returns error when first >= last", _ {
			std::array<char, 32> buf{};
			auto result = to_chars(buf.data(), buf.data(), 1.0f, 1);
			expect(result.has_value()).to_equal(false);
			expect(result.error()).to_equal(std::errc::no_buffer_space);
		});

		it("handles zero-length precision", _ {
			std::array<char, 32> buf{};
			auto result = to_chars(buf.data(), buf.data() + buf.size(), 42.0f, 0);
			expect(result.has_value()).to_equal(true);
			expect(std::string(buf.data())).to_equal("42"s);
		});
	});

	context("fromSlot", _ {
		it("converts a slot and sub-slot into a string", _ {
			expect(string::fromSlot(3, 1)).to_equal("3B"s);
		});

		it("left-pads the slot with '0's", _ {
			expect(string::fromSlot(3, 1, 3)).to_equal("003B"s);
		});
	});

	context("fromNoteCode", _ {
		it("converts a note code into a string", _ {
			expect(string::fromNoteCode(60)).to_equal("C3"s);
		});

		it("appends the octave number", _ {
			expect(string::fromNoteCode(63, nullptr, true)).to_equal("D.3"s);
		});

		it("does not append the octave number", _ {
			expect(string::fromNoteCode(60, nullptr, false)).to_equal("C"s);
		});

		it("returns the length without the dot", _ {
			size_t length;
			expect(string::fromNoteCode(63, &length)).to_equal("D.3"s);
			expect(length).to_equal(2);
		});
	});
});

CPPSPEC_SPEC(string);
