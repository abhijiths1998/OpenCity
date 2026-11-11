#include "Layout/OCLayoutCompat.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace OCLayout
{
	// --- CPython random.Random (Modules/_randommodule.c, mt19937ar) -----------------------------

	void FPyRandom::InitGenrand(uint32 S)
	{
		Mt[0] = S;
		for (int32 i = 1; i < 624; ++i)
		{
			Mt[i] = 1812433253u * (Mt[i - 1] ^ (Mt[i - 1] >> 30)) + uint32(i);
		}
		Index = 624;
	}

	FPyRandom::FPyRandom(uint32 Seed)
	{
		// random.seed(int): init_by_array with the 32-bit words of abs(seed) (one word here).
		const uint32 Key[1] = { Seed };
		const int32 KeyLen = 1;
		InitGenrand(19650218u);
		int32 i = 1, j = 0;
		for (int32 k = FMath::Max(624, KeyLen); k > 0; --k)
		{
			Mt[i] = (Mt[i] ^ ((Mt[i - 1] ^ (Mt[i - 1] >> 30)) * 1664525u)) + Key[j] + uint32(j);
			++i;
			++j;
			if (i >= 624) { Mt[0] = Mt[623]; i = 1; }
			if (j >= KeyLen) { j = 0; }
		}
		for (int32 k = 623; k > 0; --k)
		{
			Mt[i] = (Mt[i] ^ ((Mt[i - 1] ^ (Mt[i - 1] >> 30)) * 1566083941u)) - uint32(i);
			++i;
			if (i >= 624) { Mt[0] = Mt[623]; i = 1; }
		}
		Mt[0] = 0x80000000u;
		Index = 624;
	}

	uint32 FPyRandom::NextU32()
	{
		static const uint32 Mag01[2] = { 0u, 0x9908b0dfu };
		if (Index >= 624)
		{
			int32 kk = 0;
			uint32 y;
			for (; kk < 624 - 397; ++kk)
			{
				y = (Mt[kk] & 0x80000000u) | (Mt[kk + 1] & 0x7fffffffu);
				Mt[kk] = Mt[kk + 397] ^ (y >> 1) ^ Mag01[y & 1u];
			}
			for (; kk < 623; ++kk)
			{
				y = (Mt[kk] & 0x80000000u) | (Mt[kk + 1] & 0x7fffffffu);
				Mt[kk] = Mt[kk + (397 - 624)] ^ (y >> 1) ^ Mag01[y & 1u];
			}
			y = (Mt[623] & 0x80000000u) | (Mt[0] & 0x7fffffffu);
			Mt[623] = Mt[396] ^ (y >> 1) ^ Mag01[y & 1u];
			Index = 0;
		}
		uint32 y = Mt[Index++];
		y ^= (y >> 11);
		y ^= (y << 7) & 0x9d2c5680u;
		y ^= (y << 15) & 0xefc60000u;
		y ^= (y >> 18);
		return y;
	}

	double FPyRandom::Random()
	{
		const uint32 A = NextU32() >> 5, B = NextU32() >> 6;
		return (double(A) * 67108864.0 + double(B)) * (1.0 / 9007199254740992.0);
	}

	// --- NumPy default_rng: SeedSequence + PCG64 (bit_generator.pyx, pcg64.h) ------------------

	namespace
	{
		constexpr uint32 InitA = 0x43b0d7e5u, MultA = 0x931e8875u, InitB = 0x8b51f9ddu, MultB = 0x58f38dedu;
		constexpr uint32 MixMultL = 0xca01f9ddu, MixMultR = 0x4973f715u;

		uint32 HashMix(uint32 Value, uint32& HashConst)
		{
			Value ^= HashConst;
			HashConst *= MultA;
			Value *= HashConst;
			Value ^= Value >> 16;
			return Value;
		}

		uint32 Mix(uint32 X, uint32 Y)
		{
			uint32 R = MixMultL * X - MixMultR * Y;
			R ^= R >> 16;
			return R;
		}

		using u128 = unsigned __int128;
		const u128 PcgMult = (u128(2549297995355413924ull) << 64) | u128(4865540595714422341ull);
	}

	FNpRandom::FNpRandom(uint32 Seed)
	{
		// SeedSequence(seed): entropy = [seed] (one 32-bit word), pool of 4 words.
		uint32 Pool[4];
		uint32 HashConst = InitA;
		const uint32 Entropy[1] = { Seed };
		for (int32 i = 0; i < 4; ++i)
		{
			Pool[i] = HashMix(i < 1 ? Entropy[i] : 0u, HashConst);
		}
		for (int32 Src = 0; Src < 4; ++Src)
		{
			for (int32 Dst = 0; Dst < 4; ++Dst)
			{
				if (Src != Dst)
				{
					Pool[Dst] = Mix(Pool[Dst], HashMix(Pool[Src], HashConst));
				}
			}
		}
		// generate_state(4, uint64): 8 words cycling through the pool, read as little-endian uint64s.
		uint32 Words[8];
		uint32 H = InitB;
		for (int32 i = 0; i < 8; ++i)
		{
			uint32 V = Pool[i % 4];
			V ^= H;
			H *= MultB;
			V *= H;
			V ^= V >> 16;
			Words[i] = V;
		}
		uint64 Val[4];
		for (int32 k = 0; k < 4; ++k)
		{
			Val[k] = uint64(Words[2 * k]) | (uint64(Words[2 * k + 1]) << 32);
		}
		// pcg64_set_seed: state = val[0]:val[1], inc = val[2]:val[3]; srandom: inc = (seq << 1) | 1,
		// state = 0, step, state += initstate, step.
		const u128 InitState = (u128(Val[0]) << 64) | u128(Val[1]);
		const u128 InitSeq = (u128(Val[2]) << 64) | u128(Val[3]);
		const u128 Inc = (InitSeq << 1) | 1u;
		u128 State = 0;
		State = State * PcgMult + Inc;
		State += InitState;
		State = State * PcgMult + Inc;
		StateHi = uint64(State >> 64);
		StateLo = uint64(State);
		IncHi = uint64(Inc >> 64);
		IncLo = uint64(Inc);
	}

	void FNpRandom::Step()
	{
		u128 State = (u128(StateHi) << 64) | u128(StateLo);
		const u128 Inc = (u128(IncHi) << 64) | u128(IncLo);
		State = State * PcgMult + Inc;
		StateHi = uint64(State >> 64);
		StateLo = uint64(State);
	}

	uint64 FNpRandom::Next64()
	{
		Step();
		const uint64 X = StateHi ^ StateLo;
		const uint32 Rot = uint32(StateHi >> 58);
		return (X >> Rot) | (X << ((64u - Rot) & 63u));
	}

	// --- Python number formatting ---------------------------------------------------------------

	double PyRound(double X, int32 Digits)
	{
		if (!std::isfinite(X))
		{
			return X;
		}
		char Buf[64];
		std::snprintf(Buf, sizeof(Buf), "%.*f", Digits, X);  // glibc rounds the exact binary value, ties to even
		return std::strtod(Buf, nullptr);
	}

	int64 PyRoundInt(double X)
	{
		return int64(std::nearbyint(X));
	}

	FString PyRepr(double X)
	{
		if (X == 0.0)
		{
			return std::signbit(X) ? TEXT("-0.0") : TEXT("0.0");
		}
		// Shortest round-trip digits: the first precision whose correctly rounded value reads back exactly.
		char Buf[64];
		for (int32 P = 0; P < 17; ++P)
		{
			std::snprintf(Buf, sizeof(Buf), "%.*e", P, X);
			if (std::strtod(Buf, nullptr) == X)
			{
				break;
			}
		}
		// Buf = [-]d[.ddd]e[+-]xx
		FString S(ANSI_TO_TCHAR(Buf));
		const bool bNeg = S.StartsWith(TEXT("-"));
		if (bNeg)
		{
			S.RightChopInline(1);
		}
		int32 EPos = INDEX_NONE;
		S.FindChar(TEXT('e'), EPos);
		const FString Mant = S.Left(EPos);
		const int32 Exp = FCString::Atoi(*S.Mid(EPos + 1));
		const FString Digits = Mant.Replace(TEXT("."), TEXT(""));
		const int32 DecPt = Exp + 1;
		FString Out;
		if (DecPt <= -4 || DecPt > 16)
		{
			Out = Digits.Left(1);
			if (Digits.Len() > 1)
			{
				Out += TEXT(".") + Digits.Mid(1);
			}
			Out += FString::Printf(TEXT("e%c%02d"), Exp < 0 ? TEXT('-') : TEXT('+'), FMath::Abs(Exp));
		}
		else if (DecPt <= 0)
		{
			Out = TEXT("0.") + FString::ChrN(-DecPt, TEXT('0')) + Digits;
		}
		else if (DecPt >= Digits.Len())
		{
			Out = Digits + FString::ChrN(DecPt - Digits.Len(), TEXT('0')) + TEXT(".0");
		}
		else
		{
			Out = Digits.Left(DecPt) + TEXT(".") + Digits.Mid(DecPt);
		}
		return bNeg ? TEXT("-") + Out : Out;
	}

	// --- JSON -----------------------------------------------------------------------------------

	FJsonPtr FJson::MakeNull() { return MakeShared<FJson>(); }
	FJsonPtr FJson::MakeBool(bool V) { FJsonPtr J = MakeShared<FJson>(); J->Kind = EKind::Bool; J->Bool = V; return J; }
	FJsonPtr FJson::MakeInt(int64 V) { FJsonPtr J = MakeShared<FJson>(); J->Kind = EKind::Int; J->Int = V; return J; }
	FJsonPtr FJson::MakeFloat(double V) { FJsonPtr J = MakeShared<FJson>(); J->Kind = EKind::Float; J->Num = V; return J; }
	FJsonPtr FJson::MakeString(const FString& V) { FJsonPtr J = MakeShared<FJson>(); J->Kind = EKind::String; J->Str = V; return J; }
	FJsonPtr FJson::MakeArray() { FJsonPtr J = MakeShared<FJson>(); J->Kind = EKind::Array; return J; }
	FJsonPtr FJson::MakeObject() { FJsonPtr J = MakeShared<FJson>(); J->Kind = EKind::Object; return J; }

	FJson& FJson::Set(const FString& Key, const FJsonPtr& V)
	{
		const int32 i = Keys.IndexOfByKey(Key);
		if (i == INDEX_NONE)
		{
			Keys.Add(Key);
			Items.Add(V);
		}
		else
		{
			Items[i] = V;
		}
		return *this;
	}

	FJsonPtr FJson::Get(const FString& Key) const
	{
		const int32 i = Keys.IndexOfByKey(Key);
		return i == INDEX_NONE ? nullptr : Items[i];
	}

	namespace
	{
		void WriteString(FString& Out, const FString& S)
		{
			Out += TEXT('"');
			for (const TCHAR C : S)
			{
				switch (C)
				{
				case TEXT('"'): Out += TEXT("\\\""); break;
				case TEXT('\\'): Out += TEXT("\\\\"); break;
				case TEXT('\n'): Out += TEXT("\\n"); break;
				case TEXT('\r'): Out += TEXT("\\r"); break;
				case TEXT('\t'): Out += TEXT("\\t"); break;
				case TEXT('\b'): Out += TEXT("\\b"); break;
				case TEXT('\f'): Out += TEXT("\\f"); break;
				default:
					if (C < 0x20 || C > 0x7e)
					{
						Out += FString::Printf(TEXT("\\u%04x"), uint32(C));
					}
					else
					{
						Out += C;
					}
				}
			}
			Out += TEXT('"');
		}
	}

	void FJson::WriteTo(FString& Out, int32 Indent) const
	{
		switch (Kind)
		{
		case EKind::Null: Out += TEXT("null"); return;
		case EKind::Bool: Out += Bool ? TEXT("true") : TEXT("false"); return;
		case EKind::Int: Out += FString::Printf(TEXT("%lld"), (long long)Int); return;
		case EKind::Float: Out += PyRepr(Num); return;
		case EKind::String: WriteString(Out, Str); return;
		case EKind::Array:
		case EKind::Object:
		{
			const bool bObj = Kind == EKind::Object;
			if (Items.IsEmpty())
			{
				Out += bObj ? TEXT("{}") : TEXT("[]");
				return;
			}
			Out += bObj ? TEXT("{") : TEXT("[");
			const FString Pad = FString::ChrN(Indent + 1, TEXT(' '));
			for (int32 i = 0; i < Items.Num(); ++i)
			{
				Out += i == 0 ? TEXT("\n") : TEXT(",\n");
				Out += Pad;
				if (bObj)
				{
					WriteString(Out, Keys[i]);
					Out += TEXT(": ");
				}
				Items[i]->WriteTo(Out, Indent + 1);
			}
			Out += TEXT("\n") + FString::ChrN(Indent, TEXT(' '));
			Out += bObj ? TEXT("}") : TEXT("]");
			return;
		}
		}
	}

	FString FJson::Write() const
	{
		FString Out;
		Out.Reserve(4 * 1024 * 1024);
		WriteTo(Out, 0);
		return Out;
	}

	namespace
	{
		struct FParser
		{
			const TCHAR* P;
			const TCHAR* End;
			FString Error;

			void Ws() { while (P < End && (*P == ' ' || *P == '\n' || *P == '\r' || *P == '\t')) { ++P; } }

			FJsonPtr Value()
			{
				Ws();
				if (P >= End) { Error = TEXT("unexpected end"); return nullptr; }
				const TCHAR C = *P;
				if (C == '{')
				{
					++P;
					FJsonPtr O = FJson::MakeObject();
					Ws();
					if (P < End && *P == '}') { ++P; return O; }
					while (true)
					{
						Ws();
						FJsonPtr K = Value();
						if (!K || K->Kind != FJson::EKind::String) { Error = TEXT("object key"); return nullptr; }
						Ws();
						if (P >= End || *P != ':') { Error = TEXT("expected ':'"); return nullptr; }
						++P;
						FJsonPtr V = Value();
						if (!V) { return nullptr; }
						O->Keys.Add(K->Str);
						O->Items.Add(V);
						Ws();
						if (P < End && *P == ',') { ++P; continue; }
						if (P < End && *P == '}') { ++P; return O; }
						Error = TEXT("expected ',' or '}'");
						return nullptr;
					}
				}
				if (C == '[')
				{
					++P;
					FJsonPtr A = FJson::MakeArray();
					Ws();
					if (P < End && *P == ']') { ++P; return A; }
					while (true)
					{
						FJsonPtr V = Value();
						if (!V) { return nullptr; }
						A->Items.Add(V);
						Ws();
						if (P < End && *P == ',') { ++P; continue; }
						if (P < End && *P == ']') { ++P; return A; }
						Error = TEXT("expected ',' or ']'");
						return nullptr;
					}
				}
				if (C == '"')
				{
					++P;
					FString S;
					while (P < End && *P != '"')
					{
						if (*P == '\\' && P + 1 < End)
						{
							++P;
							switch (*P)
							{
							case 'n': S += TEXT('\n'); break;
							case 'r': S += TEXT('\r'); break;
							case 't': S += TEXT('\t'); break;
							case 'b': S += TEXT('\b'); break;
							case 'f': S += TEXT('\f'); break;
							case 'u':
								if (P + 4 < End)
								{
									S += TCHAR(FParse::HexNumber(*FString(4, P + 1)));
									P += 4;
								}
								break;
							default: S += *P;
							}
							++P;
							continue;
						}
						S += *P++;
					}
					++P;
					return FJson::MakeString(S);
				}
				if (C == 't' && End - P >= 4) { P += 4; return FJson::MakeBool(true); }
				if (C == 'f' && End - P >= 5) { P += 5; return FJson::MakeBool(false); }
				if (C == 'n' && End - P >= 4) { P += 4; return FJson::MakeNull(); }
				// Number
				const TCHAR* Start = P;
				bool bFloat = false;
				while (P < End && (FChar::IsDigit(*P) || *P == '-' || *P == '+' || *P == '.' || *P == 'e' || *P == 'E'))
				{
					bFloat |= *P == '.' || *P == 'e' || *P == 'E';
					++P;
				}
				if (P == Start) { Error = FString::Printf(TEXT("unexpected '%c'"), C); return nullptr; }
				const FString Lit(int32(P - Start), Start);
				FJsonPtr N = bFloat ? FJson::MakeFloat(std::strtod(TCHAR_TO_ANSI(*Lit), nullptr)) : FJson::MakeInt(FCString::Atoi64(*Lit));
				N->Raw = Lit;
				return N;
			}
		};
	}

	FJsonPtr FJson::Parse(const FString& Text, FString& OutError)
	{
		FParser Parser{ *Text, *Text + Text.Len() };
		FJsonPtr V = Parser.Value();
		OutError = Parser.Error;
		return V;
	}

	int32 FJson::Compare(const FJsonPtr& A, const FJsonPtr& B, double Tolerance, int32 MaxReport, TArray<FString>& OutReport, const FString& Path)
	{
		auto Report = [&](const FString& What)
		{
			if (OutReport.Num() < MaxReport)
			{
				OutReport.Add(Path + TEXT(": ") + What);
			}
			return 1;
		};
		if (!A || !B)
		{
			return (A || B) ? Report(TEXT("missing")) : 0;
		}
		const bool bNumA = A->Kind == EKind::Int || A->Kind == EKind::Float;
		const bool bNumB = B->Kind == EKind::Int || B->Kind == EKind::Float;
		if (bNumA && bNumB)
		{
			const double DA = A->AsNumber(), DB = B->AsNumber();
			return FMath::Abs(DA - DB) <= Tolerance ? 0 : Report(FString::Printf(TEXT("%s vs %s"), *PyRepr(DA), *PyRepr(DB)));
		}
		if (A->Kind != B->Kind)
		{
			return Report(TEXT("different types"));
		}
		switch (A->Kind)
		{
		case EKind::Bool: return A->Bool == B->Bool ? 0 : Report(TEXT("bool differs"));
		case EKind::String: return A->Str == B->Str ? 0 : Report(FString::Printf(TEXT("'%s' vs '%s'"), *A->Str, *B->Str));
		case EKind::Array:
		{
			int32 N = 0;
			if (A->Items.Num() != B->Items.Num())
			{
				N += Report(FString::Printf(TEXT("%d items vs %d"), A->Items.Num(), B->Items.Num()));
			}
			for (int32 i = 0; i < FMath::Min(A->Items.Num(), B->Items.Num()); ++i)
			{
				N += Compare(A->Items[i], B->Items[i], Tolerance, MaxReport, OutReport, FString::Printf(TEXT("%s[%d]"), *Path, i));
			}
			return N;
		}
		case EKind::Object:
		{
			int32 N = 0;
			for (int32 i = 0; i < A->Keys.Num(); ++i)
			{
				N += Compare(A->Items[i], B->Get(A->Keys[i]), Tolerance, MaxReport, OutReport, Path + TEXT(".") + A->Keys[i]);
			}
			for (const FString& K : B->Keys)
			{
				if (!A->Get(K))
				{
					N += Report(TEXT("extra key ") + K);
				}
			}
			return N;
		}
		default: return 0;
		}
	}
}

int32 OCLayout::FPyRandom::RandBelow(int32 N)
{
	check(N > 0);
	const int32 K = FMath::FloorLog2(uint32(N)) + 1;   // n.bit_length()
	uint32 R = NextU32() >> (32 - K);
	while (R >= uint32(N))
	{
		R = NextU32() >> (32 - K);
	}
	return int32(R);
}

void OCLayout::FJson::WriteCompactTo(FString& Out) const
{
	if (Kind != EKind::Array && Kind != EKind::Object)
	{
		WriteTo(Out, 0);
		return;
	}
	const bool bObj = Kind == EKind::Object;
	Out += bObj ? TEXT("{") : TEXT("[");
	for (int32 i = 0; i < Items.Num(); ++i)
	{
		if (i > 0) { Out += TEXT(","); }
		if (bObj)
		{
			MakeString(Keys[i])->WriteTo(Out, 0);
			Out += TEXT(":");
		}
		Items[i]->WriteCompactTo(Out);
	}
	Out += bObj ? TEXT("}") : TEXT("]");
}

FString OCLayout::FJson::WriteCompact() const
{
	FString Out;
	Out.Reserve(1024 * 1024);
	WriteCompactTo(Out);
	return Out;
}
