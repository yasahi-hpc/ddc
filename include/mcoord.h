#pragma once

#include <cstddef>
#include <utility>

#include "dim.h"
#include "taggedarray.h"

#include <experimental/mdspan>

using MCoordElement = std::size_t;

using MLengthElement = std::ptrdiff_t;

template <class... Tags>
using MCoord = TaggedArray<MCoordElement, Tags...>;

using MCoordX = MCoord<Dim::X>;

using MCoordVx = MCoord<Dim::Vx>;

using MCoordXVx = MCoord<Dim::X, Dim::Vx>;


namespace detail {

template <class, class>
struct ExtentToMCoord;

template <class... Tags, std::size_t... Ints>
struct ExtentToMCoord<MCoord<Tags...>, std::index_sequence<Ints...>>
{
    template <ptrdiff_t... Extents>
    static inline constexpr MCoord<Tags...> mcoord(
            const std::experimental::extents<Extents...>& extent) noexcept
    {
        return MCoord<Tags...>(extent.extent(Ints)...);
    }
};

} // namespace detail

template <class... Tags, class Extents>
inline constexpr MCoord<Tags...> mcoord_end(Extents extent) noexcept
{
    return detail::ExtentToMCoord<MCoord<Tags...>, std::index_sequence_for<Tags...>>::mcoord(
            extent);
}