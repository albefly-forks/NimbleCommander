// Copyright (C) 2022 Michael Kazakov. Subject to GNU General Public License version 3.
#include "Tests.h"
#include "TestEnv.h"
#include <VFS/VFS.h>
#include <VFS/ArcLA.h>
#include <VFS/VFSGenericMemReadOnlyFile.h>
#include <Habanero/WriteAtomically.h>

using namespace nc::vfs;

#define PREFIX "VFSArchive "

TEST_CASE(PREFIX "Can unzip an archive with Chinese symbols")
{
    // These two archives have a single file named "中文测试", which is 4byte long: "123\x0A"

    // This one was compressed via Keka 1.2.55
    static const unsigned char __chineese_name_zip_a[] = {
        0x50, 0x4b, 0x03, 0x04, 0x14, 0x00, 0x08, 0x00, 0x08, 0x00, 0x50, 0x04, 0xf8, 0x54, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x20, 0x00, 0xe4, 0xb8, 0xad, 0xe6, 0x96, 0x87,
        0xe6, 0xb5, 0x8b, 0xe8, 0xaf, 0x95, 0x55, 0x54, 0x0d, 0x00, 0x07, 0x18, 0x23, 0xdc, 0x62, 0x19, 0x23, 0xdc,
        0x62, 0x18, 0x23, 0xdc, 0x62, 0x75, 0x78, 0x0b, 0x00, 0x01, 0x04, 0xf5, 0x01, 0x00, 0x00, 0x04, 0x14, 0x00,
        0x00, 0x00, 0x33, 0x34, 0x32, 0xe6, 0x02, 0x00, 0x50, 0x4b, 0x07, 0x08, 0x08, 0xfd, 0x82, 0x5a, 0x06, 0x00,
        0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x50, 0x4b, 0x01, 0x02, 0x14, 0x03, 0x14, 0x00, 0x08, 0x00, 0x08, 0x00,
        0x50, 0x04, 0xf8, 0x54, 0x08, 0xfd, 0x82, 0x5a, 0x06, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x0c, 0x00,
        0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xa4, 0x81, 0x00, 0x00, 0x00, 0x00, 0xe4, 0xb8,
        0xad, 0xe6, 0x96, 0x87, 0xe6, 0xb5, 0x8b, 0xe8, 0xaf, 0x95, 0x55, 0x54, 0x0d, 0x00, 0x07, 0x18, 0x23, 0xdc,
        0x62, 0x19, 0x23, 0xdc, 0x62, 0x18, 0x23, 0xdc, 0x62, 0x75, 0x78, 0x0b, 0x00, 0x01, 0x04, 0xf5, 0x01, 0x00,
        0x00, 0x04, 0x14, 0x00, 0x00, 0x00, 0x50, 0x4b, 0x05, 0x06, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00,
        0x5a, 0x00, 0x00, 0x00, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00};
    static const unsigned int __chineese_name_zip_a_len = 208;

    // This one was compressed via NC v1.4(?)
    static const unsigned char __chineese_name_zip_b[] = {
        0x50, 0x4b, 0x03, 0x04, 0x0a, 0x00, 0x00, 0x08, 0x00, 0x00, 0x51, 0x04, 0xf8, 0x54, 0x08, 0xfd, 0x82, 0x5a,
        0x04, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00, 0xe4, 0xb8, 0xad, 0xe6, 0x96, 0x87,
        0xe6, 0xb5, 0x8b, 0xe8, 0xaf, 0x95, 0x31, 0x32, 0x33, 0x0a, 0x50, 0x4b, 0x01, 0x02, 0x3f, 0x03, 0x0a, 0x00,
        0x00, 0x08, 0x00, 0x00, 0x51, 0x04, 0xf8, 0x54, 0x08, 0xfd, 0x82, 0x5a, 0x04, 0x00, 0x00, 0x00, 0x04, 0x00,
        0x00, 0x00, 0x0c, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x80, 0xa4, 0x81, 0x00, 0x00,
        0x00, 0x00, 0xe4, 0xb8, 0xad, 0xe6, 0x96, 0x87, 0xe6, 0xb5, 0x8b, 0xe8, 0xaf, 0x95, 0x0a, 0x00, 0x20, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x18, 0x00, 0x29, 0xf5, 0x28, 0x16, 0xb2, 0x9e, 0xd8, 0x01, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x50, 0x4b, 0x05, 0x06,
        0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x5e, 0x00, 0x00, 0x00, 0x2e, 0x00, 0x00, 0x00, 0x00, 0x00};
    static const unsigned int __chineese_name_zip_b_len = 162;

    // This one is compressed via MacOS 13.0.1
    static const unsigned char __3_zip[] = {
        0x50, 0x4b, 0x03, 0x04, 0x14, 0x00, 0x08, 0x00, 0x08, 0x00, 0x51, 0x04, 0xf8, 0x54, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x20, 0x00, 0xe4, 0xb8, 0xad, 0xe6, 0x96, 0x87,
        0xe6, 0xb5, 0x8b, 0xe8, 0xaf, 0x95, 0x55, 0x54, 0x0d, 0x00, 0x07, 0x8a, 0x85, 0xdc, 0x62, 0xe4, 0xd1, 0x99,
        0x63, 0xdf, 0xd1, 0x99, 0x63, 0x75, 0x78, 0x0b, 0x00, 0x01, 0x04, 0xf5, 0x01, 0x00, 0x00, 0x04, 0x14, 0x00,
        0x00, 0x00, 0x33, 0x34, 0x32, 0xe6, 0x02, 0x00, 0x50, 0x4b, 0x07, 0x08, 0x08, 0xfd, 0x82, 0x5a, 0x06, 0x00,
        0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x50, 0x4b, 0x01, 0x02, 0x14, 0x03, 0x14, 0x00, 0x08, 0x00, 0x08, 0x00,
        0x51, 0x04, 0xf8, 0x54, 0x08, 0xfd, 0x82, 0x5a, 0x06, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x0c, 0x00,
        0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xa4, 0x81, 0x00, 0x00, 0x00, 0x00, 0xe4, 0xb8,
        0xad, 0xe6, 0x96, 0x87, 0xe6, 0xb5, 0x8b, 0xe8, 0xaf, 0x95, 0x55, 0x54, 0x0d, 0x00, 0x07, 0x8a, 0x85, 0xdc,
        0x62, 0xe4, 0xd1, 0x99, 0x63, 0xdf, 0xd1, 0x99, 0x63, 0x75, 0x78, 0x0b, 0x00, 0x01, 0x04, 0xf5, 0x01, 0x00,
        0x00, 0x04, 0x14, 0x00, 0x00, 0x00, 0x50, 0x4b, 0x05, 0x06, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00,
        0x5a, 0x00, 0x00, 0x00, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00};
    static const unsigned int __3_zip_len = 208;

    auto check = [](std::span<const std::byte> _bytes) {
        TestDir dir;
        const auto path = std::filesystem::path(dir.directory) / "tmp.zip";
        REQUIRE(nc::base::WriteAtomically(path, _bytes));

        std::shared_ptr<ArchiveHost> host;
        REQUIRE_NOTHROW(host = std::make_shared<ArchiveHost>(path.c_str(), TestEnv().vfs_native));

        CHECK(host->StatTotalFiles() == 1);
        CHECK(host->StatTotalDirs() == 0);
        CHECK(host->StatTotalRegs() == 1);

        VFSFilePtr file;
        REQUIRE(host->CreateFile(reinterpret_cast<const char *>(u8"/中文测试"), file, 0) == 0);
        REQUIRE(file->Open(VFSFlags::OF_Read) == 0);

        auto bytes = file->ReadFile();
        REQUIRE(bytes);
        REQUIRE(bytes->size() == 4);
        CHECK(std::memcmp(bytes->data(), "123\x0A", 4) == 0);
    };
    check({reinterpret_cast<const std::byte *>(__chineese_name_zip_a), __chineese_name_zip_a_len});
    check({reinterpret_cast<const std::byte *>(__chineese_name_zip_b), __chineese_name_zip_b_len});
    check({reinterpret_cast<const std::byte *>(__3_zip), __3_zip_len});
}

