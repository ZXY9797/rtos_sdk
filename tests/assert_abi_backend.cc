#include <cstdlib>

unsigned assert_calls = 0U;

// Intentionally a separate translation unit: a C++-mangled caller must fail
// linkage instead of accidentally matching another C++ implementation.
#if defined(CONFIG_ASSERT_NO_FILE_INFO)
extern "C" void assert_post_action(void)
{
    ++assert_calls;
}
#else
extern "C" void assert_post_action(const char* file, unsigned int line)
{
    if (file == nullptr || line == 0U) std::abort();
    ++assert_calls;
}
#endif
