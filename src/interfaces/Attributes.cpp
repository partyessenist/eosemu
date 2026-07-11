#include "interfaces/Attributes.h"

namespace EOSEmu
{
	AttrValue ReadAttr(EOS_EAttributeType Type, int64_t AsInt64, double AsDouble, EOS_Bool AsBool, const char* AsUtf8)
	{
		AttrValue V;
		V.Type = Type;
		switch (Type)
		{
		case EOS_EAttributeType::EOS_AT_BOOLEAN: V.Bool = AsBool; break;
		case EOS_EAttributeType::EOS_AT_INT64: V.Int = AsInt64; break;
		case EOS_EAttributeType::EOS_AT_DOUBLE: V.Dbl = AsDouble; break;
		case EOS_EAttributeType::EOS_AT_STRING: V.Str = AsUtf8 ? AsUtf8 : ""; break;
		default: break;
		}
		return V;
	}

	void WriteAttrWire(net::ByteWriter& W, const std::string& Key, const AttrValue& V)
	{
		W.Str(Key);
		W.U8(static_cast<uint8_t>(V.Type));
		switch (V.Type)
		{
		case EOS_EAttributeType::EOS_AT_BOOLEAN: W.U8(V.Bool ? 1 : 0); break;
		case EOS_EAttributeType::EOS_AT_INT64: W.I64(V.Int); break;
		case EOS_EAttributeType::EOS_AT_DOUBLE: W.Dbl(V.Dbl); break;
		case EOS_EAttributeType::EOS_AT_STRING: W.Str(V.Str); break;
		default: break;
		}
	}

	bool ReadAttrWire(net::ByteReader& R, std::string& OutKey, AttrValue& OutValue)
	{
		OutKey = R.Str();
		OutValue.Type = static_cast<EOS_EAttributeType>(R.U8());
		switch (OutValue.Type)
		{
		case EOS_EAttributeType::EOS_AT_BOOLEAN: OutValue.Bool = R.U8() ? EOS_TRUE : EOS_FALSE; break;
		case EOS_EAttributeType::EOS_AT_INT64: OutValue.Int = R.I64(); break;
		case EOS_EAttributeType::EOS_AT_DOUBLE: OutValue.Dbl = R.Dbl(); break;
		case EOS_EAttributeType::EOS_AT_STRING: OutValue.Str = R.Str(); break;
		default: break;
		}
		return R.Ok();
	}

	namespace
	{
		// Reduces every value to a double so ordered comparisons work uniformly.
		// Strings are handled separately (only equality/contains make sense).
		double Numeric(const AttrValue& V)
		{
			switch (V.Type)
			{
			case EOS_EAttributeType::EOS_AT_BOOLEAN: return V.Bool ? 1.0 : 0.0;
			case EOS_EAttributeType::EOS_AT_INT64: return static_cast<double>(V.Int);
			case EOS_EAttributeType::EOS_AT_DOUBLE: return V.Dbl;
			default: return 0.0;
			}
		}
	}

	bool CompareAttr(const AttrValue& Stored, EOS_EComparisonOp Op, const AttrValue& Query)
	{
		const bool BothString = (Stored.Type == EOS_EAttributeType::EOS_AT_STRING && Query.Type == EOS_EAttributeType::EOS_AT_STRING);

		switch (Op)
		{
		case EOS_EComparisonOp::EOS_CO_EQUAL:
		case EOS_EComparisonOp::EOS_CO_ANYOF:
		case EOS_EComparisonOp::EOS_CO_ONEOF:
			return BothString ? (Stored.Str == Query.Str) : (Numeric(Stored) == Numeric(Query));
		case EOS_EComparisonOp::EOS_CO_NOTEQUAL:
		case EOS_EComparisonOp::EOS_CO_NOTANYOF:
		case EOS_EComparisonOp::EOS_CO_NOTONEOF:
			return BothString ? (Stored.Str != Query.Str) : (Numeric(Stored) != Numeric(Query));
		case EOS_EComparisonOp::EOS_CO_GREATERTHAN:
			return !BothString && Numeric(Stored) > Numeric(Query);
		case EOS_EComparisonOp::EOS_CO_GREATERTHANOREQUAL:
			return !BothString && Numeric(Stored) >= Numeric(Query);
		case EOS_EComparisonOp::EOS_CO_LESSTHAN:
			return !BothString && Numeric(Stored) < Numeric(Query);
		case EOS_EComparisonOp::EOS_CO_LESSTHANOREQUAL:
			return !BothString && Numeric(Stored) <= Numeric(Query);
		case EOS_EComparisonOp::EOS_CO_DISTANCE:
			// "prefer nearest" -- as a filter it always passes; ordering by
			// distance is not something the samples rely on.
			return true;
		case EOS_EComparisonOp::EOS_CO_CONTAINS:
			return BothString && Stored.Str.find(Query.Str) != std::string::npos;
		default:
			return false;
		}
	}
}
