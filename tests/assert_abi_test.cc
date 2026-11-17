#include <sys/__assert.h>
#include <cstdlib>

extern unsigned assert_calls;

int main()
{
    __ASSERT_NO_MSG(true);
    if (assert_calls != 0U) std::abort();
    __ASSERT_NO_MSG(false);
    if (assert_calls != 1U) std::abort();
}
