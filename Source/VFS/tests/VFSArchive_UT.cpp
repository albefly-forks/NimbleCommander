// Copyright (C) 2022-2023 Michael Kazakov. Subject to GNU General Public License version 3.
#include "Tests.h"
#include "TestEnv.h"
#include <VFS/VFS.h>
#include <VFS/ArcLA.h>
#include "../source/ArcLA/Internal.h" // FIXME!
#include <VFS/VFSGenericMemReadOnlyFile.h>
#include <Base/WriteAtomically.h>

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
        CHECK(std::memcmp(bytes->data(), "123", 3) == 0);
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

TEST_CASE(PREFIX "Symlinks handling")
{
    // dir r/d
    // reg r/f
    // sym r/l0 -> f
    // sym r/l1 -> ./f
    // sym r/l2 -> l1
    // sym r/l3 -> ./l2
    // sym r/l4 -> ./l5 (cycle)
    // sym r/l5 -> ./l4 (cycle)
    // sym r/l6 -> d/l0
    // sym r/l7 -> ./d/l0
    // sym r/l8 -> ./d/../f
    // sym r/l9 -> l9
    // sym r/l10 -> ./././././l10
    // sym r/d/l0 -> ../f
    // sym r/d/l1 -> ./../f
    // sym r/d/l2 -> ././../f
    // sym r/d/l3 -> ./.././d/./../f
    // sym r/d/l4 -> ../d/../d/../d/../f
    // sym r/d/l5 -> ./../l3
    const unsigned char arc_tar_gz[] = {
        0x1f, 0x8b, 0x08, 0x00, 0xa9, 0xfb, 0x6c, 0x65, 0x00, 0x03, 0xed, 0x9a, 0x4d, 0x6c, 0x1b, 0x45, 0x14, 0xc7,
        0x37, 0x44, 0x88, 0xd4, 0x02, 0xc1, 0x29, 0x95, 0x22, 0x0e, 0x8b, 0x90, 0xe0, 0x00, 0x78, 0xe7, 0x73, 0x27,
        0x2e, 0x8a, 0x54, 0xa7, 0x4e, 0x62, 0x93, 0xd8, 0xf1, 0x57, 0x12, 0x27, 0x97, 0x68, 0x6d, 0xaf, 0x4d, 0x92,
        0xf5, 0x47, 0xbd, 0xeb, 0xc4, 0x8d, 0x04, 0x37, 0xa8, 0xc4, 0x95, 0xf2, 0x71, 0x04, 0x81, 0x90, 0x38, 0x70,
        0xa8, 0x90, 0x90, 0x38, 0xa1, 0x4a, 0xa4, 0xbd, 0x20, 0x48, 0x25, 0x54, 0xa1, 0x70, 0x41, 0x2a, 0xa2, 0xf7,
        0xaa, 0x95, 0x4a, 0x25, 0x24, 0x66, 0x77, 0x9d, 0x84, 0x1a, 0x25, 0xcd, 0x96, 0x8e, 0x4b, 0x95, 0xf9, 0x47,
        0xde, 0xb1, 0x9d, 0xd9, 0x7d, 0xb3, 0xfb, 0xdb, 0xf7, 0xf6, 0xcd, 0xf3, 0xb4, 0x34, 0x45, 0xb8, 0x00, 0x00,
        0x8c, 0x52, 0xd5, 0x6b, 0x75, 0xbf, 0x05, 0x88, 0xf8, 0x6d, 0x57, 0x2a, 0x24, 0x14, 0x63, 0xc8, 0xa8, 0x4e,
        0xa0, 0x0a, 0x20, 0xd4, 0x21, 0x52, 0x54, 0x2a, 0x7e, 0x68, 0x8a, 0xd2, 0xb6, 0x1d, 0xa3, 0xc5, 0x87, 0x52,
        0x5b, 0xa9, 0xb6, 0xeb, 0x87, 0xf4, 0xe3, 0xdd, 0x2a, 0x95, 0x43, 0xfe, 0xdf, 0x3d, 0x8f, 0xbd, 0xf6, 0x31,
        0x51, 0x4b, 0xb3, 0x20, 0x14, 0x6c, 0x23, 0x38, 0x7f, 0x44, 0x11, 0x56, 0x54, 0x14, 0xd6, 0x2c, 0xe1, 0xf7,
        0x80, 0xe4, 0x0f, 0x81, 0x60, 0x1b, 0x41, 0xf8, 0x03, 0x48, 0x39, 0x7f, 0x8c, 0x29, 0xf5, 0xf8, 0xef, 0xfe,
        0x89, 0x1b, 0xa4, 0xe4, 0x8f, 0x45, 0xdb, 0x38, 0x3a, 0x7f, 0x88, 0x79, 0xeb, 0xfa, 0x3f, 0xc1, 0xc0, 0xf7,
        0x7f, 0x24, 0x7a, 0x70, 0x92, 0x3f, 0x11, 0x6d, 0x23, 0x08, 0x7f, 0x46, 0x99, 0xc7, 0x9f, 0x50, 0x19, 0xff,
        0xfb, 0xa1, 0x56, 0x1f, 0x2e, 0x71, 0x00, 0xfe, 0x04, 0x50, 0xe4, 0xfb, 0x3f, 0xf2, 0xf9, 0x0b, 0xbf, 0x39,
        0x25, 0x7f, 0xe1, 0x21, 0x36, 0x50, 0xfc, 0x07, 0xba, 0xcb, 0x1f, 0xe9, 0x3a, 0xe7, 0x6f, 0x89, 0xce, 0x4c,
        0x15, 0xc9, 0x5f, 0x0b, 0x2f, 0x1f, 0x76, 0x5a, 0x0f, 0x43, 0xfc, 0x7a, 0xe8, 0x84, 0x1c, 0xcc, 0x5f, 0x27,
        0x7b, 0xfc, 0x11, 0x83, 0x5e, 0xfe, 0x0f, 0x75, 0xa2, 0xa8, 0xa2, 0xf3, 0x52, 0x4f, 0xc7, 0x9c, 0xbf, 0xf2,
        0xe4, 0xf0, 0x53, 0xca, 0x13, 0x8a, 0x92, 0x34, 0x4a, 0xea, 0x6c, 0x4e, 0x2d, 0xa8, 0x5d, 0xb9, 0xdf, 0x29,
        0x27, 0xf8, 0x8b, 0x47, 0x87, 0x01, 0xf7, 0xba, 0xf0, 0xcf, 0x03, 0x9f, 0x1c, 0xed, 0x90, 0xd1, 0x7c, 0x3e,
        0xeb, 0xbf, 0x73, 0xf7, 0x18, 0x18, 0xe2, 0x6f, 0x3e, 0xee, 0xe9, 0x32, 0xd8, 0xfd, 0xfe, 0x59, 0x45, 0x39,
        0x59, 0x6a, 0xd4, 0xc2, 0x46, 0xb3, 0x69, 0x99, 0xe1, 0xbc, 0xd9, 0x71, 0x26, 0xea, 0xa5, 0x46, 0x79, 0xa5,
        0x5e, 0xf5, 0xf7, 0x3f, 0xc9, 0x37, 0xcf, 0x29, 0xca, 0xc8, 0x7e, 0x1f, 0xcb, 0xb0, 0x9d, 0xb6, 0x6d, 0x96,
        0xcb, 0x86, 0x63, 0xbe, 0x98, 0xce, 0x75, 0xed, 0xbc, 0xcc, 0x37, 0xe7, 0x14, 0x85, 0xed, 0xf7, 0xab, 0x99,
        0x8e, 0xc1, 0xfb, 0x18, 0xa7, 0xd6, 0x92, 0xb1, 0x19, 0xa3, 0x68, 0x5a, 0xcb, 0xc4, 0x68, 0x33, 0x7b, 0xcd,
        0x2e, 0xda, 0x9d, 0xfa, 0xea, 0x9a, 0x5d, 0xe9, 0x54, 0xd8, 0x86, 0x55, 0xae, 0x17, 0xcf, 0x12, 0xf7, 0x19,
        0xd3, 0x76, 0x2a, 0xaf, 0x8d, 0xbe, 0x0e, 0x31, 0x41, 0x90, 0x45, 0x46, 0xc9, 0xd6, 0x07, 0x96, 0xe9, 0x1e,
        0x38, 0xd1, 0x34, 0x5e, 0x72, 0xdb, 0x9b, 0x37, 0xbe, 0xb9, 0xf3, 0xeb, 0x20, 0xfa, 0x63, 0x5b, 0x5f, 0xfe,
        0x81, 0x7e, 0xfd, 0x9b, 0x3d, 0xb7, 0xf3, 0xe9, 0x9f, 0xda, 0xc5, 0xf7, 0x4f, 0x9f, 0xfe, 0xf2, 0xad, 0xdb,
        0x2f, 0x7c, 0x71, 0x79, 0xeb, 0xda, 0x64, 0xf6, 0x79, 0x76, 0xe7, 0xee, 0xd5, 0x4b, 0xf1, 0xbf, 0xe6, 0xae,
        0xc7, 0x7f, 0xde, 0x3e, 0xf1, 0xf4, 0x50, 0x6d, 0xe4, 0xa3, 0xe5, 0x1f, 0x0b, 0xdf, 0x7f, 0x36, 0x7f, 0x0e,
        0x5c, 0xfe, 0xee, 0xc2, 0xb7, 0x99, 0x1b, 0x57, 0x9a, 0xf0, 0x0a, 0xb9, 0x79, 0x49, 0xbd, 0x3b, 0x71, 0xcd,
        0xb1, 0x87, 0xdf, 0xf9, 0x7c, 0xcb, 0xba, 0xfe, 0x4c, 0xf3, 0x6a, 0x6d, 0x78, 0xa7, 0xfe, 0xee, 0xab, 0x3f,
        0x9d, 0x72, 0x94, 0xaf, 0x2e, 0xbc, 0xfd, 0xe1, 0xce, 0xfa, 0x7b, 0xe6, 0xce, 0xd0, 0xf9, 0x8d, 0x5b, 0x23,
        0xe9, 0xdf, 0x07, 0xcf, 0x5f, 0xdc, 0xfe, 0xe5, 0x41, 0xb1, 0x1e, 0x55, 0x2d, 0x2d, 0x6d, 0x74, 0xe2, 0xa6,
        0x51, 0x36, 0x5b, 0x9a, 0xa8, 0x38, 0x70, 0x1f, 0xff, 0x87, 0x18, 0xe0, 0x1e, 0xff, 0xc7, 0x0c, 0xf2, 0xfc,
        0xaf, 0x23, 0x68, 0x3c, 0xf7, 0xe8, 0x98, 0xfb, 0x3f, 0x62, 0x6a, 0xcd, 0x59, 0xa9, 0x99, 0x63, 0x90, 0x01,
        0xa8, 0x43, 0x0a, 0x48, 0x24, 0xac, 0x47, 0x74, 0x04, 0x48, 0x08, 0xe1, 0x88, 0x3a, 0x93, 0x18, 0x8f, 0x66,
        0xcf, 0xc4, 0x13, 0xf3, 0x13, 0xe1, 0x8e, 0xe1, 0x38, 0xad, 0xf0, 0x83, 0x39, 0xd8, 0xd8, 0x68, 0xdb, 0x44,
        0xaf, 0xa4, 0xec, 0x58, 0xb2, 0x5d, 0xcc, 0xa4, 0x2c, 0x2d, 0x99, 0x2a, 0xa6, 0xab, 0x25, 0x58, 0x30, 0x1a,
        0x95, 0xb5, 0x75, 0x7b, 0x29, 0x15, 0xcd, 0x4c, 0x5b, 0x91, 0x48, 0x6e, 0xaa, 0xd6, 0xb1, 0x0b, 0x0b, 0x59,
        0x2b, 0x51, 0x4a, 0x69, 0xab, 0x3a, 0x28, 0x4d, 0x26, 0xb4, 0xf9, 0xc2, 0x6a, 0x2e, 0x95, 0xcf, 0x9c, 0xc9,
        0x6c, 0x24, 0x8a, 0xd9, 0xb3, 0x85, 0xc2, 0x28, 0x5d, 0xec, 0x4c, 0xa7, 0x16, 0xcc, 0x7c, 0x6c, 0x6a, 0x9d,
        0xe6, 0xe8, 0x9c, 0x59, 0x89, 0x97, 0x62, 0xf1, 0x78, 0x2a, 0x3d, 0x33, 0x9e, 0x48, 0x37, 0x27, 0x61, 0x3d,
        0xbb, 0x39, 0xd9, 0x58, 0x58, 0xeb, 0x2c, 0x6c, 0xae, 0xc6, 0x0a, 0xb1, 0x6c, 0x31, 0x5b, 0x34, 0x8a, 0x8d,
        0x45, 0x7b, 0x73, 0xb3, 0x09, 0xa2, 0xd3, 0x68, 0xae, 0xd2, 0x2c, 0x1a, 0xe5, 0x46, 0xd3, 0x42, 0xd5, 0x95,
        0x78, 0x59, 0xcb, 0x18, 0x73, 0xb3, 0x99, 0xd8, 0x9b, 0x2c, 0x9e, 0x41, 0xd1, 0x10, 0x8c, 0x10, 0x35, 0xc7,
        0x4f, 0x75, 0x66, 0xf1, 0xbf, 0x9d, 0xea, 0x23, 0x0b, 0x15, 0x21, 0xfd, 0x50, 0x5e, 0x3d, 0x81, 0x73, 0xac,
        0xb3, 0x34, 0x6f, 0x2f, 0x65, 0xa2, 0x5c, 0xe3, 0x6f, 0x94, 0xa6, 0x26, 0x6a, 0x51, 0x4f, 0x21, 0x1a, 0x39,
        0xe8, 0x2a, 0xf4, 0x1e, 0xa0, 0x37, 0x46, 0x86, 0x74, 0x72, 0x98, 0xfd, 0x7f, 0x06, 0xf7, 0xb1, 0x72, 0x21,
        0x5b, 0x9b, 0xc9, 0x57, 0x59, 0x32, 0x9f, 0x04, 0xc9, 0xd5, 0x09, 0x3c, 0x9b, 0xaf, 0x82, 0x10, 0xcf, 0x11,
        0x0f, 0x30, 0x7d, 0xcf, 0xbe, 0x3d, 0xa1, 0x3a, 0xf4, 0x68, 0x1d, 0xe8, 0x31, 0x97, 0xb8, 0xa8, 0xbf, 0xaf,
        0xfb, 0xe5, 0x7f, 0xe0, 0x5f, 0xf1, 0x1f, 0x32, 0x02, 0x64, 0xfe, 0xd7, 0x0f, 0x41, 0x24, 0xbc, 0xfc, 0x27,
        0xf5, 0x3f, 0x16, 0x9f, 0xff, 0x33, 0xd1, 0x36, 0x82, 0xd4, 0x7f, 0x10, 0xf1, 0xe6, 0xff, 0xba, 0xee, 0xd7,
        0x7f, 0xcb, 0x9a, 0x25, 0x38, 0x0a, 0x1c, 0x73, 0xff, 0x6f, 0x09, 0xbf, 0xc0, 0xc1, 0xea, 0x3f, 0x00, 0xf9,
        0xf3, 0x7f, 0x06, 0x39, 0x7f, 0xf1, 0x8f, 0x26, 0xc9, 0x5f, 0xb3, 0x22, 0xa2, 0x6d, 0x1c, 0x9d, 0x3f, 0x23,
        0xcc, 0xaf, 0xff, 0x62, 0xef, 0xf7, 0x5f, 0xf1, 0x43, 0x93, 0xfc, 0x35, 0x6b, 0x54, 0xb4, 0x8d, 0x20, 0xf1,
        0x1f, 0x7b, 0xf5, 0x5f, 0xcc, 0x53, 0xc2, 0x6e, 0xfc, 0x0f, 0x87, 0xc5, 0x66, 0xa8, 0x92, 0xbf, 0xf0, 0x22,
        0x7b, 0xa0, 0xf8, 0x4f, 0xb0, 0xe7, 0xff, 0xd8, 0xe7, 0x2f, 0xfe, 0x09, 0x20, 0xf9, 0xeb, 0xa2, 0x6d, 0x04,
        0xf1, 0x7f, 0x48, 0x3d, 0xfe, 0x14, 0x32, 0xce, 0x5f, 0x7c, 0xf6, 0x27, 0xf9, 0xf3, 0x10, 0x2b, 0xda, 0x46,
        0x10, 0xff, 0xd7, 0xfd, 0xdf, 0xff, 0x01, 0x26, 0x72, 0xfd, 0x5f, 0x3f, 0xe4, 0xf2, 0x17, 0xbd, 0x02, 0x28,
        0x08, 0x7f, 0x4a, 0xfc, 0xfa, 0x3f, 0xd2, 0xfd, 0xf5, 0x5f, 0xee, 0xe2, 0xaf, 0xb2, 0x16, 0x16, 0x97, 0x06,
        0x48, 0xfe, 0xc2, 0x17, 0x59, 0x04, 0xf2, 0x7f, 0xe4, 0xe6, 0x7f, 0x84, 0x00, 0x6f, 0xfe, 0xdf, 0x4d, 0x00,
        0xf7, 0x37, 0x0f, 0xff, 0x1e, 0x90, 0xfc, 0x85, 0xaf, 0x00, 0x0a, 0x1e, 0xff, 0x31, 0x04, 0x70, 0xd7, 0xff,
        0x05, 0x47, 0x27, 0xc9, 0x5f, 0xf8, 0x0a, 0xa0, 0x20, 0xfc, 0x09, 0x43, 0x1e, 0x7f, 0xbc, 0xb7, 0xfe, 0x57,
        0xce, 0xff, 0x44, 0xaa, 0xd5, 0x87, 0x12, 0x6b, 0x10, 0xfe, 0x88, 0x02, 0xaf, 0xfe, 0xeb, 0xcf, 0xff, 0xfa,
        0x30, 0x01, 0x94, 0xfc, 0x85, 0x57, 0x00, 0x02, 0xf9, 0x3f, 0xf0, 0xd6, 0x7f, 0x33, 0x86, 0x77, 0xe3, 0xbf,
        0xe0, 0x3b, 0xe0, 0x98, 0xf3, 0x97, 0x92, 0x92, 0x3a, 0xbe, 0xfa, 0x1b, 0xf7, 0x37, 0x65, 0xfb, 0x00, 0x3a,
        0x00, 0x00

    };

    TestDir dir;
    const auto path = std::filesystem::path(dir.directory) / "tmp_tar_gz";
    REQUIRE(nc::base::WriteAtomically(path, {reinterpret_cast<const std::byte *>(arc_tar_gz), std::size(arc_tar_gz)}));
    std::shared_ptr<ArchiveHost> host;
    REQUIRE_NOTHROW(host = std::make_shared<ArchiveHost>(path.c_str(), TestEnv().vfs_native));
    CHECK(host->StatTotalFiles() == 21);
    CHECK(host->StatTotalDirs() == 2);
    CHECK(host->StatTotalRegs() == 1);

    auto readsym = [&](const char *_path) -> std::string {
        char buf[1024];
        REQUIRE(host->ReadSymlink(_path, buf, sizeof(buf), {}) == VFSError::Ok);
        return buf;
    };
    CHECK(readsym("/r/l0") == "f");
    CHECK(readsym("/r/l1") == "./f");
    CHECK(readsym("/r/l2") == "l1");
    CHECK(readsym("/r/l3") == "./l2");
    CHECK(readsym("/r/l4") == "./l5");
    CHECK(readsym("/r/l5") == "./l4");
    CHECK(readsym("/r/l6") == "d/l0");
    CHECK(readsym("/r/l7") == "./d/l0");
    CHECK(readsym("/r/l8") == "./d/../f");
    CHECK(readsym("/r/l9") == "l9");
    CHECK(readsym("/r/l10") == "./././././l10");
    CHECK(readsym("/r/l11") == "./l5");
    CHECK(readsym("/r/d/l0") == "../f");
    CHECK(readsym("/r/d/l1") == "./../f");
    CHECK(readsym("/r/d/l2") == "././../f");
    CHECK(readsym("/r/d/l3") == "./.././d/./../f");
    CHECK(readsym("/r/d/l4") == "../d/../d/../d/../f");
    CHECK(readsym("/r/d/l5") == "./../l3");

    auto symlink = [&](const char *_path) -> const ArchiveHost::Symlink & {
        const arc::DirEntry *entry = host->FindEntry(_path);
        REQUIRE(entry);
        const ArchiveHost::Symlink *symlink = host->ResolvedSymlink(entry->aruid);
        REQUIRE(symlink);
        return *symlink;
    };
    CHECK(symlink("/r/l0").target_path == "/r/f");
    CHECK(symlink("/r/l1").target_path == "/r/f");
    CHECK(symlink("/r/l2").target_path == "/r/f");
    CHECK(symlink("/r/l3").target_path == "/r/f");
    CHECK(symlink("/r/l4").state == ArchiveHost::SymlinkState::Loop);
    CHECK(symlink("/r/l5").state == ArchiveHost::SymlinkState::Loop);
    CHECK(symlink("/r/l6").target_path == "/r/f");
    CHECK(symlink("/r/l7").target_path == "/r/f");
    CHECK(symlink("/r/l8").target_path == "/r/f");
    CHECK(symlink("/r/l9").state == ArchiveHost::SymlinkState::Loop);
    CHECK(symlink("/r/l10").state == ArchiveHost::SymlinkState::Loop);
    CHECK(symlink("/r/l11").state == ArchiveHost::SymlinkState::Loop);
    CHECK(symlink("/r/d/l0").target_path == "/r/f");
    CHECK(symlink("/r/d/l1").target_path == "/r/f");
    CHECK(symlink("/r/d/l2").target_path == "/r/f");
    CHECK(symlink("/r/d/l3").target_path == "/r/f");
    CHECK(symlink("/r/d/l4").target_path == "/r/f");
    CHECK(symlink("/r/d/l5").target_path == "/r/f");
}

