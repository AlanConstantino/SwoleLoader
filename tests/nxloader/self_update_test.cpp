#include "nxloader/self_update.hpp"

#include <cstdio>
#include <cstdlib>

using namespace nxloader::self_update;

namespace {

int failures{};

void Expect(bool condition, const char* text, int line) {
    if (!condition) {
        std::fprintf(stderr, "line %d: %s\n", line, text);
        ++failures;
    }
}

#define EXPECT(x) Expect((x), #x, __LINE__)

constexpr std::uint32_t V(std::uint32_t major, std::uint32_t minor, std::uint32_t micro) {
    return major << 16 | minor << 8 | micro;
}

ReleaseAsset Asset(std::string name, std::uint64_t size = 100) {
    return {name, "https://github.com/owner/repo/releases/download/v1.0.0/" + name, size};
}

Release MakeRelease(std::string tag, std::vector<ReleaseAsset> assets) {
    return {std::move(tag), "notes", std::move(assets)};
}

void TestParseVersion() {
    EXPECT(ParseVersion("0.1.6") == V(0, 1, 6));
    EXPECT(ParseVersion("v0.1.6") == V(0, 1, 6));
    EXPECT(ParseVersion("V1.2.3") == V(1, 2, 3));
    EXPECT(ParseVersion("1.2") == V(1, 2, 0));
    EXPECT(ParseVersion("7") == V(7, 0, 0));
    EXPECT(ParseVersion("255.255.255") == V(255, 255, 255));
    EXPECT(ParseVersion("010.0.0") == V(10, 0, 0));

    for (const char* bad : {"", "v", "vv1.0.0", "1.", "1..2", ".1.2", "1.2.3.", "1.2.3.4",
            "256.0.0", "1.256.0", "99999999999.0.0", "1.2.3-beta", "1.2.3 ", " 1.2.3", "1.x.3", "latest"}) {
        Expect(!ParseVersion(bad).has_value(), bad, __LINE__);
    }
}

void TestUpToDate() {
    const auto same = Evaluate(MakeRelease("v0.1.6", {}), V(0, 1, 6), "app.nro");
    EXPECT(same.status == Status::UpToDate);
    EXPECT(same.version == V(0, 1, 6));

    const auto older = Evaluate(MakeRelease("v0.1.5", {Asset("app.nro"), Asset("app.nro.sig")}), V(0, 1, 6), "app.nro");
    EXPECT(older.status == Status::UpToDate);

    // Ordering compares each part numerically, not as text.
    EXPECT(Evaluate(MakeRelease("0.1.10", {Asset("app.nro"), Asset("app.nro.sig")}), V(0, 1, 9), "app.nro").status == Status::Available);
    EXPECT(Evaluate(MakeRelease("0.2.0", {Asset("app.nro"), Asset("app.nro.sig")}), V(0, 1, 255), "app.nro").status == Status::Available);
    EXPECT(Evaluate(MakeRelease("1.0.0", {Asset("app.nro"), Asset("app.nro.sig")}), V(0, 255, 255), "app.nro").status == Status::Available);
}

void TestInvalidTag() {
    const auto check = Evaluate(MakeRelease("nightly", {Asset("app.nro"), Asset("app.nro.sig")}), V(0, 1, 6), "app.nro");
    EXPECT(check.status == Status::Invalid);
    EXPECT(!check.error.empty());
}

void TestAvailable() {
    const auto check = Evaluate(MakeRelease("v0.1.7", {
        Asset("app-0.1.7.zip"), Asset("app.nro", 4638526), Asset("app.nro.sig", 72)}), V(0, 1, 6), "app.nro");
    EXPECT(check.status == Status::Available);
    EXPECT(check.version == V(0, 1, 7));
    EXPECT(check.app.name == "app.nro");
    EXPECT(check.app.size == 4638526);
    EXPECT(check.signature.name == "app.nro.sig");
    EXPECT(check.error.empty());
}

void TestNameMatching() {
    // Case differences are ignored.
    auto check = Evaluate(MakeRelease("v2.0.0", {Asset("App.NRO"), Asset("app.nro.SIG")}), V(1, 0, 0), "app.nro");
    EXPECT(check.status == Status::Available);
    EXPECT(check.app.name == "App.NRO");

    // A renamed .nro still updates installs that expect the old name.
    check = Evaluate(MakeRelease("v2.0.0", {Asset("new-name.nro"), Asset("new-name.nro.sig"), Asset("new-name.zip")}), V(1, 0, 0), "app.nro");
    EXPECT(check.status == Status::Available);
    EXPECT(check.app.name == "new-name.nro");
    EXPECT(check.signature.name == "new-name.nro.sig");

    // With several .nro files the expected name decides.
    check = Evaluate(MakeRelease("v2.0.0", {Asset("debug.nro"), Asset("app.nro"), Asset("app.nro.sig"), Asset("debug.nro.sig")}), V(1, 0, 0), "app.nro");
    EXPECT(check.status == Status::Available);
    EXPECT(check.app.name == "app.nro");
    EXPECT(check.signature.name == "app.nro.sig");

    // Several .nro files and none with the expected name is ambiguous.
    check = Evaluate(MakeRelease("v2.0.0", {Asset("a.nro"), Asset("b.nro"), Asset("a.nro.sig")}), V(1, 0, 0), "app.nro");
    EXPECT(check.status == Status::Invalid);

    // The signature must belong to the chosen file.
    check = Evaluate(MakeRelease("v2.0.0", {Asset("app.nro"), Asset("other.nro.sig"), Asset("app.sig"), Asset("app.nro.sig.txt")}), V(1, 0, 0), "app.nro");
    EXPECT(check.status == Status::Invalid);
}

void TestMissingFiles() {
    auto check = Evaluate(MakeRelease("v2.0.0", {Asset("app.zip"), Asset("app.zip.sig")}), V(1, 0, 0), "app.nro");
    EXPECT(check.status == Status::Invalid);
    EXPECT(!check.error.empty());

    check = Evaluate(MakeRelease("v2.0.0", {Asset("app.nro")}), V(1, 0, 0), "app.nro");
    EXPECT(check.status == Status::Invalid);
    EXPECT(check.error == "The latest release is not signed.");

    check = Evaluate(MakeRelease("v2.0.0", {}), V(1, 0, 0), "app.nro");
    EXPECT(check.status == Status::Invalid);
}

void TestSizesAndLinks() {
    EXPECT(Evaluate(MakeRelease("v2.0.0", {Asset("app.nro", 0), Asset("app.nro.sig")}), V(1, 0, 0), "app.nro").status == Status::Invalid);
    EXPECT(Evaluate(MakeRelease("v2.0.0", {Asset("app.nro", MaxAppBytes + 1), Asset("app.nro.sig")}), V(1, 0, 0), "app.nro").status == Status::Invalid);
    EXPECT(Evaluate(MakeRelease("v2.0.0", {Asset("app.nro", MaxAppBytes), Asset("app.nro.sig", MaxSignatureBytes)}), V(1, 0, 0), "app.nro").status == Status::Available);
    EXPECT(Evaluate(MakeRelease("v2.0.0", {Asset("app.nro"), Asset("app.nro.sig", 0)}), V(1, 0, 0), "app.nro").status == Status::Invalid);
    EXPECT(Evaluate(MakeRelease("v2.0.0", {Asset("app.nro"), Asset("app.nro.sig", MaxSignatureBytes + 1)}), V(1, 0, 0), "app.nro").status == Status::Invalid);

    auto release = MakeRelease("v2.0.0", {Asset("app.nro"), Asset("app.nro.sig")});
    release.assets[0].url = "http://github.com/owner/repo/releases/download/v2.0.0/app.nro";
    EXPECT(Evaluate(release, V(1, 0, 0), "app.nro").status == Status::Invalid);
    release.assets[0].url.clear();
    EXPECT(Evaluate(release, V(1, 0, 0), "app.nro").status == Status::Invalid);
}

void TestLatestReleaseUrl() {
    EXPECT(LatestReleaseUrl("owner/repo") == "https://api.github.com/repos/owner/repo/releases/latest");
}

} // namespace

int main() {
    TestParseVersion();
    TestUpToDate();
    TestInvalidTag();
    TestAvailable();
    TestNameMatching();
    TestMissingFiles();
    TestSizesAndLinks();
    TestLatestReleaseUrl();

    if (failures) {
        std::fprintf(stderr, "%d self-update check(s) failed.\n", failures);
        return EXIT_FAILURE;
    }
    std::puts("Self-update tests passed: versions, release files, renames, signatures required, sizes and links.");
}
