#pragma once

// Bit-compatible building blocks for the Marevia layout generator (UOCLayoutCommandlet), which was
// first written in Python with NumPy. Reproducing the same random streams and rounding keeps the
// generated city identical, so the C++ port could be checked against the committed layout:
// - FPyRandom: CPython's random.Random (MT19937, init_by_array seeding, random(), uniform()).
// - FNpRandom: NumPy's default_rng (PCG64 seeded through SeedSequence, random()).
// - PyRound: Python's round() (correctly rounded, ties to even).
// - FJson: an ordered JSON tree that keeps ints and floats apart and writes like json.dumps(indent=1).

#include "CoreMinimal.h"

namespace OCLayout
{
	class FPyRandom
	{
	public:
		explicit FPyRandom(uint32 Seed);
		uint32 NextU32();
		/** random.random(): 53-bit double in [0, 1). */
		double Random();
		/** random.uniform(a, b) = a + (b - a) * random(). */
		double Uniform(double A, double B) { return A + (B - A) * Random(); }
		/** random._randbelow(n) (getrandbits rejection), as random.choice uses it. */
		int32 RandBelow(int32 N);

	private:
		uint32 Mt[624];
		int32 Index = 625;
		void InitGenrand(uint32 S);
	};

	class FNpRandom
	{
	public:
		explicit FNpRandom(uint32 Seed);
		uint64 Next64();
		/** Generator.random(): (next64 >> 11) / 2^53. */
		double Random() { return double(Next64() >> 11) * (1.0 / 9007199254740992.0); }

	private:
		// 128-bit state and increment as (high, low).
		uint64 StateHi = 0, StateLo = 0, IncHi = 0, IncLo = 0;
		void Step();
	};

	/** Python round(x, ndigits) for floats: correctly rounded to ndigits decimals, ties to even. */
	double PyRound(double X, int32 Digits);
	/** Python round(x) -> int: nearest, ties to even. */
	int64 PyRoundInt(double X);
	/** Python float.__repr__ (shortest round-trip digits, exponent outside 1e-4..1e16). */
	FString PyRepr(double X);
	/** Python's x // y for floats. */
	inline double PyFloorDiv(double X, double Y) { return FMath::FloorToDouble(X / Y); }

	/** An ordered JSON value: objects keep key order, numbers keep int or float. */
	struct FJson;
	using FJsonPtr = TSharedPtr<FJson>;
	struct FJson
	{
		enum class EKind : uint8 { Null, Bool, Int, Float, String, Array, Object };
		EKind Kind = EKind::Null;
		bool Bool = false;
		int64 Int = 0;
		double Num = 0.0;
		FString Str;
		FString Raw;                // number literal as read (written back verbatim)
		TArray<FJsonPtr> Items;     // array items, or object values
		TArray<FString> Keys;       // object keys, parallel to Items

		static FJsonPtr MakeNull();
		static FJsonPtr MakeBool(bool V);
		static FJsonPtr MakeInt(int64 V);
		static FJsonPtr MakeFloat(double V);
		static FJsonPtr MakeString(const FString& V);
		static FJsonPtr MakeArray();
		static FJsonPtr MakeObject();

		/** Object: set or replace a key (a replaced key keeps its position, as in a Python dict). */
		FJson& Set(const FString& Key, const FJsonPtr& V);
		FJsonPtr Get(const FString& Key) const;
		FJson& Add(const FJsonPtr& V) { Items.Add(V); return *this; }
		double AsNumber() const { return Kind == EKind::Int ? double(Int) : Num; }

		/** json.dumps(value, indent=1). */
		FString Write() const;
		/** json.dumps(value, separators=(",", ":")). */
		FString WriteCompact() const;
		static FJsonPtr Parse(const FString& Text, FString& OutError);
		/** Every difference between two trees (numbers within Tolerance count as equal), up to MaxReport. */
		static int32 Compare(const FJsonPtr& A, const FJsonPtr& B, double Tolerance, int32 MaxReport, TArray<FString>& OutReport,
			const FString& Path = FString());

	private:
		void WriteTo(FString& Out, int32 Indent) const;
		void WriteCompactTo(FString& Out) const;
	};
}
