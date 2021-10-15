#pragma once

namespace model
{

    namespace memory
    {

        /**
         * The type for identifiers, which is the same as the id type used in the
         * libosmium library.
         * OpenStreetMap contains more than 2 billion nodes (as of 2021), which
         * is why the long datatype is needed. While OSM object ids are always
         * positive, in some cases, ids need to be marked as invalid, which is why
         * object_id_type is signed.
         * 
         * For more information, refer to https://osmcode.org/libosmium/manual.html
         */
        typedef signed long object_id_type;

    }

}