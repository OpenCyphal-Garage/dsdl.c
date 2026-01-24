/// Test runner for dsdl.c public API test suite
///
/// Uses ThrowTheSwitch Unity framework.
/// This executable tests public API functions only.
/// Internal function tests are in separate executables.

#include "dsdl.h"

#include "unity.h"

#include <stdlib.h>

// ============================================================================
// Test helpers
// ============================================================================

static void* test_realloc(dsdl_t* self, void* ptr, size_t new_size)
{
    (void)self;
    if (new_size == 0) {
        free(ptr);
        return NULL;
    }
    return realloc(ptr, new_size);
}

// ============================================================================
// Public API tests
// ============================================================================

void test_dsdl_new_destroy(void)
{
    dsdl_t dsdl;
    dsdl_new(&dsdl, test_realloc);
    // Verify initialization
    TEST_ASSERT_NOT_NULL(dsdl.realloc);
    dsdl_destroy(&dsdl);
    TEST_PASS();
}

void test_dsdl_add_namespace(void)
{
    dsdl_t dsdl;
    dsdl_new(&dsdl, test_realloc);

    // Add a namespace
    bool result = dsdl_add_namespace(&dsdl, wkv_key("test_dsdl_root_namespaces"));
    TEST_ASSERT_TRUE(result);

    dsdl_destroy(&dsdl);
}

// ============================================================================
// Main
// ============================================================================

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

    // Public API tests
    RUN_TEST(test_dsdl_new_destroy);
    RUN_TEST(test_dsdl_add_namespace);

    return UNITY_END();
}
