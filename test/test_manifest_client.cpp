#include <gtest/gtest.h>

#include "upgrade/manifest_client.hpp"

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
