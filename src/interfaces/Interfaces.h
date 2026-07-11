#pragma once

//
// Internal interface objects.
//
// Each EOS_H<Interface> handle points directly at one of these. They hold the
// per-interface state and a back-reference to the owning Platform so an entry
// point can reach shared services (identity, dispatcher, transport) without a
// separate lookup. Entry points recover the object with As<T>(Handle).
//
// Interface bodies live in interfaces/<Name>.cpp; this header declares just
// enough for Platform to own them and for the cast helpers to work.
//

#include "eos_sdk.h"

namespace EOSEmu
{
	class Platform;

	/// Common base so every interface carries its Platform back-pointer.
	class InterfaceBase
	{
	public:
		explicit InterfaceBase(Platform& Owner) : Platform_(Owner) {}
		Platform& Owner() { return Platform_; }

	protected:
		Platform& Platform_;
	};

	/// Recovers an interface object from its opaque handle. The handle is the
	/// object address, so this is a checked reinterpret_cast.
	template <typename T, typename Handle>
	T* As(Handle H)
	{
		return reinterpret_cast<T*>(H);
	}
}
