#pragma once

#if !defined(_HAS_CXX20) || !_HAS_CXX20
#	include <experimental/unordered_map>
#	include <experimental/map>

namespace std
{
	template <class _Kty, class _Ty, class _Hasher, class _Keyeq, class _Alloc, class _Pr>
	void erase_if(unordered_map<_Kty, _Ty, _Hasher, _Keyeq, _Alloc>& container, _Pr pred)
	{
		std::experimental::erase_if(container, pred);
	}

    template <class _Kty, class _Ty, class _Hasher, class _Keyeq, class _Alloc, class _Pr>
    void erase_if(unordered_multimap<_Kty, _Ty, _Hasher, _Keyeq, _Alloc>& container, _Pr pred)
	{
		std::experimental::erase_if(container, pred);
	}
}

#else
#	include <unordered_map>
#endif