TEST_CASE(PREFIX "Can unzip an archive with Cyrillic symbols")
{
    // These two archives have a single file named "Привет, Мир!.txt", which is byte long: "123"

    // This one is compressed via MacOS 13.0.1
    static const unsigned char __1_zip[] = {
        0x50, 0x4b, 0x03, 0x04, 0x14, 0x00, 0x08, 0x00, 0x08, 0x00, 0xb3, 0x69, 0x8e, 0x55, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x19, 0x00, 0x20, 0x00, 0xd0, 0x9f, 0xd1, 0x80, 0xd0, 0xb8,
        0xd0, 0xb2, 0xd0, 0xb5, 0xd1, 0x82, 0x2c, 0x20, 0xd0, 0x9c, 0xd0, 0xb8, 0xd1, 0x80, 0x21, 0x2e, 0x74, 0x78,
        0x74, 0x55, 0x54, 0x0d, 0x00, 0x07, 0x02, 0xcc, 0x99, 0x63, 0x04, 0xcc, 0x99, 0x63, 0x02, 0xcc, 0x99, 0x63,
        0x75, 0x78, 0x0b, 0x00, 0x01, 0x04, 0xf5, 0x01, 0x00, 0x00, 0x04, 0x14, 0x00, 0x00, 0x00, 0x33, 0x34, 0x32,
        0x06, 0x00, 0x50, 0x4b, 0x07, 0x08, 0xd2, 0x63, 0x48, 0x88, 0x05, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
        0x50, 0x4b, 0x03, 0x04, 0x14, 0x00, 0x08, 0x00, 0x08, 0x00, 0xb3, 0x69, 0x8e, 0x55, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0xe3, 0x00, 0x00, 0x00, 0x24, 0x00, 0x20, 0x00, 0x5f, 0x5f, 0x4d, 0x41, 0x43, 0x4f,
        0x53, 0x58, 0x2f, 0x2e, 0x5f, 0xd0, 0x9f, 0xd1, 0x80, 0xd0, 0xb8, 0xd0, 0xb2, 0xd0, 0xb5, 0xd1, 0x82, 0x2c,
        0x20, 0xd0, 0x9c, 0xd0, 0xb8, 0xd1, 0x80, 0x21, 0x2e, 0x74, 0x78, 0x74, 0x55, 0x54, 0x0d, 0x00, 0x07, 0x02,
        0xcc, 0x99, 0x63, 0x04, 0xcc, 0x99, 0x63, 0x11, 0xcc, 0x99, 0x63, 0x75, 0x78, 0x0b, 0x00, 0x01, 0x04, 0xf5,
        0x01, 0x00, 0x00, 0x04, 0x14, 0x00, 0x00, 0x00, 0x63, 0x60, 0x15, 0x63, 0x67, 0x60, 0x62, 0x60, 0xf0, 0x4d,
        0x4c, 0x56, 0xf0, 0x0f, 0x56, 0x88, 0x50, 0x80, 0x02, 0x90, 0x18, 0x03, 0x27, 0x10, 0x1b, 0x01, 0xf1, 0x46,
        0x20, 0x06, 0xf1, 0x1f, 0x33, 0x10, 0x05, 0x1c, 0x43, 0x42, 0x82, 0xa0, 0x4c, 0x90, 0x8e, 0x23, 0x40, 0x2c,
        0x8f, 0xa6, 0x84, 0x09, 0x2a, 0xce, 0xcf, 0xc0, 0x20, 0x9e, 0x9c, 0x9f, 0xab, 0x97, 0x58, 0x50, 0x90, 0x93,
        0xaa, 0x17, 0x92, 0x5a, 0x51, 0xe2, 0x9a, 0x97, 0x9c, 0x9f, 0x92, 0x99, 0x97, 0x0e, 0x51, 0x77, 0x19, 0x88,
        0x05, 0x18, 0x18, 0xa4, 0x10, 0x6a, 0x72, 0x12, 0x8b, 0x4b, 0x4a, 0x8b, 0x53, 0x53, 0x52, 0x12, 0x4b, 0x52,
        0x95, 0x03, 0x82, 0x41, 0x8a, 0x4a, 0x4b, 0xd2, 0x74, 0x2d, 0xac, 0x0d, 0x8d, 0x4d, 0x8c, 0x0c, 0xcd, 0x2d,
        0x2d, 0x4c, 0x18, 0xce, 0xcc, 0x4c, 0x06, 0x09, 0x3f, 0x73, 0x11, 0x31, 0x04, 0xd1, 0x00, 0x50, 0x4b, 0x07,
        0x08, 0x99, 0x83, 0x0e, 0xe6, 0x85, 0x00, 0x00, 0x00, 0xe3, 0x00, 0x00, 0x00, 0x50, 0x4b, 0x01, 0x02, 0x14,
        0x03, 0x14, 0x00, 0x08, 0x00, 0x08, 0x00, 0xb3, 0x69, 0x8e, 0x55, 0xd2, 0x63, 0x48, 0x88, 0x05, 0x00, 0x00,
        0x00, 0x03, 0x00, 0x00, 0x00, 0x19, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xa4,
        0x81, 0x00, 0x00, 0x00, 0x00, 0xd0, 0x9f, 0xd1, 0x80, 0xd0, 0xb8, 0xd0, 0xb2, 0xd0, 0xb5, 0xd1, 0x82, 0x2c,
        0x20, 0xd0, 0x9c, 0xd0, 0xb8, 0xd1, 0x80, 0x21, 0x2e, 0x74, 0x78, 0x74, 0x55, 0x54, 0x0d, 0x00, 0x07, 0x02,
        0xcc, 0x99, 0x63, 0x04, 0xcc, 0x99, 0x63, 0x02, 0xcc, 0x99, 0x63, 0x75, 0x78, 0x0b, 0x00, 0x01, 0x04, 0xf5,
        0x01, 0x00, 0x00, 0x04, 0x14, 0x00, 0x00, 0x00, 0x50, 0x4b, 0x01, 0x02, 0x14, 0x03, 0x14, 0x00, 0x08, 0x00,
        0x08, 0x00, 0xb3, 0x69, 0x8e, 0x55, 0x99, 0x83, 0x0e, 0xe6, 0x85, 0x00, 0x00, 0x00, 0xe3, 0x00, 0x00, 0x00,
        0x24, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xa4, 0x81, 0x6c, 0x00, 0x00, 0x00,
        0x5f, 0x5f, 0x4d, 0x41, 0x43, 0x4f, 0x53, 0x58, 0x2f, 0x2e, 0x5f, 0xd0, 0x9f, 0xd1, 0x80, 0xd0, 0xb8, 0xd0,
        0xb2, 0xd0, 0xb5, 0xd1, 0x82, 0x2c, 0x20, 0xd0, 0x9c, 0xd0, 0xb8, 0xd1, 0x80, 0x21, 0x2e, 0x74, 0x78, 0x74,
        0x55, 0x54, 0x0d, 0x00, 0x07, 0x02, 0xcc, 0x99, 0x63, 0x04, 0xcc, 0x99, 0x63, 0x11, 0xcc, 0x99, 0x63, 0x75,
        0x78, 0x0b, 0x00, 0x01, 0x04, 0xf5, 0x01, 0x00, 0x00, 0x04, 0x14, 0x00, 0x00, 0x00, 0x50, 0x4b, 0x05, 0x06,
        0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x02, 0x00, 0xd9, 0x00, 0x00, 0x00, 0x63, 0x01, 0x00, 0x00, 0x00, 0x00};
    static const unsigned int __1_zip_len = 594;

    // This one is compressed via NC 1.4
    const unsigned char __2_zip[] = {
        0x50, 0x4b, 0x03, 0x04, 0x14, 0x00, 0x08, 0x08, 0x08, 0x00, 0xb3, 0x69, 0x8e, 0x55, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x19, 0x00, 0x20, 0x00, 0xd0, 0x9f, 0xd1, 0x80, 0xd0, 0xb8,
        0xd0, 0xb2, 0xd0, 0xb5, 0xd1, 0x82, 0x2c, 0x20, 0xd0, 0x9c, 0xd0, 0xb8, 0xd1, 0x80, 0x21, 0x2e, 0x74, 0x78,
        0x74, 0x55, 0x54, 0x0d, 0x00, 0x07, 0x02, 0xcc, 0x99, 0x63, 0x04, 0xcc, 0x99, 0x63, 0x02, 0xcc, 0x99, 0x63,
        0x75, 0x78, 0x0b, 0x00, 0x01, 0x04, 0xf5, 0x01, 0x00, 0x00, 0x04, 0x14, 0x00, 0x00, 0x00, 0x33, 0x34, 0x32,
        0x06, 0x00, 0x50, 0x4b, 0x07, 0x08, 0xd2, 0x63, 0x48, 0x88, 0x05, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
        0x50, 0x4b, 0x03, 0x04, 0x14, 0x00, 0x08, 0x08, 0x08, 0x00, 0x00, 0x00, 0x21, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0xe4, 0x00, 0x00, 0x00, 0x24, 0x00, 0x14, 0x00, 0x5f, 0x5f, 0x4d, 0x41, 0x43, 0x4f,
        0x53, 0x58, 0x2f, 0x2e, 0x5f, 0xd0, 0x9f, 0xd1, 0x80, 0xd0, 0xb8, 0xd0, 0xb2, 0xd0, 0xb5, 0xd1, 0x82, 0x2c,
        0x20, 0xd0, 0x9c, 0xd0, 0xb8, 0xd1, 0x80, 0x21, 0x2e, 0x74, 0x78, 0x74, 0x55, 0x54, 0x01, 0x00, 0x00, 0x75,
        0x78, 0x0b, 0x00, 0x01, 0x04, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x63, 0x60, 0x15, 0x63,
        0x67, 0x60, 0x62, 0x60, 0xf0, 0x4d, 0x4c, 0x56, 0xf0, 0x0f, 0x56, 0x88, 0x50, 0x80, 0x02, 0x90, 0x18, 0x03,
        0x27, 0x10, 0x1b, 0x01, 0xf1, 0x26, 0x20, 0x06, 0xf1, 0x9f, 0x30, 0x10, 0x05, 0x1c, 0x43, 0x42, 0x82, 0xa0,
        0x4c, 0x90, 0x8e, 0x23, 0x40, 0xac, 0x80, 0xa6, 0x84, 0x09, 0x2a, 0xce, 0xcf, 0xc0, 0x20, 0x9e, 0x9c, 0x9f,
        0xab, 0x97, 0x58, 0x50, 0x90, 0x93, 0xaa, 0x17, 0x92, 0x5a, 0x51, 0xe2, 0x9a, 0x97, 0x9c, 0x9f, 0x92, 0x99,
        0x97, 0x0e, 0x51, 0x77, 0x05, 0x88, 0x05, 0x18, 0x18, 0xa4, 0x10, 0x6a, 0x72, 0x12, 0x8b, 0x4b, 0x4a, 0x8b,
        0x53, 0x53, 0x52, 0x12, 0x4b, 0x52, 0x95, 0x03, 0x82, 0x41, 0x8a, 0x4a, 0x4b, 0xd2, 0x74, 0x2d, 0xac, 0x0d,
        0x8d, 0x4d, 0x8c, 0x0c, 0xcd, 0x2d, 0x2d, 0x4c, 0x18, 0x18, 0xce, 0xcc, 0x4c, 0x06, 0x89, 0x3f, 0x73, 0x11,
        0x31, 0x04, 0xd1, 0x00, 0x50, 0x4b, 0x07, 0x08, 0x1b, 0x54, 0x58, 0x7a, 0x86, 0x00, 0x00, 0x00, 0xe4, 0x00,
        0x00, 0x00, 0x50, 0x4b, 0x01, 0x02, 0x14, 0x03, 0x14, 0x00, 0x08, 0x08, 0x08, 0x00, 0xb3, 0x69, 0x8e, 0x55,
        0xd2, 0x63, 0x48, 0x88, 0x05, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x19, 0x00, 0x20, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xa4, 0x81, 0x00, 0x00, 0x00, 0x00, 0xd0, 0x9f, 0xd1, 0x80, 0xd0, 0xb8,
        0xd0, 0xb2, 0xd0, 0xb5, 0xd1, 0x82, 0x2c, 0x20, 0xd0, 0x9c, 0xd0, 0xb8, 0xd1, 0x80, 0x21, 0x2e, 0x74, 0x78,
        0x74, 0x55, 0x54, 0x0d, 0x00, 0x07, 0x02, 0xcc, 0x99, 0x63, 0x04, 0xcc, 0x99, 0x63, 0x02, 0xcc, 0x99, 0x63,
        0x75, 0x78, 0x0b, 0x00, 0x01, 0x04, 0xf5, 0x01, 0x00, 0x00, 0x04, 0x14, 0x00, 0x00, 0x00, 0x50, 0x4b, 0x01,
        0x02, 0x14, 0x03, 0x14, 0x00, 0x08, 0x08, 0x08, 0x00, 0x00, 0x00, 0x21, 0x00, 0x1b, 0x54, 0x58, 0x7a, 0x86,
        0x00, 0x00, 0x00, 0xe4, 0x00, 0x00, 0x00, 0x24, 0x00, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0xa4, 0x81, 0x6c, 0x00, 0x00, 0x00, 0x5f, 0x5f, 0x4d, 0x41, 0x43, 0x4f, 0x53, 0x58, 0x2f, 0x2e, 0x5f,
        0xd0, 0x9f, 0xd1, 0x80, 0xd0, 0xb8, 0xd0, 0xb2, 0xd0, 0xb5, 0xd1, 0x82, 0x2c, 0x20, 0xd0, 0x9c, 0xd0, 0xb8,
        0xd1, 0x80, 0x21, 0x2e, 0x74, 0x78, 0x74, 0x55, 0x54, 0x01, 0x00, 0x00, 0x75, 0x78, 0x0b, 0x00, 0x01, 0x04,
        0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x50, 0x4b, 0x05, 0x06, 0x00, 0x00, 0x00, 0x00, 0x02,
        0x00, 0x02, 0x00, 0xcd, 0x00, 0x00, 0x00, 0x58, 0x01, 0x00, 0x00, 0x00, 0x00};
    const unsigned int __2_zip_len = 571;

#if 0
    // Compressed with Windows 7 - it managed to use Cyrillic DOS encoding :-/
    // Currently unsupported
    static const unsigned char __3_zip[] = {
        0x50, 0x4b, 0x03, 0x04, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x6b, 0x8e, 0x55, 0xd2, 0x63, 0x48,
        0x88, 0x03, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x8f, 0xe0, 0xa8, 0xa2,
        0xa5, 0xe2, 0x2c, 0x20, 0x8c, 0xa8, 0xe0, 0x21, 0x2e, 0x74, 0x78, 0x74, 0x31, 0x32, 0x33, 0x50, 0x4b,
        0x01, 0x02, 0x14, 0x00, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x6b, 0x8e, 0x55, 0xd2, 0x63, 0x48,
        0x88, 0x03, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x01, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x8f, 0xe0, 0xa8, 0xa2, 0xa5, 0xe2, 0x2c,
        0x20, 0x8c, 0xa8, 0xe0, 0x21, 0x2e, 0x74, 0x78, 0x74, 0x50, 0x4b, 0x05, 0x06, 0x00, 0x00, 0x00, 0x00,
        0x01, 0x00, 0x01, 0x00, 0x3e, 0x00, 0x00, 0x00, 0x31, 0x00, 0x00, 0x00, 0x00, 0x00};
    static const unsigned int __3_zip_len = 133;
#endif

#if 0
    // Compressed with FAR on Windows - it also uses Cyrillic DOS encoding :-/
    // Currently unsupported
    static const unsigned char __4_zip[] = {
        0x50, 0x4b, 0x03, 0x04, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x6b, 0x8e, 0x55, 0xd2, 0x63, 0x48,
        0x88, 0x03, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x8f, 0xe0, 0xa8, 0xa2,
        0xa5, 0xe2, 0x2c, 0x20, 0x8c, 0xa8, 0xe0, 0x21, 0x2e, 0x74, 0x78, 0x74, 0x31, 0x32, 0x33, 0x50, 0x4b,
        0x01, 0x02, 0x3f, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x6b, 0x8e, 0x55, 0xd2, 0x63, 0x48,
        0x88, 0x03, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x10, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x8f, 0xe0, 0xa8, 0xa2, 0xa5, 0xe2, 0x2c,
        0x20, 0x8c, 0xa8, 0xe0, 0x21, 0x2e, 0x74, 0x78, 0x74, 0x0a, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x01, 0x00, 0x18, 0x00, 0x9b, 0x2d, 0x24, 0x57, 0xbf, 0x0f, 0xd9, 0x01, 0x9b, 0x2d, 0x24, 0x57, 0xbf,
        0x0f, 0xd9, 0x01, 0xb0, 0x8e, 0x51, 0x49, 0xbf, 0x0f, 0xd9, 0x01, 0x50, 0x4b, 0x05, 0x06, 0x00, 0x00,
        0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x62, 0x00, 0x00, 0x00, 0x31, 0x00, 0x00, 0x00, 0x00, 0x00};
    static const unsigned int __4_zip_len = 169;
#endif

    auto check = [](std::span<const std::byte> _bytes) {
        TestDir dir;
        const auto path = std::filesystem::path(dir.directory) / "tmp.zip";
        REQUIRE(nc::base::WriteAtomically(path, _bytes));

        std::shared_ptr<ArchiveHost> host;
        REQUIRE_NOTHROW(host = std::make_shared<ArchiveHost>(path.c_str(), TestEnv().vfs_native));

        CHECK(host->StatTotalFiles() == 1);
        CHECK(host->StatTotalDirs() == 0);
        CHECK(host->StatTotalRegs() == 1);

        VFSFilePtr file;
        REQUIRE(host->CreateFile(reinterpret_cast<const char *>(u8"/Привет, Мир!.txt"), file, 0) == 0);
        REQUIRE(file->Open(VFSFlags::OF_Read) == 0);

        auto bytes = file->ReadFile();
        REQUIRE(bytes);
        REQUIRE(bytes->size() == 3);
        CHECK(std::memcmp(bytes->data(), "123", 4) == 0);
    };
    check({reinterpret_cast<const std::byte *>(__1_zip), __1_zip_len});
    check({reinterpret_cast<const std::byte *>(__2_zip), __2_zip_len});
}

