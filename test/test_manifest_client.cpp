#include <gtest/gtest.h>

#include "upgrade/manifest_client.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <random>

namespace fs = std::filesystem;

namespace {

class TempDir {
public:
    TempDir() {
        static thread_local std::mt19937_64 rng(std::random_device{}());
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = fs::temp_directory_path() /
                ("bto-manifest-" + std::to_string(stamp) + "-" + std::to_string(rng()));
        fs::create_directories(path_);
    }

    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }

    const fs::path& path() const { return path_; }

private:
    fs::path path_;
};

class EnvVarGuard {
public:
    explicit EnvVarGuard(const char* key)
        : key_(key) {
        const char* value = std::getenv(key_);
        if (value) {
            had_original_ = true;
            original_ = value;
        }
    }

    ~EnvVarGuard() {
        if (had_original_) {
            setenv(key_, original_.c_str(), 1);
        } else {
            unsetenv(key_);
        }
    }

private:
    const char* key_;
    bool had_original_ = false;
    std::string original_;
};

}  // namespace

TEST(ManifestClient, FetchesManifestFromFileUrl) {
    TempDir temp;
    const auto bin_dir = temp.path() / "bin";
    fs::create_directories(bin_dir);

    std::ofstream(bin_dir / "bto") << "artifact";
    std::ofstream(temp.path() / "manifest.json")
        << R"({"version":"v1","git_commit":"deadbeef","build_time":"now","artifacts":[{"name":"bto","size":8,"sha256":"1234"}]})";

    bto::upgrade::ManifestClient client(
        "file://" + (temp.path() / "manifest.json").string(),
        "file://" + bin_dir.string());

    auto manifest = client.fetch_manifest();
    ASSERT_TRUE(manifest.has_value());
    EXPECT_EQ(manifest->version, "v1");
    ASSERT_EQ(manifest->artifacts.size(), 1u);
    EXPECT_EQ(manifest->artifacts.front().name, "bto");
}

TEST(ManifestClient, DownloadsArtifactFromFileUrl) {
    TempDir temp;
    const auto bin_dir = temp.path() / "bin";
    fs::create_directories(bin_dir);

    std::ofstream(bin_dir / "p2p-tunnel-server") << "payload";

    bto::upgrade::ManifestClient client(
        "file://" + (temp.path() / "manifest.json").string(),
        "file://" + bin_dir.string());

    bto::upgrade::ArtifactManifestEntry entry;
    entry.name = "p2p-tunnel-server";
    entry.size = 7;
    entry.sha256 = "abcd";

    const auto output = temp.path() / "download" / "p2p-tunnel-server";
    std::string error;
    ASSERT_TRUE(client.download_artifact(entry, output, &error)) << error;
    EXPECT_TRUE(fs::exists(output));
}

TEST(ManifestClient, RejectsInvalidArtifactName) {
    TempDir temp;
    bto::upgrade::ManifestClient client(
        "file://" + (temp.path() / "manifest.json").string(),
        "file://" + temp.path().string());

    bto::upgrade::ArtifactManifestEntry entry;
    entry.name = "../bad";

    std::string error;
    EXPECT_FALSE(client.download_artifact(entry, temp.path() / "out" / "bad", &error));
    EXPECT_NE(error.find("invalid artifact name"), std::string::npos);
}

TEST(ManifestClient, RejectsCustomInsecureHttpWithoutOverride) {
    EnvVarGuard allow_http("BTO_ALLOW_INSECURE_HTTP_UPGRADE");
    unsetenv("BTO_ALLOW_INSECURE_HTTP_UPGRADE");

    bto::upgrade::ManifestClient client(
        "http://example.com/api/binaries/manifest",
        "http://example.com/api/binaries");

    std::string error;
    EXPECT_FALSE(client.fetch_manifest(&error).has_value());
    EXPECT_NE(error.find("refusing insecure HTTP download"), std::string::npos);
}

TEST(ManifestClient, RejectsUnsupportedSchemeWithoutOverride) {
    bto::upgrade::ManifestClient client(
        "ftp://example.com/api/binaries/manifest",
        "ftp://example.com/api/binaries");

    std::string error;
    EXPECT_FALSE(client.fetch_manifest(&error).has_value());
    EXPECT_NE(error.find("unsupported manifest/artifact URL scheme"), std::string::npos);
}

TEST(ManifestClient, DefaultUrlsAreHttps) {
    EXPECT_EQ(
        bto::upgrade::default_manifest_url(),
        "https://downloads.bto.asia/api/binaries/manifest");
    EXPECT_EQ(
        bto::upgrade::default_binaries_base_url(),
        "https://downloads.bto.asia/api/binaries");
}
