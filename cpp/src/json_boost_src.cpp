// Boost.JSON's out-of-line definitions, compiled here so the library is used
// header-only (no separate Boost.JSON binary to link). This translation unit
// is added to the build only when the Boost backend is selected.
#if defined(RPM_JSON_BOOST)
#include <boost/json/src.hpp>
#endif
