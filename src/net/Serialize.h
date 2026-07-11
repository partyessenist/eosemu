#pragma once

//
// Minimal length-prefixed serialization for LAN payloads.
//
// Every multi-field payload (Hello, lobby/session announcements, invites) is
// encoded with these helpers so the wire format is explicit and versioned
// alongside WireHeader. Little-endian, length-prefixed strings, bounds-checked
// reads that fail closed -- a malformed datagram yields defaults, never a read
// past the buffer.
//

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace EOSEmu
{
	namespace net
	{
		class ByteWriter
		{
		public:
			void U8(uint8_t V) { Buf_.push_back(V); }
			void U16(uint16_t V) { Append(&V, sizeof(V)); }
			void U32(uint32_t V) { Append(&V, sizeof(V)); }
			void U64(uint64_t V) { Append(&V, sizeof(V)); }
			void I64(int64_t V) { Append(&V, sizeof(V)); }
			void Dbl(double V) { Append(&V, sizeof(V)); }
			void Bool(bool V) { U8(V ? 1 : 0); }

			void Str(const std::string& S)
			{
				U32(static_cast<uint32_t>(S.size()));
				Buf_.insert(Buf_.end(), S.begin(), S.end());
			}

			void Bytes(const void* Data, uint32_t Len)
			{
				U32(Len);
				const uint8_t* P = static_cast<const uint8_t*>(Data);
				Buf_.insert(Buf_.end(), P, P + Len);
			}

			const std::vector<uint8_t>& Data() const { return Buf_; }
			uint16_t Size() const { return static_cast<uint16_t>(Buf_.size()); }

		private:
			void Append(const void* P, size_t N)
			{
				const uint8_t* B = static_cast<const uint8_t*>(P);
				Buf_.insert(Buf_.end(), B, B + N);
			}
			std::vector<uint8_t> Buf_;
		};

		class ByteReader
		{
		public:
			ByteReader(const uint8_t* Data, size_t Len) : Data_(Data), Len_(Len) {}

			bool Ok() const { return Ok_; }
			size_t Remaining() const { return Len_ - Pos_; }

			uint8_t U8() { uint8_t V = 0; Read(&V, sizeof(V)); return V; }
			uint16_t U16() { uint16_t V = 0; Read(&V, sizeof(V)); return V; }
			uint32_t U32() { uint32_t V = 0; Read(&V, sizeof(V)); return V; }
			uint64_t U64() { uint64_t V = 0; Read(&V, sizeof(V)); return V; }
			int64_t I64() { int64_t V = 0; Read(&V, sizeof(V)); return V; }
			double Dbl() { double V = 0; Read(&V, sizeof(V)); return V; }
			bool Bool() { return U8() != 0; }

			std::string Str()
			{
				const uint32_t N = U32();
				if (!Ok_ || N > Remaining())
				{
					Ok_ = false;
					return {};
				}
				std::string S(reinterpret_cast<const char*>(Data_ + Pos_), N);
				Pos_ += N;
				return S;
			}

			std::vector<uint8_t> Bytes()
			{
				const uint32_t N = U32();
				if (!Ok_ || N > Remaining())
				{
					Ok_ = false;
					return {};
				}
				std::vector<uint8_t> B(Data_ + Pos_, Data_ + Pos_ + N);
				Pos_ += N;
				return B;
			}

		private:
			void Read(void* Out, size_t N)
			{
				if (!Ok_ || Remaining() < N)
				{
					Ok_ = false;
					return;
				}
				std::memcpy(Out, Data_ + Pos_, N);
				Pos_ += N;
			}

			const uint8_t* Data_;
			size_t Len_;
			size_t Pos_ = 0;
			bool Ok_ = true;
		};
	}
}