// https://github.com/libarchive/libarchive/issues/151#issuecomment-91876716
// TODO: these don't work (normalization...)
// 表だよ\新しいフォルダ\新規テキスト ドキュメント.txt
TEST_CASE(PREFIX "Can unrar a file with japanese filenames")
{
    const unsigned char __longname_jp_rar[] = {
        0x52, 0x61, 0x72, 0x21, 0x1a, 0x07, 0x00, 0xcf, 0x90, 0x73, 0x00, 0x00, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0xa8, 0x29, 0x74, 0x20, 0x92, 0x94, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02,
        0x00, 0x00, 0x00, 0x00, 0x22, 0x69, 0xe1, 0x3e, 0x14, 0x30, 0x6f, 0x00, 0x20, 0x00, 0x00, 0x00, 0x95, 0x5c,
        0x82, 0xbe, 0x82, 0xe6, 0x5c, 0x90, 0x56, 0x82, 0xb5, 0x82, 0xa2, 0x83, 0x74, 0x83, 0x48, 0x83, 0x8b, 0x83,
        0x5f, 0x5c, 0x90, 0x56, 0x8b, 0x4b, 0x83, 0x65, 0x83, 0x4c, 0x83, 0x58, 0x83, 0x67, 0x20, 0x83, 0x68, 0x83,
        0x4c, 0x83, 0x85, 0x83, 0x81, 0x83, 0x93, 0x83, 0x67, 0x2e, 0x74, 0x78, 0x74, 0x00, 0x88, 0x68, 0x68, 0x60,
        0x30, 0x88, 0x30, 0x5c, 0xaa, 0xb0, 0x65, 0x57, 0x30, 0x44, 0x30, 0xd5, 0x30, 0xa8, 0xa9, 0x30, 0xeb, 0x30,
        0xc0, 0x30, 0x5c, 0xaa, 0xb0, 0x65, 0x8f, 0x89, 0xc6, 0x30, 0xad, 0x30, 0xa2, 0xb9, 0x30, 0xc8, 0x30, 0x20,
        0xc9, 0x30, 0xaa, 0xad, 0x30, 0xe5, 0x30, 0xe1, 0x30, 0xf3, 0x30, 0x80, 0xc8, 0x30, 0x2e, 0x74, 0x78, 0x00,
        0x74, 0x00, 0xf0, 0x26, 0x39, 0x84, 0xe4, 0x98, 0x74, 0x20, 0x92, 0x93, 0x00, 0x05, 0x00, 0x00, 0x00, 0x05,
        0x00, 0x00, 0x00, 0x02, 0xdc, 0x9d, 0x6f, 0x42, 0xb6, 0x54, 0x74, 0x3e, 0x14, 0x30, 0x6e, 0x00, 0x20, 0x00,
        0x00, 0x00, 0x95, 0x5c, 0x82, 0xbe, 0x82, 0xe6, 0x5c, 0x8a, 0xbf, 0x8e, 0x9a, 0x92, 0xb7, 0x82, 0xa2, 0x83,
        0x74, 0x83, 0x40, 0x83, 0x43, 0x83, 0x8b, 0x96, 0xbc, 0x6c, 0x6f, 0x6e, 0x67, 0x2d, 0x66, 0x69, 0x6c, 0x65,
        0x6e, 0x61, 0x6d, 0x65, 0x2d, 0x69, 0x6e, 0x2d, 0x8a, 0xbf, 0x8e, 0x9a, 0x2e, 0x74, 0x78, 0x74, 0x00, 0x88,
        0x68, 0x68, 0x60, 0x30, 0x88, 0x30, 0x5c, 0xaa, 0x22, 0x6f, 0x57, 0x5b, 0x77, 0x95, 0x44, 0x30, 0xaa, 0xd5,
        0x30, 0xa1, 0x30, 0xa4, 0x30, 0xeb, 0x30, 0x80, 0x0d, 0x54, 0x6c, 0x6f, 0x6e, 0x00, 0x67, 0x2d, 0x66, 0x69,
        0x00, 0x6c, 0x65, 0x6e, 0x61, 0x00, 0x6d, 0x65, 0x2d, 0x69, 0x0a, 0x6e, 0x2d, 0x22, 0x6f, 0x57, 0x5b, 0x00,
        0x2e, 0x74, 0x78, 0x74, 0x00, 0xb0, 0x76, 0x7a, 0x68, 0x6b, 0x61, 0x6e, 0x6a, 0x69, 0x94, 0x2c, 0x74, 0xe0,
        0x92, 0x53, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x22, 0x69,
        0xe1, 0x3e, 0x14, 0x30, 0x2e, 0x00, 0x10, 0x00, 0x00, 0x00, 0x95, 0x5c, 0x82, 0xbe, 0x82, 0xe6, 0x5c, 0x90,
        0x56, 0x82, 0xb5, 0x82, 0xa2, 0x83, 0x74, 0x83, 0x48, 0x83, 0x8b, 0x83, 0x5f, 0x00, 0x88, 0x68, 0x68, 0x60,
        0x30, 0x88, 0x30, 0x5c, 0xaa, 0xb0, 0x65, 0x57, 0x30, 0x44, 0x30, 0xd5, 0x30, 0xa8, 0xa9, 0x30, 0xeb, 0x30,
        0xc0, 0x30, 0x00, 0xf0, 0x26, 0x39, 0x84, 0x6d, 0x78, 0x74, 0xe0, 0x92, 0x33, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x69, 0xe1, 0x3e, 0x14, 0x30, 0x0e, 0x00, 0x10,
        0x00, 0x00, 0x00, 0x95, 0x5c, 0x82, 0xbe, 0x82, 0xe6, 0x00, 0x88, 0x68, 0x68, 0x60, 0x30, 0x88, 0x30, 0x00,
        0xf0, 0xea, 0x09, 0x7e, 0xc4, 0x3d, 0x7b, 0x00, 0x40, 0x07, 0x00};

    TestDir dir;
    const auto path = std::filesystem::path(dir.directory) / "tmp.zip";
    REQUIRE(nc::base::WriteAtomically(
        path, {reinterpret_cast<const std::byte *>(__longname_jp_rar), std::size(__longname_jp_rar)}));

    std::shared_ptr<ArchiveHost> host;
    REQUIRE_NOTHROW(host = std::make_shared<ArchiveHost>(path.c_str(), TestEnv().vfs_native));

    CHECK(host->StatTotalFiles() == 4);
    CHECK(host->StatTotalDirs() == 2);
    CHECK(host->StatTotalRegs() == 2);

    VFSFilePtr file;
    REQUIRE(host->CreateFile(reinterpret_cast<const char *>(u8"/表だよ/新しいフォルダ/新規テキスト ドキュメント.txt"),
                             file,
                             0) == 0);
    REQUIRE(file->Open(VFSFlags::OF_Read) == 0);
    auto bytes = file->ReadFile();
    REQUIRE(bytes);
    REQUIRE(bytes->size() == 0);

    REQUIRE(host->CreateFile(
                reinterpret_cast<const char *>(u8"/表だよ/漢字長いファイル名long-filename-in-漢字.txt"), file, 0) == 0);
    REQUIRE(file->Open(VFSFlags::OF_Read) == 0);
    bytes = file->ReadFile();
    REQUIRE(bytes);
    REQUIRE(bytes->size() == 5);
    CHECK(std::memcmp(bytes->data(), "kanji", 5) == 0);
}