TEST_CASE(PREFIX "Symlinks handling - invalid values")
{
    // dir r/d
    // sym r/l0 -> nada
    // sym r/l1 -> ../nada
    // sym r/r2 -> ../.././../../././../nada
    // sym r/r3 -> nada/nada/nada
    // sym r/r4 -> ../nada
    // sym r/r5 -> .././r/./nada
    const unsigned char arc_tar_gz[] = {
        0x1f, 0x8b, 0x08, 0x00, 0x3f, 0xaa, 0x75, 0x65, 0x00, 0x03, 0xed, 0x95, 0xc1, 0x0a, 0xc3, 0x20, 0x0c, 0x86,
        0x7d, 0x14, 0x9f, 0xa0, 0x26, 0x9a, 0xe8, 0xf3, 0x14, 0x46, 0xc7, 0x60, 0xeb, 0xc1, 0xad, 0xef, 0xbf, 0xd4,
        0x4a, 0x8f, 0x05, 0x61, 0x39, 0x0c, 0xf2, 0x1f, 0xfa, 0x1f, 0x1a, 0xf8, 0x23, 0x9f, 0x26, 0x35, 0x38, 0x75,
        0x01, 0x40, 0x61, 0xf6, 0xcd, 0xf3, 0xe1, 0x10, 0xe9, 0xf0, 0x2e, 0x8f, 0xc4, 0x89, 0x53, 0x64, 0x28, 0xe0,
        0x01, 0x31, 0x03, 0x3b, 0xcf, 0xfa, 0xad, 0x39, 0xb7, 0xbd, 0x3f, 0x73, 0x95, 0x56, 0x5e, 0x8f, 0xfb, 0xb6,
        0x5e, 0xd4, 0x49, 0xd9, 0xb2, 0x5c, 0xfc, 0xef, 0xe7, 0x38, 0xfd, 0x4f, 0x54, 0xc3, 0x33, 0x69, 0x67, 0x0c,
        0xf0, 0xa7, 0x98, 0x50, 0xf8, 0x13, 0x15, 0x74, 0x3e, 0xae, 0xf3, 0x6d, 0x0e, 0xe7, 0x47, 0xa7, 0x39, 0xe3,
        0x4f, 0xda, 0x19, 0x43, 0xfc, 0x39, 0x0b, 0xff, 0x84, 0x90, 0x85, 0xff, 0x34, 0x29, 0x82, 0xef, 0x32, 0xfe,
        0xea, 0x63, 0x76, 0x84, 0x7f, 0xc2, 0xb2, 0xf3, 0xcf, 0x04, 0x07, 0xff, 0x29, 0xd4, 0xa0, 0x7a, 0x0b, 0x8c,
        0x7f, 0xd4, 0xce, 0x18, 0x7a, 0xff, 0xd0, 0xe6, 0x3f, 0x17, 0xee, 0xfc, 0xf7, 0x2b, 0x70, 0x7a, 0xf8, 0xfd,
        0x44, 0x30, 0xfe, 0xa0, 0x9d, 0x31, 0xc2, 0x5f, 0x9e, 0xbe, 0xf0, 0x8f, 0x39, 0x53, 0xdf, 0xff, 0xda, 0xcd,
        0x19, 0x7f, 0xd4, 0xce, 0x18, 0xe2, 0x2f, 0x85, 0x6d, 0xff, 0xa3, 0xed, 0x7f, 0x93, 0xc9, 0x64, 0xd2, 0xd4,
        0x17, 0xa4, 0xe6, 0x84, 0x17, 0x00, 0x12, 0x00, 0x00};

    TestDir dir;
    const auto path = std::filesystem::path(dir.directory) / "tmp_tar_gz";
    REQUIRE(nc::base::WriteAtomically(path, {reinterpret_cast<const std::byte *>(arc_tar_gz), std::size(arc_tar_gz)}));
    std::shared_ptr<ArchiveHost> host;
    REQUIRE_NOTHROW(host = std::make_shared<ArchiveHost>(path.c_str(), TestEnv().vfs_native));
    CHECK(host->StatTotalFiles() == 7);
    CHECK(host->StatTotalDirs() == 1);
    CHECK(host->StatTotalRegs() == 0);

    auto readsym = [&](const char *_path) -> std::string {
        char buf[1024];
        REQUIRE(host->ReadSymlink(_path, buf, sizeof(buf), {}) == VFSError::Ok);
        return buf;
    };
    CHECK(readsym("/r/l0") == "nada");
    CHECK(readsym("/r/l1") == "../nada");
    CHECK(readsym("/r/l2") == "../.././../../././../nada");
    CHECK(readsym("/r/l3") == "nada/nada/nada");
    CHECK(readsym("/r/l4") == "../nada");
    CHECK(readsym("/r/l5") == ".././r/./nada");

    auto symlink = [&](const char *_path) -> const ArchiveHost::Symlink & {
        const arc::DirEntry *entry = host->FindEntry(_path);
        REQUIRE(entry);
        const ArchiveHost::Symlink *symlink = host->ResolvedSymlink(entry->aruid);
        REQUIRE(symlink);
        return *symlink;
    };
    CHECK(symlink("/r/l0").state == ArchiveHost::SymlinkState::Invalid);
    CHECK(symlink("/r/l1").state == ArchiveHost::SymlinkState::Invalid);
    CHECK(symlink("/r/l2").state == ArchiveHost::SymlinkState::Invalid);
    CHECK(symlink("/r/l3").state == ArchiveHost::SymlinkState::Invalid);
    CHECK(symlink("/r/l4").state == ArchiveHost::SymlinkState::Invalid);
    CHECK(symlink("/r/l5").state == ArchiveHost::SymlinkState::Invalid);
}
