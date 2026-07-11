#pragma once

//
// Shared attribute value model for Lobby and Sessions.
//
// Both interfaces use the identical EOS_*_AttributeData shape (a tagged union of
// int64/double/bool/utf8) and the same EOS_EComparisonOp search semantics, so
// the value type, its wire encoding and the comparison logic live here once.
//

#include <string>

#include "eos_common.h"
#include "net/Serialize.h"

namespace EOSEmu
{
	struct AttrValue
	{
		EOS_EAttributeType Type = EOS_EAttributeType::EOS_AT_BOOLEAN;
		int64_t Int = 0;
		double Dbl = 0.0;
		EOS_Bool Bool = EOS_FALSE;
		std::string Str;

		static AttrValue MakeBool(EOS_Bool V) { AttrValue A; A.Type = EOS_EAttributeType::EOS_AT_BOOLEAN; A.Bool = V; return A; }
		static AttrValue MakeInt(int64_t V) { AttrValue A; A.Type = EOS_EAttributeType::EOS_AT_INT64; A.Int = V; return A; }
		static AttrValue MakeDouble(double V) { AttrValue A; A.Type = EOS_EAttributeType::EOS_AT_DOUBLE; A.Dbl = V; return A; }
		static AttrValue MakeString(const char* V) { AttrValue A; A.Type = EOS_EAttributeType::EOS_AT_STRING; A.Str = V ? V : ""; return A; }
	};

	/// Builds an AttrValue from the raw union fields (both interfaces' AttributeData
	/// share this layout).
	AttrValue ReadAttr(EOS_EAttributeType Type, int64_t AsInt64, double AsDouble, EOS_Bool AsBool, const char* AsUtf8);

	void WriteAttrWire(net::ByteWriter& W, const std::string& Key, const AttrValue& V);
	bool ReadAttrWire(net::ByteReader& R, std::string& OutKey, AttrValue& OutValue);

	/// Evaluates `Stored <op> Query`, e.g. does the stored attribute satisfy the
	/// searcher's parameter. Comparisons only apply between matching value types.
	bool CompareAttr(const AttrValue& Stored, EOS_EComparisonOp Op, const AttrValue& Query);

	inline bool AttrEqual(const AttrValue& A, const AttrValue& B)
	{
		if (A.Type != B.Type) return false;
		switch (A.Type)
		{
		case EOS_EAttributeType::EOS_AT_BOOLEAN: return A.Bool == B.Bool;
		case EOS_EAttributeType::EOS_AT_INT64:   return A.Int == B.Int;
		case EOS_EAttributeType::EOS_AT_DOUBLE:  return A.Dbl == B.Dbl;
		case EOS_EAttributeType::EOS_AT_STRING:  return A.Str == B.Str;
		}
		return false;
	}
}
