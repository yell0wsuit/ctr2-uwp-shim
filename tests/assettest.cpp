#include "host/assets.h"
#include <cstdio>
#include <fstream>
#include <string>

static int failures;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            printf("FAIL %d: %s\n", __LINE__, #x);                                                                     \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)

static std::string Read(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

int main() {
    const auto root = std::filesystem::current_path() /
                      (L"asset-test-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    const auto maps = root / L"Assets" / L"assets" / L"maps";
    CHECK(SUCCEEDED(ctr2::RepairAssetNames(root))); // No game assets beside most test executables.
    CHECK(!std::filesystem::exists(root));
    std::filesystem::create_directories(maps);
    const auto encoded = maps / L"%21_MasterGameDesignDocument.xml";
    const auto decoded = maps / L"!_MasterGameDesignDocument.xml";
    const auto controller = maps / L"%21_MapSelectController.xml";
    std::ofstream(encoded) << "design";
    std::ofstream(controller) << "controller";
    std::ofstream(maps / L"other%21.xml") << "unrelated";

    // Repairs use the supplied game path, independent of the working directory.
    CHECK(SUCCEEDED(ctr2::RepairAssetNames(root)));
    CHECK(!std::filesystem::exists(encoded));
    CHECK(!std::filesystem::exists(controller));
    CHECK(Read(decoded) == "design");
    CHECK(Read(maps / L"!_MapSelectController.xml") == "controller");
    CHECK(Read(maps / L"other%21.xml") == "unrelated");
    CHECK(SUCCEEDED(ctr2::RepairAssetNames(root))); // Repeated launches are harmless.

    std::ofstream(encoded) << "replacement";
    CHECK(SUCCEEDED(ctr2::RepairAssetNames(root)));
    CHECK(Read(decoded) == "design"); // Preserve a pre-existing decoded file.
    CHECK(Read(encoded) == "replacement");

    std::filesystem::remove(decoded);
    HANDLE locked = CreateFileW(encoded.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    CHECK(locked != INVALID_HANDLE_VALUE);
    CHECK(FAILED(ctr2::RepairAssetNames(root))); // Report a blocked repair before game initialization.
    CHECK(Read(encoded) == "replacement");
    CHECK(!std::filesystem::exists(decoded));
    if (locked != INVALID_HANDLE_VALUE) CloseHandle(locked);
    CHECK(SUCCEEDED(ctr2::RepairAssetNames(root)));
    CHECK(Read(decoded) == "replacement");
    std::filesystem::remove_all(root);
    printf("assettest: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
