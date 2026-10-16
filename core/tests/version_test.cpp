#include "facetry/core/version.hpp"

#include <doctest.h>

TEST_CASE("version matches the project version") {
    CHECK(facetry::core::version() == FACETRY_EXPECTED_VERSION);
}

TEST_CASE("platform matches the build target") {
    CHECK(facetry::core::platform() == FACETRY_EXPECTED_PLATFORM);
}
