#if defined(_WIN32)
#define AYTHER_TEST_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define AYTHER_TEST_EXPORT __attribute__((visibility("default")))
#else
#define AYTHER_TEST_EXPORT
#endif

struct retro_system_info {
    const char* library_name;
    const char* library_version;
    const char* valid_extensions;
    bool need_fullpath;
    bool block_extract;
};

extern "C" AYTHER_TEST_EXPORT unsigned retro_api_version(void) noexcept {
    return 1U;
}

extern "C" AYTHER_TEST_EXPORT void retro_get_system_info(
    retro_system_info* const info) noexcept {
    if (info == nullptr) {
        return;
    }

    *info = retro_system_info{
        "AYTHER Synthetic Core",
        "1.0-test",
        "aytest|rom",
        false,
        false,
    };
}
