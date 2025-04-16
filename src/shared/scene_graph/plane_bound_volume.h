#pragma once


#include "math/plane.h"
#include "math/axis_aligned_box.h"

#include <vector>

#include "math/ray.h"

namespace mmo
{
    class PlaneBoundVolume
    {
    public:
        typedef std::vector<Plane> PlaneList;

        /// Publicly accessible plane list, you can modify this direct
        PlaneList planes;
        Plane::Side outside;

        PlaneBoundedVolume() :outside(Plane::NegativeSide) {}
        PlaneBoundedVolume(Plane::Side theOutside)
            : outside(theOutside) {
        }

        /** Intersection test with AABB
        @remarks May return false positives but will never miss an intersection.
        */
        inline bool intersects(const AABB& box) const
        {
            if (box.IsNull()) return false;

            // Get centre of the box
            Vector3 centre = box.GetCenter();

            // Get the half-size of the box
            Vector3 halfSize = box.GetExtents();

            PlaneList::const_iterator i, iend;
            iend = planes.end();
            for (i = planes.begin(); i != iend; ++i)
            {
                const Plane& plane = *i;

                Plane::Side side = plane.getSide(centre, halfSize);
                if (side == outside)
                {
                    // Found a splitting plane therefore return not intersecting
                    return false;
                }
            }

            // couldn't find a splitting plane, assume intersecting
            return true;
        }


    };

}
