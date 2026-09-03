#if defined(_WIN32)
#define AYTHER_TEST_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define AYTHER_TEST_EXPORT __attribute__((visibility("default")))
#else
#define AYTHER_TEST_EXPORT
#endif

extern "C" AYTHER_TEST_EXPORT unsigned ayther_test_library_marker(void) noexcept {
    return 0x41595448U;
}
