#pragma once

#include <cstdlib>
#include <string>

namespace rtc::testing {

// RAII helper for setting/clearing an environment variable within a test's
// scope and restoring the previous value on destruction. Keeps env-dependent
// tests (e.g. Config) hermetic and order-independent.
class EnvGuard {
public:
    EnvGuard(std::string name, const std::string& value) : name_(std::move(name)) {
        capture_previous();
        set(name_.c_str(), value.c_str());
    }

    // Constructs a guard that unsets the variable for the scope.
    explicit EnvGuard(std::string name) : name_(std::move(name)) {
        capture_previous();
        unset(name_.c_str());
    }

    ~EnvGuard() {
        if (had_previous_) {
            set(name_.c_str(), previous_.c_str());
        } else {
            unset(name_.c_str());
        }
    }

    EnvGuard(const EnvGuard&) = delete;
    EnvGuard& operator=(const EnvGuard&) = delete;

private:
    void capture_previous() {
#if defined(_MSC_VER)
        char* buffer = nullptr;
        std::size_t size = 0;
        if (_dupenv_s(&buffer, &size, name_.c_str()) == 0 && buffer != nullptr) {
            had_previous_ = true;
            previous_ = buffer;
            std::free(buffer);
        }
#else
        if (const char* existing = std::getenv(name_.c_str())) {
            had_previous_ = true;
            previous_ = existing;
        }
#endif
    }

    static void set(const char* name, const char* value) {
#if defined(_MSC_VER)
        _putenv_s(name, value);
#else
        ::setenv(name, value, /*overwrite=*/1);
#endif
    }

    static void unset(const char* name) {
#if defined(_MSC_VER)
        _putenv_s(name, "");
#else
        ::unsetenv(name);
#endif
    }

    std::string name_;
    std::string previous_;
    bool had_previous_ = false;
};

}  // namespace rtc::testing
