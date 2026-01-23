/// Test runner for dsdl.c public API test suite
///
/// Uses ThrowTheSwitch Unity framework.
/// This executable tests public API functions only.
/// Internal function tests are in separate executables.

#include "unity.h"

// External test function declarations
// test_serialization.c
void test_serialization_placeholder(void);

void setUp(void)
{
    // Called before each test
}

void tearDown(void)
{
    // Called after each test
}

int main(void)
{
    UNITY_BEGIN();

    // Serialization tests (public API)
    RUN_TEST(test_serialization_placeholder);

    return UNITY_END();
}