// https://github.com/libarchive/libarchive/issues/151#issuecomment-91876705
TEST_CASE(PREFIX "Can unrar a file with cyrilic filenames")
{
    const unsigned char __test_ru_rar[] = {
        0x52, 0x61, 0x72, 0x21, 0x1a, 0x07, 0x00, 0xcf, 0x90, 0x73, 0x00, 0x00, 0x0d, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x2d, 0x29, 0x74, 0x20, 0x82, 0x36, 0x00, 0x06, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00,
        0x00, 0x03, 0xcc, 0xe1, 0x81, 0x14, 0xcb, 0xa3, 0x3f, 0x3e, 0x14, 0x30, 0x16, 0x00, 0xed, 0x81, 0x00,
        0x00, 0xd0, 0x9f, 0xd0, 0xa0, 0xd0, 0x98, 0xd0, 0x92, 0xd0, 0x95, 0xd0, 0xa2, 0x00, 0x04, 0x55, 0x1f,
        0x20, 0x18, 0x12, 0x50, 0x15, 0x22, 0xf0, 0xf2, 0xe9, 0xf7, 0xe5, 0xf4, 0xbd, 0xac, 0x74, 0x20, 0x82,
        0x36, 0x00, 0x06, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x03, 0xcc, 0xe1, 0x81, 0x14, 0x6b, 0xa5,
        0x3f, 0x3e, 0x14, 0x30, 0x16, 0x00, 0xed, 0x81, 0x00, 0x00, 0xd0, 0xbf, 0xd1, 0x80, 0xd0, 0xb8, 0xd0,
        0xb2, 0xd0, 0xb5, 0xd1, 0x82, 0x00, 0x04, 0x55, 0x3f, 0x40, 0x38, 0x32, 0x50, 0x35, 0x42, 0xf0, 0xf2,
        0xe9, 0xf7, 0xe5, 0xf4, 0xc4, 0x3d, 0x7b, 0x00, 0x40, 0x07, 0x00};

    TestDir dir;
    const auto path = std::filesystem::path(dir.directory) / "tmp.zip";
    REQUIRE(nc::base::WriteAtomically(path,
                                      {reinterpret_cast<const std::byte *>(__test_ru_rar), std::size(__test_ru_rar)}));

    std::shared_ptr<ArchiveHost> host;
    REQUIRE_NOTHROW(host = std::make_shared<ArchiveHost>(path.c_str(), TestEnv().vfs_native));

    CHECK(host->StatTotalFiles() == 2);
    CHECK(host->StatTotalDirs() == 0);
    CHECK(host->StatTotalRegs() == 2);

    VFSFilePtr file;
    REQUIRE(host->CreateFile(reinterpret_cast<const char *>(u8"/ПРИВЕТ"), file, 0) == 0);
    REQUIRE(file->Open(VFSFlags::OF_Read) == 0);
    auto bytes = file->ReadFile();
    REQUIRE(bytes);
    REQUIRE(bytes->size() == 6);
    CHECK(std::memcmp(bytes->data(), "\xf0\xf2\xe9\xf7\xe5\xf4", 6) == 0);

    REQUIRE(host->CreateFile(reinterpret_cast<const char *>(u8"/привет"), file, 0) == 0);
    REQUIRE(file->Open(VFSFlags::OF_Read) == 0);
    bytes = file->ReadFile();
    REQUIRE(bytes);
    REQUIRE(bytes->size() == 6);
    CHECK(std::memcmp(bytes->data(), "\xf0\xf2\xe9\xf7\xe5\xf4", 6) == 0);
}
