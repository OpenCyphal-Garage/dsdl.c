/// Test runner for dsdl.c public API test suite
///
/// Uses ThrowTheSwitch Unity framework.
/// This executable tests public API functions only.
/// Internal function tests are in separate executables.

#include "dsdl.h"

#include "unity.h"

#include <stdlib.h>
#include <stdio.h>

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

#ifndef DSDL_TEST_ROOT
#define DSDL_TEST_ROOT "."
#endif

static bool add_namespace_rel(dsdl_t* const dsdl, const char* const rel_path)
{
    if ((dsdl == NULL) || (rel_path == NULL)) {
        return false;
    }
    char path_buf[512];
    const int len = snprintf(path_buf, sizeof(path_buf), "%s/%s", DSDL_TEST_ROOT, rel_path);
    if ((len < 0) || ((size_t)len >= sizeof(path_buf))) {
        return false;
    }
    return dsdl_add_namespace(dsdl, wkv_key(path_buf));
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
    bool result = add_namespace_rel(&dsdl, "test_dsdl_root_namespaces");
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
