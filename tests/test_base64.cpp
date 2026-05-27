#include <gtest/gtest.h>

#include <vector>

#include "core/base64.hpp"

using namespace clusterrun;

namespace {

std::vector<std::uint8_t> bytes(const std::string& s) {
  return std::vector<std::uint8_t>(s.begin(), s.end());
}

}  // namespace

TEST(Base64, KnownVectors) {
  EXPECT_EQ(base64_encode(bytes("")), "");
  EXPECT_EQ(base64_encode(bytes("f")), "Zg==");
  EXPECT_EQ(base64_encode(bytes("fo")), "Zm8=");
  EXPECT_EQ(base64_encode(bytes("foo")), "Zm9v");
  EXPECT_EQ(base64_encode(bytes("foob")), "Zm9vYg==");
  EXPECT_EQ(base64_encode(bytes("foobar")), "Zm9vYmFy");
}

TEST(Base64, RoundTripBinary) {
  std::vector<std::uint8_t> data;
  for (int i = 0; i < 256; ++i) {
    data.push_back(static_cast<std::uint8_t>(i));
  }
  auto enc = base64_encode(data);
  auto dec = base64_decode(enc);
  ASSERT_TRUE(dec.has_value());
  EXPECT_EQ(*dec, data);
}

TEST(Base64, RejectsBadInput) {
  EXPECT_FALSE(base64_decode("Zg=").has_value());       // wrong length
  EXPECT_FALSE(base64_decode("Zm9v!Yg=").has_value());  // illegal char
  EXPECT_FALSE(base64_decode("=Zm9").has_value());      // padding in wrong place
}
